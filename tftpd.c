#include "tftpd.h"
#include "tftpd_task.c"
#include "tftpd_cmd.c"


/***************************** Incoming request handler task********************************************/
	
void tftd_incoming_request_handler(void)
{
	
	struct sockaddr_in server_addr, client_addr;
	TASK_ID task_id;
	uint32 iterator, session_id, socket_fd, rv, buffer_size, duplicate_req_check, msg_buf[4];
	uint32 session_id_arr[4], error_buffer_size;
	size_t client_addr_size;
	uint8 buffer_data[MAX_TFTP_PACKET_SIZE], error_buffer[TFTP_MAX_ERROR_BUFFER_SIZE];

	tftpdhdr_rrq_wrq_t *tftp_buffer = (tftpdhdr_rrq_wrq_t *) buffer_data;
	client_addr_size = sizeof(client_addr);
	tftp_server_status = TFTP_SERVER_DISABLE;
	socket_fd = 0;
	iterator = 0;
	duplicate_req_check = NO_DUPLICATE_REQUEST;
	
	//Create Message Queue
	msg_queue_id = sys_msgq_create(MSG_QUEUE_SIZE, 0);
	if(msg_queue_id == NULL)
	{
		syslog(LOG_ERR,"Error to Create Message Queue in tftpd.\n");
		return ;
	}
	
    while(1) 
	{   
		session_id = 0;

		rv = sys_msgq_receive(msg_queue_id, (uint32 *)&msg_buf, SYS_WAIT_FOREVER);
		if(rv < 0)
		{
			syslog(LOG_WARNING,"Message Receive Failed from Message Queue in tftpd.\n");
		}
		
		switch(msg_buf[0])
		{
			
			case TFTP_SERVER_DISABLE:
				
				if(tftp_server_status == TFTP_SERVER_DISABLE)
					break;
				
				tftp_server_status = TFTP_SERVER_DISABLE;
				
				//close socket
				socket_unregister(socket_fd);
				rv = so_close(socket_fd);
				if(rv != 0)
					syslog(LOG_ERR,"Error to close socket in tftpd.\n");
				break;
				
			case TFTP_SERVER_ENABLE:
				
				if(tftp_server_status == TFTP_SERVER_ENABLE)
					break;

				tftp_server_status = TFTP_SERVER_ENABLE;
				
				// Create a UDP socket
				socket_fd = so_socket(AF_INET, SOCK_DGRAM, 0);
				if (socket_fd < 0)
				{
					syslog(LOG_ERR,"Error to create socket in tftpd.\n");
					continue;
				}

				// Set up the server address structure
				server_addr.sin_family = AF_INET;
				server_addr.sin_port = htons(tftp_port);
				server_addr.sin_addr.s_addr = INADDR_ANY;

				// Bind the socket to the server address
				rv = so_bind(socket_fd, (struct soaddr *)&server_addr, sizeof(server_addr));		
				if (rv < 0) 		
				{
					syslog(LOG_ERR,"Error to bind socket in tftpd.\n");
					
					//so_close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket in tftpd.\n");

					continue ;
				}
				
				//register the socket with message queue
				rv = socket_register(socket_fd, (ULONG)msg_queue_id, 0);				
				if(rv < 0)				
				{
					syslog(LOG_ERR,"Error to register socket with message queue in tftpd.\n");
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket in tftpd.\n");
					
					continue;
				}

				break;
			
			case TFTP_SERVER_PORT_CHANGE:
			
				if(tftp_server_status == TFTP_SERVER_DISABLE)
					break;
				
				//close socket
				socket_unregister(socket_fd);
				rv = so_close(socket_fd);
				if(rv != 0)
					syslog(LOG_ERR,"Error to close socket in tftpd.\n");
				
				// Create a UDP socket
				socket_fd = so_socket(AF_INET, SOCK_DGRAM, 0);
				if (socket_fd < 0)
				{
					syslog(LOG_ERR,"Error to create socket in tftpd.\n");
					continue;
				}

				// Set up the server address structure
				server_addr.sin_family = AF_INET;
				server_addr.sin_port = htons(tftp_port);
				server_addr.sin_addr.s_addr = INADDR_ANY;

				// Bind the socket to the server address
				rv = so_bind(socket_fd, (struct soaddr *)&server_addr, sizeof(server_addr));		
				if (rv < 0) 		
				{
					syslog(LOG_ERR,"Error to bind socket in tftpd.\n");
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket in tftpd.\n");
					continue;
				}
				
				//Register the socket with message queue
				rv = socket_register(socket_fd, (ULONG)msg_queue_id, 0);				
				if(rv < 0)				
				{
					syslog(LOG_ERR,"Error to register socket with message queue in tftpd.\n");
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket in tftpd.\n");

					continue;
				}

				break;
				
			case TFTP_SERVER_PORT_DEFAULT:
				
				if(tftp_server_status == TFTP_SERVER_DISABLE)
					break;
				
				//close socket
				socket_unregister(socket_fd);
				rv = so_close(socket_fd);
				if(rv != 0)
					syslog(LOG_ERR,"Error to close socket in tftpd.\n");
				
				// Create a UDP socket
				socket_fd = so_socket(AF_INET, SOCK_DGRAM, 0);
				if (socket_fd < 0)
				{
					syslog(LOG_ERR,"Error to create socket in tftpd.\n");
					continue;
				}

				// Set up the server address structure
				server_addr.sin_family = AF_INET;
				server_addr.sin_port = htons(tftp_port);
				server_addr.sin_addr.s_addr = INADDR_ANY;

				// Bind the socket to the server address
				rv = so_bind(socket_fd, (struct soaddr *)&server_addr, sizeof(server_addr));		
				if (rv < 0) 		
				{
					syslog(LOG_ERR,"Error to bind socket in tftpd.\n");
					
					//so_close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket in tftpd.\n");
					
					continue;
				}
				
				//Bind the socket with message queue
				rv = socket_register(socket_fd, (ULONG)msg_queue_id, 0);				
				if(rv < 0)				
				{
					syslog(LOG_ERR,"Error to register socket with message queue in tftpd.\n");
					
					//so_close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket in tftpd.\n");

					continue;
				}
				break;
			
			case SOCKET_DATARCVD:
				
				memset(&buffer_data, 0, sizeof(buffer_data));
				rv = so_recvfrom(socket_fd, buffer_data, sizeof(buffer_data), 0, (struct soaddr *)&client_addr, &client_addr_size);
				if(rv < 0)
					syslog(LOG_WARNING,"Failed to receive request from socket in tftpd.\n");
					
				//Handle multiple request from same port same address
				while(iterator < MAX_READ_SESSION)
				{
					
					if(session[iterator].current_status == ACTIVE && client_addr.sin_port == session[iterator].client_address.sin_port && client_addr.sin_addr.s_addr == session[iterator].client_address.sin_addr.s_addr)
						duplicate_req_check = DUPLICATE_REQUEST;
					iterator = iterator + 1;
				}
				
				//Check Duplicate Request
				if( duplicate_req_check == DUPLICATE_REQUEST)
					break;
					
				//Handle Read Request
				if(ntohs(tftp_buffer->tftp_op_code) == TFTP_OP_RRQ )			
				{
					//If Writing session already running
					if(session[session_id].current_status == WRITING)
					{
						memset( &error_buffer, 0, sizeof(error_buffer));
					
						//Send Error to client that server busy
						tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
						tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
						tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_NOT_DEFINED);
						strcpy(tftphdr_err_p->error_msg, "Server Busy, Try agin Later.....");			
						error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
								
						rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
						if(rv < 0)
							syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
						
						break;
					}
					
					while( session_id <= MAX_READ_SESSION)
					{
						if(session_id == MAX_READ_SESSION)
						{	
							memset( &error_buffer, 0, sizeof(error_buffer));
					
							//Send Error to client that server busy
							tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
							tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
							tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_NOT_DEFINED);
							strcpy(tftphdr_err_p->error_msg, "Server Busy, Try agin Later.....");			
							error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
									
							rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
							if(rv < 0)
								syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
							break;
						}
						else if(session[session_id].is_active == NOT_ACTIVE)
						{
							if(strlen(buffer_data + 2) >= TFTP_MAX_FILENAME_LEN)
							{
								memset(&error_buffer, 0, sizeof(error_buffer));
					
								//Send Error To client that filename length out of range
								tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
								tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
								tftphdr_err_p->tftp_err_code = htons( TFTP_ERROR_ILLEGAL_OPERATION);
								strcpy(tftphdr_err_p->error_msg, "FileName length out of range, try valid range <1 - 254>");
								error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
								
								rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
								if(rv < 0)
									syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
								break;	
							}
							
							session_id_arr[0] = session_id;
							session[session_id].client_address = client_addr;
							memcpy(session[session_id].buffer, buffer_data, sizeof(buffer_data));
							
							task_id = sys_task_spawn("RRQT",128, 0, MAX_STACK_SIZE, (FUNCPTR)read_request_handler, session_id_arr, 1);
							if(task_id == NULL)
								syslog(LOG_ERR,"Failed to create new session for read request in tftpd.\n");
							else
								session[session_id].is_active = ACTIVE;
							
							break;
						
						}
						session_id = session_id + 1;
					}
					
				}
				else if(ntohs(tftp_buffer->tftp_op_code) == TFTP_OP_WRQ )					//Handle Write Request
				{
					if(strlen(buffer_data + 2) >= TFTP_MAX_FILENAME_LEN)
					{
						memset(&error_buffer, 0, sizeof(error_buffer));
					
						//Send Error To client that filename length out of range
						tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
						tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
						tftphdr_err_p->tftp_err_code = htons( TFTP_ERROR_ILLEGAL_OPERATION);
						strcpy(tftphdr_err_p->error_msg, "FileName length out of range, try valid range <1 - 254>");
						error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
								
						rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
						if(rv < 0)
							syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
						break;	
					}
					
					while(session_id < MAX_READ_SESSION)
					{
						//If any read session running then break
						if(session[session_id].is_active == ACTIVE)
							break;
						
						session_id = session_id + 1;
					}
					
					//if all read session inactive
					if(session_id == MAX_READ_SESSION)
					{
						session_id = 0;
						session_id_arr[0] = session_id;
						session[session_id].client_address = client_addr;
						memcpy(session[session_id].buffer, buffer_data, sizeof(buffer_data));
						task_id = sys_task_spawn("WRQT",128,0, MAX_STACK_SIZE, (FUNCPTR)write_request_handler, session_id_arr, 1);
						if(task_id == NULL)
						{
							syslog(LOG_ERR,"Failed to create new session for read request in tftpd.\n");
						}
						else
						{
							session[session_id].is_active = ACTIVE;
						}
					}
					else
					{
						//Send Error to client that server busy
						memset(&error_buffer, 0, sizeof(error_buffer));
			
						//Send Error To client
						tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
						tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
						tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_NOT_DEFINED);
						strcpy(tftphdr_err_p->error_msg, "Server Busy, Try agin Later.....");
						
						error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
						
						rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
						if(rv < 0)
							syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
					
					}
					
				}
				else
				{
					//Invalid Request received
					memset(&error_buffer, 0, sizeof(error_buffer));
		
					//Send Error To client
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
					strcpy(tftphdr_err_p->error_msg, "Invalid Request Received.");
					
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
					if(rv < 0)
							syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
				}
				break;

			default:
				//Send Error to client that server busy
				memset(&error_buffer, 0, sizeof(error_buffer));
			
				//Send Error To client
				tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
				tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
				tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
				strcpy(tftphdr_err_p->error_msg, "Invalid Requeste Received.");
						
				error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
						
				rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_addr, client_addr_size);
				if(rv < 0)
					syslog(LOG_ERR,"Failed to send error packet to socket in tftpd.\n");
				break;
		}
		
	}
	
    rv = so_close(socket_fd);
	if(rv != 0)
		syslog(LOG_ERR,"Error to close socket in tftpd.\n");

	return ;
}
	
	
void tftpd_init(void)
{
	TASK_ID task_id;
	
	tftp_var.module_type = MODULE_TYPE_TFTPD;
	tftp_var.version = 1;
	tftp_var.next = NULL;
	strcpy(tftp_var.module_name, "tftpd");
	strcpy(tftp_var.module_description, "tftpd simple server");
	
	register_module_version(&tftp_var);
	interface_set_showrunning_service(MODULE_TYPE_TFTPD, (FUNCPTR)show_tftp_server_running);
	tftp_register_cmds();
	
	task_id = sys_task_spawn("MYSR",128,0,MAX_STACK_SIZE, (FUNCPTR)tftd_incoming_request_handler, 0, 0);
	if(task_id == NULL)
		syslog(LOG_ERR,"Failed to create tftp task for handle incoming  wrq/rrq in tftpd.\n");
	
	return ;
}
