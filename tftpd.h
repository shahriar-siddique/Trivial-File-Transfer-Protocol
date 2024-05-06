#include "libsys/vos/vos_task.h"
#include "libsys/vos/vos_memory.h"
#include <ip/socket.h>
#include <ip/msg.h>
#include <ip/inet.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <libfile/file_sys.h>
#include <libsys/timer.h>
#include <libdev/modules.h>
#include <libsys/verctl.h>
#include <libvty/log.h>

#include <libcmd/cmdparse.h>
#include <libcmd/argparse.h>
#include <libcmd/cmderror.h>

#define MAX_PORT_NUMBER 65535
#define DEFAULT_PORT 69
#define MAX_BUFFER_SIZE 512
#define MSG_QUEUE_SIZE 256
#define MAX_TFTP_PACKET_SIZE 516
#define MAX_READ_SESSION 3
#define MAX_WRITE_SESSION 1
#define MAX_STACK_SIZE 16320
#define MAX_SESSION 3
#define MAX_FILENAME_LEN 255
#define TFTP_CONNECTION_TIMEOUT 101
#define MAX_RETRY 5
#define MAX_TIMEOUT_SECOND 5
#define NEED_DELAY 102
#define TFTP_HEADER_SIZE 4
#define ACTIVE 1
#define	NOT_ACTIVE 0
#define READING 1
#define WRITING 2
#define IDLE 0
#define DUPLICATE_REQUEST 1
#define NO_DUPLICATE_REQUEST 0


#define TFTP_MAX_FILENAME_LEN 255
#define TFTP_MAX_MODE_SIZE_LEN 9
#define TFTP_MAX_OPTION_LEN 8
#define TFTP_MAX_FILE_SIZE_LEN 5
#define TFTP_MAX_ERROR_BUFFER_SIZE 255


#define TFTP_ERROR_NOT_DEFINED 0 // Not defined, see error message (if any).
#define TFTP_ERROR_FILE_NOT_FOUND 1 // File not found.
#define TFTP_ERROR_ACCESS_VIOLATION 2 // Access violation.
#define TFTP_ERROR_DISK_FULL 3 // Disk full or allocation exceeded.
#define TFTP_ERROR_ILLEGAL_OPERATION 4 // Illegal TFTP operation.
#define TFTP_ERROR_UNKNOWN_TRANSFER 5 // Unknown transfer ID.
#define TFTP_ERROR_FILE_EXISTS 6 // File already exists.
#define TFTP_ERROR_NO_SUCH_USER 7 // No such user.

#define TFTP_OP_RRQ 1 // Read request
#define TFTP_OP_WRQ 2 // Write request
#define TFTP_OP_DATA 3 // Data packet
#define TFTP_OP_ACK 4 // Acknowledgment
#define TFTP_OP_ERROR 5 // Error packet
#define TFTP_OP_OACK 6 // Option Acknowledgment

#define TFTP_SERVER_ENABLE 1
#define TFTP_SERVER_DISABLE 0
#define TFTP_SERVER_TIMEOUT_CHANGE 2
#define TFTP_SERVER_TIMEOUT_DELETE 3
#define TFTP_SERVER_RETRY_COUNT 4
#define TFTP_SERVER_RETRY_COUNT_DELETE 5
#define TFTP_SERVER_PORT_CHANGE 6
#define TFTP_SERVER_PORT_DEFAULT 7

typedef struct tftp_rrq_wrq_hdr
{
	uint16 tftp_op_code;
	uint8 tftp_file_and_mode[0];
	
}__attribute__((packed)) tftpdhdr_rrq_wrq_t;

typedef struct tftp_data_hdr
{
	uint16 tftp_op_code;
	uint16 tftp_block;
	uint8 tftp_data[0];
	
}__attribute__((packed)) tftpdhdr_data_t;

typedef struct tftp_ack_hdr
{
	uint16 tftp_op_code;
	uint16 tftp_block;
	
}__attribute__((packed)) tftpdhdr_ack_t;

typedef struct tftp_oack_hdr
{
	uint16 tftp_op_code;
	uint8 tftp_option[0];
	
}__attribute__((packed)) tftpdhdr_oack_t;

typedef struct tftp_err_hdr
{
	uint16 tftp_op_code;
	uint16 tftp_err_code;
	uint8 error_msg[0];
	
}__attribute__((packed)) tftpdhdr_err_t;

typedef struct
{
	uint32 session_id;
	uint16 client_port;
	uint16 source_port;
	uint16 transfer_block;
	uint16 block_size;
	uint8 current_status;
	uint8 is_active;
	uint8 filename[MAX_FILENAME_LEN];
	uint8 buffer[MAX_TFTP_PACKET_SIZE];
	struct sockaddr_in client_address;
} tftp_session_t;


static int cmd_conf_tftp(int argc, char **argv, struct user *u);
static int cmd_conf_tftp_server(int argc, char **argv, struct user *u);
static int tftp_server_enable(int argc, char **argv, struct user *u);
static int tftp_server_port (int argc, char **argv, struct user *u);
static int tftp_server_retransmit(int argc, char **argv, struct user *u);
int show_tftp(int argc, char **argv, struct user *u);
static int do_show_tftp_config_session(int argc, char **argv, struct user *u);


extern unsigned long Print(char *format, ...); 

struct version_list tftp_var;
tftp_session_t session[MAX_READ_SESSION];
MSG_Q_ID msg_queue_id;

uint32 tftp_server_status;
uint16 tftp_port = DEFAULT_PORT;
uint32 tftp_timeout = MAX_TIMEOUT_SECOND;
uint32 tftp_retry_count = MAX_RETRY;

