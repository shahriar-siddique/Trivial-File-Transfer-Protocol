

/*****************************Read request handler task********************************************/

void read_request_handler(uint32 session_id )
{	
	FCB_POINT * fid = NULL;
	MSG_Q_ID msgq_id;
	TIMER_USER_DATA timer_ud;
	struct sockaddr_in server_addr;
	struct soaddr_in client_address;
	tftpdhdr_ack_t tftphdr_ack;
	
	uint32 socket_fd, time_rv, rv, buffer_size, retry_number, random_port, offset, timer_id, msg_buf[4];
	uint32 max_option_length, max_tftp_buffer_size, option_block_size, file_buffer_offset, file_read_size, error_buffer_size;
	uint32 file_buffer_size;
	size_t client_addr_size;
	uint16 block_number;
	uint8 initial_buffer_data[MAX_TFTP_PACKET_SIZE];
	uint8 ack_received;
	
	uint8 file_name[TFTP_MAX_FILENAME_LEN];
	uint8 tftp_mode[TFTP_MAX_MODE_SIZE_LEN];
	uint8 option1[TFTP_MAX_OPTION_LEN];
	uint8 block_size_string[TFTP_MAX_FILE_SIZE_LEN];
	uint8 error_buffer[TFTP_MAX_ERROR_BUFFER_SIZE];
	
	retry_number = 1;
	ack_received = 0;
	offset = 0;
	max_option_length = 0;
	block_number = 1;
	max_tftp_buffer_size = MAX_TFTP_PACKET_SIZE;
	memcpy(&client_address, &session[session_id].client_address, sizeof(session[session_id].client_address));
	random_port = rand()% MAX_PORT_NUMBER;
	
	memcpy(initial_buffer_data, session[session_id].buffer, sizeof(initial_buffer_data));
	
	session[session_id].current_status = READING;
	session[session_id].source_port = random_port;
	session[session_id].client_port = client_address.sin_port;
	session[session_id].transfer_block = 0;
	session[session_id].block_size = MAX_BUFFER_SIZE;
	client_addr_size = sizeof(client_address);
	tftpdhdr_rrq_wrq_t *tftphdr_rrq_p = (tftpdhdr_rrq_wrq_t *) initial_buffer_data;
	
	// Create Message Queue
	msgq_id = sys_msgq_create(MSG_QUEUE_SIZE, 0);
	if(msgq_id == NULL)
	{
		syslog(LOG_ERR,"Error to create Massage Queue for read_request in tftpd_task.\n");
		return ;
	}

    // Create a UDP socket
    socket_fd = so_socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) 
	{
        syslog(LOG_ERR,"Error to socket for read_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		
		//Delete MSGQ
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");;
        return ;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(random_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

	// Bind the socket to the server address
	rv = so_bind(socket_fd, (struct soaddr *)&server_addr, sizeof(server_addr));		
    if (rv < 0) 		
	{
        syslog(LOG_ERR,"Error to bind socket for read_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
		
		//Delete MSGQ
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");

		return ;
	}
	
	//register the socket with message queue
	rv = socket_register(socket_fd, (ULONG) msgq_id, 0);				
	if(rv < 0)				
	{
		syslog(LOG_ERR,"Error to register Massage Queue with socket for read_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
		
		//Delete MSGQ
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to delete Massage Queue for read_request in tftpd_task.\n");
		
		return ;
	}
	
	
	//Timer Configuration
	timer_ud.msg.qid = msgq_id;
	timer_ud.msg.msg_buf[0] = TFTP_CONNECTION_TIMEOUT;
	
	time_rv = sys_add_timer(TIMER_LOOP | TIMER_MSG_METHOD, &timer_ud, &timer_id);
	if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
		syslog(LOG_ERR,"Timer Table Full.\n");
	else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
		syslog(LOG_ERR,"Timer Type Invalid.\n");

	
	//Get filename
	strcpy(file_name, tftphdr_rrq_p->tftp_file_and_mode);
	max_option_length = strlen(file_name) + 1;
	
	//Get tftp_mode
	strcpy(tftp_mode, tftphdr_rrq_p->tftp_file_and_mode + max_option_length);
	max_option_length = max_option_length + strlen(tftp_mode) + 1;
	
	//Get blksize option
	while(1)
	{
		strcpy(option1, tftphdr_rrq_p->tftp_file_and_mode + max_option_length);
					
		if(strcasecmp(option1, "blksize") == 0 || option1[0] == '\0' )
		{
			break;
		}
		max_option_length = max_option_length + strlen(option1) + 1;			
	}
		
	
	//Get block_size string
	max_option_length = max_option_length + strlen(option1) + 1;
	strcpy(block_size_string, tftphdr_rrq_p->tftp_file_and_mode + max_option_length);
	max_option_length = max_option_length + strlen(block_size_string) + 1;
	

	//Add filename to session
	strcpy(session[session_id].filename, file_name);
	
	
	//Handle Option
	if(strcasecmp(option1 , "blksize") == 0)
	{
		max_option_length = 0;
		memset(&initial_buffer_data, 0, sizeof(initial_buffer_data));
		
		tftpdhdr_oack_t *tftphdr_oack = (tftpdhdr_oack_t *) initial_buffer_data;
		tftphdr_oack->tftp_op_code = htons(TFTP_OP_OACK);
		strcpy(tftphdr_oack->tftp_option, option1);
		
		//This server can not receive block size over 4096 and less than 8
		if(atoi(block_size_string) > 4096)
			strcpy(block_size_string, "4096");
		else if(atoi(block_size_string) < 8)
			strcpy(block_size_string, "8");
		
		
		strcpy(tftphdr_oack->tftp_option + strlen(option1) + 1, block_size_string);
		
		max_option_length = strlen(option1) + 1 + strlen(block_size_string) + 1 + 2 ;
		rv = so_sendto(socket_fd, initial_buffer_data, max_option_length, 0,(struct soaddr *) &client_address , client_addr_size);
		if(rv < 0)
			syslog(LOG_WARNING,"Failed to send oack to client for rrq in tftp_task\n");
		
		//Start Timer
		time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
		if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
			syslog(LOG_ERR,"Timer Table is full for rrq in tftp_task\n");
		else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
			syslog(LOG_ERR,"Timer Type is Invalid for rrq in tftp_task\n");	
		
		// Option block size
		option_block_size = atoi(block_size_string);
		
		//Store block size in session structure
		session[session_id].block_size = option_block_size;
			
		
		while( !ack_received )
		{
	
			rv = sys_msgq_receive(msgq_id, (uint32 *)&msg_buf, SYS_WAIT_FOREVER);
			if(rv < 0)
			{
				syslog(LOG_WARNING,"Failed to received message from message queue for rrq in tftp_task\n");
				continue;
			}
			
			rv = so_recvfrom(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address, &client_addr_size);
			if(rv < 0)
			{
				syslog(LOG_WARNING,"Failed to received data from socket_fd for rrq in tftp_task\n");
			}

			
			//Stop Timer
			time_rv = sys_stop_timer(timer_id);
			if(time_rv != 0)
				syslog(LOG_ERR,"Error to stop timer for read request in tftp_task\n");
			else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
				syslog(LOG_ERR,"Timer Type is Invalid for read request in tftp_task\n");
			
			
			switch(msg_buf[0])
			{
				case SOCKET_DATARCVD:
					
					//If Error found
					if(tftphdr_ack.tftp_op_code == htons(TFTP_OP_ERROR))
					{
						//Clear session data
						session[session_id].is_active = NOT_ACTIVE;
						session[session_id].current_status = IDLE;
						session[session_id].source_port = 0;
						session[session_id].client_port = 0;
						session[session_id].transfer_block = 0;
						session[session_id].block_size = 0;
						session[session_id].filename[0] = '\0';						
						
									
						//Delete timer
						time_rv = sys_delete_timer(timer_id);
						if(time_rv != SYS_NOERR)
							syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
						else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
							syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
									
						//close socket
						rv = so_close(socket_fd);
						if(rv != 0)
							syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
									
						//Delete MSGQ
						rv = sys_msgq_delete(msgq_id);
						if(rv != SYS_OK)
							syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");

						return ;	
						
					}
					else if(tftphdr_ack.tftp_op_code == htons(TFTP_OP_ACK) && tftphdr_ack.tftp_block == htons(0))
					{
						max_tftp_buffer_size = option_block_size + TFTP_HEADER_SIZE;
						ack_received = 1;
					}
					else// Send error to client for illegal operation
					{
						memset( &error_buffer, 0, sizeof(error_buffer));
					
						//Send Error To client
						tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
						tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
						tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
						strcpy(tftphdr_err_p->error_msg, "Invalid Request Received");
						
						error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
						
						rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *)&client_address, client_addr_size);
						if(rv < 0)
							syslog(LOG_WARNING,"Failed to send error packet for read_request in tftpd_task.\n");
						
						//Clear session data
						session[session_id].is_active = NOT_ACTIVE;
						session[session_id].current_status = IDLE;
						session[session_id].source_port = 0;
						session[session_id].client_port = 0;
						session[session_id].transfer_block = 0;
						session[session_id].block_size = 0;
						session[session_id].filename[0] = '\0';						
						
				
						//close socket
						rv = so_close(socket_fd);
						if(rv != 0)
							syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
							
						//Delete MSGQ
						rv = sys_msgq_delete(msgq_id);
						if(rv != SYS_OK)
							syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");
							
						//Delete timer
						time_rv = sys_delete_timer(timer_id);
						if(time_rv != SYS_NOERR)
							syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
						else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
							syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
						
						return ;			
					}
				
					break;
				case TFTP_CONNECTION_TIMEOUT:
					
					if(retry_number == tftp_retry_count)
					{
						//Clear session data
						session[session_id].is_active = NOT_ACTIVE;
						session[session_id].current_status = IDLE;
						session[session_id].source_port = 0;
						session[session_id].client_port = 0;
						session[session_id].transfer_block = 0;
						session[session_id].block_size = 0;
						session[session_id].filename[0] = '\0';						
									
						//Delete timer
						time_rv = sys_delete_timer(timer_id);
						if(time_rv != SYS_NOERR)
							syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
						else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
							syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
									
						//close socket
						rv = so_close(socket_fd);
						if(rv != 0)
							syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
									
						//Delete MSGQ
						rv = sys_msgq_delete(msgq_id);
						if(rv != SYS_OK)
							syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");
						
						return ;
					}
					
					//Resend OAck to client
					retry_number = retry_number + 1;
					rv = so_sendto(socket_fd, initial_buffer_data, max_option_length, 0, (struct soaddr *) &client_address , client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send OAck for read_request in tftpd_task.\n");

					break;
					
				default:
					
					break;
			}
			
		}
	}
	else
		max_tftp_buffer_size = MAX_TFTP_PACKET_SIZE;
	
	
	while(1)
	{
		if(FILE_NOERROR == enter_filesys(OPEN_READ))
			break;
		else
			sys_task_delay(1);					
	}

	
	fid = file_open(file_name, "r", NULL);
	if(fid == NULL)
	{
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		session[session_id].filename[0] = '\0';						
		
		memset( &error_buffer, 0, sizeof(error_buffer));
					
		//Send Error To client
		tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
		tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
		tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_FILE_NOT_FOUND);
		strcpy(tftphdr_err_p->error_msg, "File Not Found");			
		error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;

		rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
		if(rv < 0)
			syslog(LOG_WARNING,"Failed to send error packet for read_request in tftpd_task.\n");
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
		
		//Delete MSGQ
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");
		
		//Delete timer
		time_rv = sys_delete_timer(timer_id);
		if(time_rv != SYS_NOERR)
			syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
		else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
			syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
		
		//exit filesystem
		rv = exit_filesys(OPEN_READ);
		if(rv != FILE_NOERROR)
			syslog(LOG_ERR,"Error to Exit File system for read_request in tftpd_task.\n");
		
		return ;
	}
	
	//File read in a buffer size
	if(max_tftp_buffer_size <= 2052)
		file_buffer_size = (max_tftp_buffer_size - TFTP_HEADER_SIZE) * 100;
	else
		file_buffer_size = (max_tftp_buffer_size - TFTP_HEADER_SIZE) * 50;
	uint8 *file_buffer = sys_mem_malloc(file_buffer_size);
	file_read_size =  file_read(fid, file_buffer, file_buffer_size);

	
	//Calculate offset of file pointer
	offset = offset + file_read_size;
	file_buffer_offset = 0;
	
	//file close
	if(file_close(fid) != 0)
		syslog(LOG_WARNING,"Failed to close file for read_request in tftpd_task.\n");


	//exit filesystem
	rv = exit_filesys(OPEN_READ);
	if(rv != FILE_NOERROR)
		syslog(LOG_ERR,"Error to Exit File system for read_request in tftpd_task.\n");
	
	uint8 buffer_data[max_tftp_buffer_size];
	tftpdhdr_data_t *tftphdr_data_p = (tftpdhdr_data_t *) buffer_data;
	
	if((file_read_size - file_buffer_offset) >= (max_tftp_buffer_size - TFTP_HEADER_SIZE))
	{
		memcpy(tftphdr_data_p->tftp_data, (file_buffer + file_buffer_offset), (max_tftp_buffer_size - TFTP_HEADER_SIZE));
		file_buffer_offset = file_buffer_offset +  (max_tftp_buffer_size - TFTP_HEADER_SIZE);
		buffer_size = (max_tftp_buffer_size - TFTP_HEADER_SIZE);
	}
	else
	{
		memcpy(tftphdr_data_p->tftp_data, file_buffer + file_buffer_offset, file_read_size - file_buffer_offset);
		buffer_size = file_read_size - file_buffer_offset;
	}
	
	tftphdr_data_p->tftp_op_code = htons(TFTP_OP_DATA);
	tftphdr_data_p->tftp_block = htons(block_number);
		
	rv = so_sendto(socket_fd, buffer_data, (buffer_size + TFTP_HEADER_SIZE), 0, (struct soaddr *) &client_address, client_addr_size);
	if(rv < 0)
		syslog(LOG_WARNING,"Failed to send buffer data for read_request in tftpd_task.\n");;
	
	//store session block information to session structure
	session[session_id].transfer_block = block_number;
		
	//Start Timer
	time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
	if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
		syslog(LOG_ERR,"Timer Table is full for rrq in tftp_task\n");
	else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
		syslog(LOG_ERR,"Timer Type is Invalid for rrq in tftp_task\n");		
	

	
	
    while(1) 
	{   	
		rv = sys_msgq_receive(msgq_id, (uint32 *)&msg_buf, SYS_WAIT_FOREVER);
		if(rv < 0)
		{
			syslog(LOG_WARNING,"Message receive failed from message queue for read_request in tftpd_task.\n");
			continue;
		}		
		
		
		if(file_read_size == file_buffer_offset)
		{
			sys_task_delay(10);
			
			//File Open
			while(1)
			{
				if(FILE_NOERROR == enter_filesys(OPEN_READ))
				{
					fid = file_open(file_name, "r", NULL);
					if(fid == NULL)
						syslog(LOG_ERR,"Failed to open file for read_request in tftpd_task.\n");
						
					file_seek(fid, offset, FROM_HEAD);
					
					
					memset(file_buffer, 0 , file_buffer_size);
					file_read_size =  file_read(fid, file_buffer, file_buffer_size);
		
					//Calculate offset of file pointer
					offset = offset + file_read_size;
					file_buffer_offset = 0;
					
					//file close
					if(file_close(fid) != 0)
						syslog(LOG_WARNING,"Failed to close file for read_request in tftpd_task.\n");
					//exit filesystem
					rv = exit_filesys(OPEN_READ);
					if(rv != FILE_NOERROR)
						syslog(LOG_ERR,"Error to Exit File system for read_request in tftpd_task.\n");

					break;
				}
				else
					sys_task_delay(1);					
			}	
		}
		
		
		
		switch(msg_buf[0])
		{
			case SOCKET_DATARCVD:
				
				memset(&tftphdr_ack, 0, sizeof(tftphdr_ack));
				
				rv = so_recvfrom(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address, &client_addr_size);
				if(rv < 0)
					syslog(LOG_ERR,"Failed to receive Ack for read_request in tftpd_task.\n");
				
				//Handle if other client send request in same port
				if( (client_address.sin_port != session[session_id].client_address.sin_port) || (client_address.sin_addr.s_addr != session[session_id].client_address.sin_addr.s_addr) )
				{
					memset( &error_buffer, 0, sizeof(error_buffer));
					
					//Send Error To client
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
					strcpy(tftphdr_err_p->error_msg, "Invalid Request Received");
					
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send buffer data for read_request in tftpd_task.\n");
					
					memcpy(&client_address, &session[session_id].client_address, sizeof(session[session_id].client_address));
					break;
				}
				
				
				
				
				if(ntohs(tftphdr_ack.tftp_op_code) == TFTP_OP_ACK)
				{
					retry_number = 1;
					
					//Stop Timer
					time_rv = sys_stop_timer(timer_id);
					if(time_rv != 0)
						syslog(LOG_ERR,"Error to stop timer for read request in tftp_task\n");
					else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
						syslog(LOG_ERR,"Timer Type is Invalid for read request in tftp_task\n");
				
					if(buffer_size < ( max_tftp_buffer_size - TFTP_HEADER_SIZE) && ntohs(tftphdr_ack.tftp_block) == block_number)
					{	
						//Clear session data
						session[session_id].is_active = NOT_ACTIVE;
						session[session_id].current_status = IDLE;
						session[session_id].source_port = 0;
						session[session_id].client_port = 0;
						session[session_id].transfer_block = 0;
						session[session_id].block_size = 0;
						session[session_id].filename[0] = '\0';						
						
						//free memory
						sys_mem_free(file_buffer);
						
						//Delete timer
						time_rv = sys_delete_timer(timer_id);
						if(time_rv != SYS_NOERR)
							syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
						else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
							syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
						
						//close socket
						rv = so_close(socket_fd);
						if(rv != 0)
							syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
						
						//Delete MSGQ
						rv = sys_msgq_delete(msgq_id);
						if(rv != SYS_OK)
							syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");

						return ;
					
					}
					else if(ntohs(tftphdr_ack.tftp_block) != block_number)
					{	
						
						tftphdr_data_p->tftp_op_code = htons(TFTP_OP_DATA);
						tftphdr_data_p->tftp_block = htons(block_number);

						rv = so_sendto(socket_fd, buffer_data, (buffer_size + TFTP_HEADER_SIZE), 0, (struct soaddr *) &client_address, client_addr_size);
						if(rv < 0)
							syslog(LOG_WARNING,"Failed to send buffer data for read_request in tftpd_task.\n");
						
						//Start Timer
						time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
						if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
							syslog(LOG_ERR,"Timer Table is full for rrq in tftp_task\n");
						else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
							syslog(LOG_ERR,"Timer Type is Invalid for rrq in tftp_task\n");	
					}
					else
					{
						block_number = block_number + 1;
						
						if((file_read_size - file_buffer_offset) >= (max_tftp_buffer_size - TFTP_HEADER_SIZE))
						{
							memcpy(tftphdr_data_p->tftp_data, file_buffer + file_buffer_offset, (max_tftp_buffer_size - TFTP_HEADER_SIZE));
							file_buffer_offset = file_buffer_offset + (max_tftp_buffer_size - TFTP_HEADER_SIZE);
							buffer_size = (max_tftp_buffer_size - TFTP_HEADER_SIZE);
							
						}
						else
						{
							memcpy(tftphdr_data_p->tftp_data, file_buffer + file_buffer_offset, file_read_size - file_buffer_offset);
							buffer_size = file_read_size - file_buffer_offset;
							
						}
						
						tftphdr_data_p->tftp_op_code = htons(TFTP_OP_DATA);
						tftphdr_data_p->tftp_block = htons(block_number);
							
						rv = so_sendto(socket_fd, buffer_data, (buffer_size + TFTP_HEADER_SIZE), 0, (struct soaddr *) &client_address, client_addr_size);
						if(rv < 0)
							syslog(LOG_WARNING,"Failed to send buffer data for read_request in tftpd_task.\n");
						
						//store session block information to session structure
						session[session_id].transfer_block = block_number;
						
						//Start Timer
						time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
						if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
							syslog(LOG_ERR,"Timer Table is full for rrq in tftp_task\n");
						else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
							syslog(LOG_ERR,"Timer Type is Invalid for rrq in tftp_task\n");	
					}

				}
				else
				{
					memset( &error_buffer, 0, sizeof(error_buffer));
					
					//Send Error To client
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
					strcpy(tftphdr_err_p->error_msg, "Invalid Request Received");			
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send buffer data for read_request in tftpd_task.\n");
					
					//Clear session data					
					session[session_id].is_active = NOT_ACTIVE;
					session[session_id].current_status = IDLE;
					session[session_id].source_port = 0;
					session[session_id].client_port = 0;
					session[session_id].transfer_block = 0;
					session[session_id].block_size = 0;
					session[session_id].filename[0] = '\0';						
								
					//free memory
					sys_mem_free(file_buffer);
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
						
					//Delete MSGQ
					rv = sys_msgq_delete(msgq_id);
					if(rv != SYS_OK)
						syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");
						
					//Delete timer
					time_rv = sys_delete_timer(timer_id);
					if(time_rv != SYS_NOERR)
						syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
					else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
						syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
					
					return ;
					
				}
				
				break;
				
			case TFTP_CONNECTION_TIMEOUT:
				
				//Stop Timer;
				time_rv = sys_stop_timer(timer_id);
				if(time_rv != 0)
					syslog(LOG_ERR,"Error to stop timer for read request in tftp_task\n");
				else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
					syslog(LOG_ERR,"Timer Type is Invalid for read request in tftp_task\n");
				
				if(retry_number == tftp_retry_count)
				{
					//Clear session data
					session[session_id].is_active = NOT_ACTIVE;
					session[session_id].current_status = IDLE;
					session[session_id].source_port = 0;
					session[session_id].client_port = 0;
					session[session_id].transfer_block = 0;
					session[session_id].block_size = 0;
					session[session_id].filename[0] = '\0';						

					
					memset( &error_buffer, 0, sizeof(error_buffer));
					
					//Send Error To client
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_NOT_DEFINED);
					strcpy(tftphdr_err_p->error_msg, "TFTP Connection timeout....");			
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send buffer data for read_request in tftpd_task.\n");
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	

					//free memory
					sys_mem_free(file_buffer);
						
					//Delete timer
					time_rv = sys_delete_timer(timer_id);
					if(time_rv != SYS_NOERR)
						syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
					else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
						syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
						
					//Delete MSGQ
					rv = sys_msgq_delete(msgq_id);
					if(rv != SYS_OK)
						syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");
					
					return ;
				}

				tftphdr_data_p->tftp_op_code = htons(TFTP_OP_DATA);
				tftphdr_data_p->tftp_block = htons(block_number);
					
				rv = so_sendto(socket_fd, buffer_data, (buffer_size + TFTP_HEADER_SIZE), 0, (struct soaddr *) &client_address, client_addr_size);
				if(rv < 0)
					syslog(LOG_ERR,"Failed to Send buffer data for read_request in tftpd_task.\n");
					
				//Start Timer
				time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
				if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
					syslog(LOG_ERR,"Timer Table is full for rrq in tftp_task\n");
				else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
					syslog(LOG_ERR,"Timer Type is Invalid for rrq in tftp_task\n");	
					
				retry_number = retry_number + 1;
				break;
				
			default:
				
				break;
		}	
		
	}
	
	//Clear session data
	session[session_id].is_active = NOT_ACTIVE;
	session[session_id].current_status = IDLE;
	session[session_id].source_port = 0;
	session[session_id].client_port = 0;
	session[session_id].transfer_block = 0;
	session[session_id].block_size = 0;
	session[session_id].filename[0] = '\0';						
	
	//close socket
	rv = so_close(socket_fd);
	if(rv != 0)
		syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	

	//free memory
	sys_mem_free(file_buffer);
						
	//Delete timer
	time_rv = sys_delete_timer(timer_id);
	if(time_rv != SYS_NOERR)
		syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
	else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
		syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");;
						
	//Delete MSGQ
	rv = sys_msgq_delete(msgq_id);
	if(rv != SYS_OK)
		syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");

	return ;
}



/*****************************Write request handler task********************************************/

void write_request_handler(uint32 session_id )
{	
	FCB_POINT * fid = NULL;
	MSG_Q_ID msgq_id;
	TIMER_USER_DATA timer_ud;
	struct sockaddr_in server_addr;
	
	uint32 socket_fd, rv, buffer_size, time_rv, random_port, timer_id, msg_buf[4];
	uint32 max_tftp_buffer_size, option_offset, retry_number, oack_not_send, error_buffer_size;
	struct soaddr_in client_address;
	size_t client_addr_size;
	uint16 block_number;
	uint8 initial_buffer_data[MAX_TFTP_PACKET_SIZE];
	uint8 error_buffer[TFTP_MAX_ERROR_BUFFER_SIZE];
	
	uint8 file_name[TFTP_MAX_FILENAME_LEN];
	uint8 tftp_mode[TFTP_MAX_MODE_SIZE_LEN];
	uint8 option1[TFTP_MAX_OPTION_LEN];
	uint8 block_size_string[TFTP_MAX_FILE_SIZE_LEN];
	
	memcpy(initial_buffer_data, session[session_id].buffer, sizeof(session[session_id].buffer));
	
	tftpdhdr_ack_t tftphdr_ack;
	tftpdhdr_rrq_wrq_t *tftphdr_wrq_p = (tftpdhdr_rrq_wrq_t *) initial_buffer_data;
	
	buffer_size = 0;
	oack_not_send = 0;
	block_number = 0;
	retry_number = 1;
	memcpy(&client_address, &session[session_id].client_address, sizeof(session[session_id].client_address));
	random_port = rand() % MAX_PORT_NUMBER;
	
	//Clear session data
	session[session_id].current_status = WRITING;
	session[session_id].is_active = ACTIVE;
	session[session_id].source_port = random_port;
	session[session_id].client_port = session[session_id].client_address.sin_port;
	session[session_id].transfer_block = 0;
	session[session_id].block_size = MAX_BUFFER_SIZE;
					
	client_addr_size = sizeof(client_address);

	// Create Message Queue
	msgq_id = sys_msgq_create(MSG_QUEUE_SIZE, 0);
	if(msgq_id == NULL)
		syslog(LOG_ERR,"Error to create message queue for write_request in tftpd_task.\n");

    // Create a UDP socket
    socket_fd = so_socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        syslog(LOG_ERR,"Error to create socket for write_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;		
		
		//Delete MSGQ
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
		
        return ;
    }

    // Set up the server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(random_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

	// Bind the socket to the server address
	rv = so_bind(socket_fd, (struct soaddr *)&server_addr, sizeof(server_addr));		
    if (rv < 0) 		
	{
        syslog(LOG_ERR,"Failed to bind socket for write_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;	
		session[session_id].block_size = 0;		
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
	
		
		//Delete MSGQ
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
		
		return ;
	}
	
	//Register the socket with message queue
	rv = socket_register(socket_fd, (ULONG) msgq_id, 0);				
	if(rv < 0)				
	{
		syslog(LOG_ERR,"Failed to register socket with message queue for write_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		session[session_id].filename[0] = '\0';						
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
		
		//Delete MSG Queue
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
	
		return ;
	}
	
	
	//Timer Configuration
	timer_ud.msg.qid = msgq_id;
	timer_ud.msg.msg_buf[0] = TFTP_CONNECTION_TIMEOUT;
	
	time_rv = sys_add_timer(TIMER_LOOP | TIMER_MSG_METHOD, &timer_ud, &timer_id);
	if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
		syslog(LOG_ERR,"Timer Table Full.\n");
	else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
		syslog(LOG_ERR,"Timer Type Invalid.\n");
	
	


	//Get filename
	strcpy(file_name, tftphdr_wrq_p->tftp_file_and_mode);
	option_offset = strlen(file_name) + 1;
	
	//Get tftp_mode
	strcpy(tftp_mode, tftphdr_wrq_p->tftp_file_and_mode + option_offset);
	option_offset = option_offset + strlen(tftp_mode) + 1;
	
	//Get blksize option
	while(1)
	{
		strcpy(option1, tftphdr_wrq_p->tftp_file_and_mode + option_offset);
					
		if(strcasecmp(option1, "blksize") == 0 || option1[0] == '\0' )
		{
			break;
		}
		option_offset = option_offset + strlen(option1) + 1;			
	}
		
	
	//Get block_size string
	option_offset = option_offset + strlen(option1) + 1;
	strcpy(block_size_string, tftphdr_wrq_p->tftp_file_and_mode + option_offset);
	

	if(strcasecmp(option1, "blksize") == 0)
	{
		if(atoi(block_size_string) > 4096)
			strcpy(block_size_string, "4096");
		else if(atoi(block_size_string) < 8)
			strcpy(block_size_string, "8");
		
		max_tftp_buffer_size = atoi(block_size_string) + TFTP_HEADER_SIZE;
	}
	else
		max_tftp_buffer_size = MAX_BUFFER_SIZE + TFTP_HEADER_SIZE;
	

	//Store block size in session structure
	session[session_id].block_size = atoi(block_size_string);
	
	//Declare buffer size based on option block
	uint8 buffer_data[max_tftp_buffer_size];
	
	//Add filename to sessison
	strcpy(session[session_id].filename, file_name);
	
	
	if(strcasecmp(option1, "blksize") == 0)
	{
		//Send Oack
		memset(&buffer_data, 0, sizeof(buffer_data));
		tftpdhdr_oack_t *tftphdr_oack = (tftpdhdr_oack_t *) buffer_data;
			
		tftphdr_oack->tftp_op_code = htons(TFTP_OP_OACK);
		strcpy(tftphdr_oack->tftp_option, option1);
		strcpy(tftphdr_oack->tftp_option + strlen(option1) + 1, block_size_string);
			
		buffer_size = strlen(option1) + 1 + strlen(block_size_string) + 1 + 2 ;
		rv = so_sendto(socket_fd, buffer_data, buffer_size, 0, (struct soaddr *)&client_address , client_addr_size);
		if(rv < 0)
			syslog(LOG_WARNING,"Failed to send OAck for write_request in tftpd_task.\n");
		else
			oack_not_send = 1;
			
		//Start Timer
		time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
		if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
			syslog(LOG_ERR,"Timer Table is full for write request in tftp_task\n");
		else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
			syslog(LOG_ERR,"Timer Type is Invalid for write request in tftp_task\n");	
	}
	else
	{
		memset(&tftphdr_ack, 0, sizeof(tftphdr_ack));
		tftphdr_ack.tftp_op_code = htons(TFTP_OP_ACK);
		tftphdr_ack.tftp_block = htons(0);

		rv = so_sendto(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address, client_addr_size);
		if(rv < 0)
			syslog(LOG_WARNING,"Failed to send Ack for write_request in tftpd_task.\n");
				
		//Start Timer
		time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
		if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
			syslog(LOG_ERR,"Timer Table is full for write request in tftp_task\n");
		else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
			syslog(LOG_ERR,"Timer Type is Invalid for write request in tftp_task\n");
	}
	

	//Enter Filesystem
	rv =  enter_filesys(OPEN_WRITE);
	if(rv != 0)
	{
		syslog(LOG_ERR,"Failed to enter file system for write_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		session[session_id].filename[0] = '\0';						
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
		
		//Delete MSG Queue
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
		
		//Delete timer
		time_rv = sys_delete_timer(timer_id);
		if(time_rv != SYS_NOERR)
			syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
		else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
			syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
		
		return ;
	}

	if( IsFileExist(file_name) != 0)
	{
		memset( &error_buffer, 0, sizeof(error_buffer));
					
		//Send Error To client
		tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
		tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
		tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_FILE_EXISTS);
		strcpy(tftphdr_err_p->error_msg, "File Already Exists.");			
		error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
		rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
		if(rv < 0)
			syslog(LOG_WARNING,"Failed to send error packet for write_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		session[session_id].filename[0] = '\0';						
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
		
		//Delete MSG Queue
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
		
		//Delete timer
		time_rv = sys_delete_timer(timer_id);
		if(time_rv != SYS_NOERR)
			syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
		else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
			syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
		
		//exit filesystem
		rv = exit_filesys(OPEN_WRITE);
		if(rv != FILE_NOERROR)
			syslog(LOG_ERR,"Error to Exit File system for write_request in tftpd_task.\n");

		return ;	
	}
		
	fid = file_open(file_name, "w", NULL);
	if(fid == NULL)
	{	
		syslog(LOG_ERR,"Failed to open file for write_request in tftpd_task.\n");
		
		//Clear session data
		session[session_id].is_active = NOT_ACTIVE;
		session[session_id].current_status = IDLE;
		session[session_id].source_port = 0;
		session[session_id].client_port = 0;
		session[session_id].transfer_block = 0;
		session[session_id].block_size = 0;
		session[session_id].filename[0] = '\0';						
		
		memset( &error_buffer, 0, sizeof(error_buffer));
					
		//Send Error To client
		tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
		tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
		tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ACCESS_VIOLATION);
		strcpy(tftphdr_err_p->error_msg, "Can not open file.");			
		error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
		
		rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address , client_addr_size);
		if(rv < 0)
			syslog(LOG_WARNING,"Failed to send error packet for write_request in tftpd_task.\n");
		
		//close socket
		rv = so_close(socket_fd);
		if(rv != 0)
			syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
		
		//Delete MSG Queue
		rv = sys_msgq_delete(msgq_id);
		if(rv != SYS_OK)
			syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");

		
		//Delete timer
		time_rv = sys_delete_timer(timer_id);
		if(time_rv != SYS_NOERR)
			syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
		else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
			syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
		
		//exit filesystem
		rv = exit_filesys(OPEN_WRITE);
		if(rv != FILE_NOERROR)
			syslog(LOG_ERR,"Error to Exit File system for write_request in tftpd_task.\n");

		return ;
	}
	
	
	
	
	
    while(1) 
	{
		rv = sys_msgq_receive(msgq_id, (uint32 *)&msg_buf, SYS_WAIT_FOREVER);
		if(rv < 0)
		{
			syslog(LOG_WARNING,"Message receive failed from message queue for write_request in tftpd_task.\n");
			continue;
		}		
		
		
		switch(msg_buf[0])
		{
			case SOCKET_DATARCVD:
			
				memset(&buffer_data, 0, sizeof(buffer_data));
				
				buffer_size = so_recvfrom(socket_fd, buffer_data, sizeof(buffer_data), 0, (struct soaddr *)&client_address, &client_addr_size);
				if(buffer_size < 0)
					syslog(LOG_ERR,"Failed to receive buffer data for write_request in tftpd_task.\n");
			
				
				tftpdhdr_data_t *tftphdr_data_p = (tftpdhdr_data_t *) buffer_data;
				buffer_size = buffer_size - TFTP_HEADER_SIZE;
				oack_not_send = 0;
				
				
				//Handle if other client send request in same port
				if( (client_address.sin_port != session[session_id].client_address.sin_port) || (client_address.sin_addr.s_addr != session[session_id].client_address.sin_addr.s_addr) )
				{
					memset( &error_buffer, 0, sizeof(error_buffer));
							
					//Send Error To client
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
					strcpy(tftphdr_err_p->error_msg, "Invalid Request Received");
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send buffer data for write_request in tftpd_task.\n");
							
					memcpy(&client_address, &session[session_id].client_address, sizeof(session[session_id].client_address));
					break;
				}

				//write data
				if(tftphdr_data_p->tftp_op_code == htons(TFTP_OP_DATA))
				{
					retry_number = 1;
					
					//Stop Timer
					time_rv = sys_stop_timer(timer_id);
					if(time_rv != 0)
						syslog(LOG_ERR,"Error to stop timer for write request in tftp_task\n");
					else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
						syslog(LOG_ERR,"Timer Type is Invalid for write request in tftp_task\n");
					

					if(buffer_size == (max_tftp_buffer_size - TFTP_HEADER_SIZE) && ntohs(tftphdr_data_p->tftp_block - 1) == block_number )
					{
						rv = file_write(fid, tftphdr_data_p->tftp_data, buffer_size);
						if(rv == 0)
						{
							memset( &error_buffer, 0, sizeof(error_buffer));
							
							//Send Error To client that disk full
							tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
							tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
							tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_DISK_FULL);
							strcpy(tftphdr_err_p->error_msg, "Insufficient Storage to write");
							error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
							
							rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
							if(rv < 0)
								syslog(LOG_WARNING,"Failed to send error packet for write request in tftpd_task.\n");
							
							//Clear session data
							session[session_id].is_active = NOT_ACTIVE;
							session[session_id].current_status = IDLE;
							session[session_id].source_port = 0;
							session[session_id].client_port = 0;
							session[session_id].transfer_block = 0;
							session[session_id].block_size = 0;
							session[session_id].filename[0] = '\0';						
							

							//close socket
							rv = so_close(socket_fd);
							if(rv != 0)
								syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
							
							//file close
							if(file_close(fid) != 0)
								syslog(LOG_WARNING,"Failed to close file for write_request in tftpd_task.\n");
							
							//exit filesystem
							rv = exit_filesys(OPEN_WRITE);
							if(rv != FILE_NOERROR)
								syslog(LOG_ERR,"Error to Exit File system for write_request in tftpd_task.\n");
							
							//Delete MSG Queue
							rv = sys_msgq_delete(msgq_id);
							if(rv != SYS_OK)
								syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
							
							
							//Delete timer
							time_rv = sys_delete_timer(timer_id);
							if(time_rv != SYS_NOERR)
								syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
							else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
								syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
							
							return ;
						}
						
						block_number = ntohs(tftphdr_data_p->tftp_block);
						
						//send ack
						memset(&tftphdr_ack, 0, sizeof(tftphdr_ack));
						tftphdr_ack.tftp_op_code = htons(TFTP_OP_ACK);
						tftphdr_ack.tftp_block = tftphdr_data_p->tftp_block;
						
						rv = so_sendto(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address , client_addr_size);
						if(rv < 0)
							syslog(LOG_WARNING,"Failed to send Ack for write_request in tftpd_task.\n");
						
						//store session block information to session structure
						session[session_id].transfer_block = block_number;
						
						//Start Timer
						time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
						if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
							syslog(LOG_ERR,"Timer Table is full for write request in tftp_task\n");
						else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
							syslog(LOG_ERR,"Timer Type is Invalid for write request in tftp_task\n");
						
					}
					else if(ntohs(tftphdr_data_p->tftp_block - 1) != block_number)
					{
						//send ack
						memset(&tftphdr_ack, 0, sizeof(tftphdr_ack));
						tftphdr_ack.tftp_op_code = htons(TFTP_OP_ACK);
						tftphdr_ack.tftp_block = htons(block_number);
						
						rv = so_sendto(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address , client_addr_size);
						if(rv < 0)
							syslog(LOG_WARNING,"Failed to send Ack for write_request in tftpd_task.\n");
						
						//Start Timer
						time_rv = sys_start_timer(timer_id, TIMER_RESOLUTION_S | tftp_timeout);
						if(time_rv == SYS_ERR_TIMER_TABLE_FULL)
							syslog(LOG_ERR,"Timer Table is full for write request in tftp_task\n");
						else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
							syslog(LOG_ERR,"Timer Type is Invalid for write request in tftp_task\n");
						
						
					}
					else
					{
						// This is last packet
						rv = file_write(fid, tftphdr_data_p->tftp_data, buffer_size);
						if(rv == 0 && buffer_size != 0)
						{
							//Clear session data
							session[session_id].is_active = NOT_ACTIVE;
							session[session_id].current_status = IDLE;
							session[session_id].source_port = 0;
							session[session_id].client_port = 0;
							session[session_id].transfer_block = 0;
							session[session_id].block_size = 0;
							session[session_id].filename[0] = '\0';						
							
							//file close
							if(file_close(fid) != 0)
								syslog(LOG_WARNING,"Failed to close file for write_request in tftpd_task.\n");
							
							//close socket
							rv = so_close(socket_fd);
							if(rv != 0)
								syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
							
							//exit filesystem
							rv = exit_filesys(OPEN_WRITE);
							if(rv != FILE_NOERROR)
								syslog(LOG_ERR,"Error to Exit File system for write_request in tftpd_task.\n");
							
							//Delete MSG Queue
							rv = sys_msgq_delete(msgq_id);
							if(rv != SYS_OK)
								syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
	
							//Delete timer
							time_rv = sys_delete_timer(timer_id);
							if(time_rv != SYS_NOERR)
								syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
							else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
								syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
		
							return ;
						}
						
						memset(&tftphdr_ack, 0, sizeof(tftphdr_ack));
						tftphdr_ack.tftp_op_code = htons(TFTP_OP_ACK);
						tftphdr_ack.tftp_block = tftphdr_data_p->tftp_block;
						
						rv = so_sendto(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address , client_addr_size);
						if(rv < 0)
							syslog(LOG_WARNING,"Failed to send Ack for write_request in tftpd_task.\n");
						
						//Clear session data
						session[session_id].is_active = NOT_ACTIVE;
						session[session_id].current_status = IDLE;
						session[session_id].source_port = 0;
						session[session_id].client_port = 0;
						session[session_id].transfer_block = 0;
						session[session_id].block_size = 0;
						session[session_id].filename[0] = '\0';						
						
						//close socket
						rv = so_close(socket_fd);
						if(rv != 0)
							syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
						
						//file close
						if(file_close(fid) != 0)
							syslog(LOG_WARNING,"Failed to close file for write_request in tftpd_task.\n");
						
						//exit filesystem
						rv = exit_filesys(OPEN_WRITE);
						if(rv != FILE_NOERROR)
							syslog(LOG_ERR,"Error to Exit File system for write_request in tftpd_task.\n");

						//Delete MSG Queue
						rv = sys_msgq_delete(msgq_id);
						if(rv != SYS_OK)
							syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");
						
						//Delete timer
						time_rv = sys_delete_timer(timer_id);
						if(time_rv != SYS_NOERR)
							syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
						else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
							syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
						
						return ;
					}		
				
				}
				else
				{		
					memset( &error_buffer, 0, sizeof(error_buffer));
					
					//Send Error To client that invalid request received
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_ILLEGAL_OPERATION);
					strcpy(tftphdr_err_p->error_msg, "Invalid Request Received.");			
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
					
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address , client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send error packet for write_request in tftpd_task.\n");

					//Clear session data
					session[session_id].is_active = NOT_ACTIVE;
					session[session_id].current_status = IDLE;
					session[session_id].source_port = 0;
					session[session_id].client_port = 0;
					session[session_id].transfer_block = 0;
					session[session_id].block_size = 0;
					session[session_id].filename[0] = '\0';								
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket for read_request in tftpd_task.\n");
	
						
					//Delete MSGQ
					rv = sys_msgq_delete(msgq_id);
					if(rv != SYS_OK)
						syslog(LOG_ERR,"Error to Delete Massage Queue for read_request in tftpd_task.\n");
						
					//Delete timer
					time_rv = sys_delete_timer(timer_id);
					if(time_rv != SYS_NOERR)
						syslog(LOG_ERR,"Timer delete failed for read_request in tftpd_task.\n");
					else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
						syslog(LOG_ERR,"Timer type invalid for read_request in tftpd_task.\n");
					
					return ;
				}
				
				break;
				
			case TFTP_CONNECTION_TIMEOUT:
				
				if(retry_number == tftp_retry_count)
				{
					memset( &error_buffer, 0, sizeof(error_buffer));
					
					//Send Error To client that invalid request received
					tftpdhdr_err_t *tftphdr_err_p = (tftpdhdr_err_t *) error_buffer;
					tftphdr_err_p->tftp_op_code = htons(TFTP_OP_ERROR);
					tftphdr_err_p->tftp_err_code = htons(TFTP_ERROR_NOT_DEFINED);
					strcpy(tftphdr_err_p->error_msg, "TFTP Connection Timeout........");			
					error_buffer_size = strlen(tftphdr_err_p->error_msg) + 1 + TFTP_HEADER_SIZE;
								
					rv = so_sendto(socket_fd, error_buffer, error_buffer_size , 0, (struct soaddr *) &client_address, client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send error packet for write_request in tftpd_task.\n");
					
					//Stop Timer
					time_rv = sys_stop_timer(timer_id);
					if(time_rv != 0)
						syslog(LOG_ERR,"Error to stop timer for write request in tftp_task\n");
					else if(time_rv == SYS_ERR_TIMER_TYPE_INVALID)
						syslog(LOG_ERR,"Timer Type is Invalid for write request in tftp_task\n");
					
					//close socket
					rv = so_close(socket_fd);
					if(rv != 0)
						syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
					
					//file close
					if(file_close(fid) != 0)
						syslog(LOG_WARNING,"Failed to close file for write_request in tftpd_task.\n");
					
					//exit filesystem
					rv = exit_filesys(OPEN_WRITE);
					if(rv != FILE_NOERROR)
						syslog(LOG_ERR,"Error to Exit File system for write_request in tftpd_task.\n");
					
					//Clear session data
					session[session_id].is_active = NOT_ACTIVE;
					session[session_id].current_status = IDLE;
					session[session_id].source_port = 0;
					session[session_id].client_port = 0;
					session[session_id].transfer_block = 0;
					session[session_id].block_size = 0;
					session[session_id].filename[0] = '\0';						
					
					//Delete MSG Queue
					rv = sys_msgq_delete(msgq_id);
					if(rv == SYS_OK)
						syslog(LOG_ERR,"Error to Delete Massage Queue for write_request in tftpd_task.\n");

					//Delete timer
					time_rv = sys_delete_timer(timer_id);
					if(time_rv != SYS_NOERR)
						syslog(LOG_ERR,"Timer delete failed for write_request in tftpd_task.\n");
					else if(time_rv == SYS_ERR_TIMER_ID_INVALID)
						syslog(LOG_ERR,"Timer type invalid for write_request in tftpd_task.\n");
			
					return ;
				}
				
				retry_number = retry_number + 1;

				if(oack_not_send == 1)
				{
					rv = so_sendto(socket_fd, buffer_data, buffer_size, 0, (struct soaddr *) &client_address , client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send OAck for write_request in tftpd_task.\n");

				}
				else				
				{
					rv = so_sendto(socket_fd, &tftphdr_ack, sizeof(tftphdr_ack), 0, (struct soaddr *) &client_address , client_addr_size);
					if(rv < 0)
						syslog(LOG_WARNING,"Failed to send Ack for write_request in tftpd_task.\n");
				}
				
				break;
				
			default:
				
				break;
		}
		
	}
	
    //close socket
	rv = so_close(socket_fd);
	if(rv != 0)
		syslog(LOG_ERR,"Error to close socket for write_request in tftpd_task.\n");
	
	return ;
}

