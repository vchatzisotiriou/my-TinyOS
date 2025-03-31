
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

int is_empty(int data_size){
	if(data_size == 0){
		return 1;
	}
	return 0;
}

int is_full(int data_size){
	if(data_size%(PIPE_BUFFER_SIZE-1)){
		return 1;
	}
	return 0;
}

int pipe_write(void* pipecb_t, const char *buf, unsigned int n){

 	pipe_cb* pipe = (pipe_cb*) pipecb_t;

   	uint count = 0;

  	if((pipe->writer == NULL) || (pipe->reader == NULL)){
    	return -1;
   	}

  	while (is_full(pipe->data_size) && (pipe->reader == NULL)) {   
    	kernel_wait(&pipe->has_space, SCHED_PIPE); //koimise ton writer
  	}

  	while(count < n){
    	pipe->BUFFER[pipe->w_position] = buf[count];
		count ++;
		pipe->data_size++;
		pipe->w_position = (pipe->w_position + 1) % PIPE_BUFFER_SIZE;
 	}
  
  	kernel_broadcast(&pipe->has_data);
  	return count;
}



int pipe_read(void* pipecb_t, char *buf,unsigned int n){
	pipe_cb* pipe = (pipe_cb*)pipecb_t;

	uint count = 0;

	if((pipe->writer == NULL) && is_empty(pipe->data_size)){
		return 0;
	}

	if(pipe->reader == NULL){
		return -1;
	}

	
	while(is_empty(pipe->data_size) && pipe->writer != NULL){
			kernel_wait(&pipe->has_data,SCHED_PIPE);
	}

	while(count < n && !is_empty(pipe->data_size)){

		buf[count]= pipe->BUFFER[pipe->r_position];
		pipe->data_size --;
		count++;
		pipe->r_position = (pipe->r_position+1)%PIPE_BUFFER_SIZE;
	}

	kernel_broadcast(&pipe->has_space);
	return count;
}



int pipe_writer_close(void* pipecb_t){
	pipe_cb* pipe = (pipe_cb*)pipecb_t;

	if(pipe == NULL){
		return -1;
	}

	pipe->writer = NULL;

	if(pipe->reader != NULL){
		kernel_broadcast(&(pipe->has_data));
	}
	else{
		free(pipe);
	}
	return 0;
}

int pipe_reader_close(void* pipecb_t){
	pipe_cb* pipe = (pipe_cb*)pipecb_t;

	if(pipe == NULL){
		return -1;
	}

	pipe->reader = NULL;

	if(pipe->writer != NULL){
		kernel_broadcast(&(pipe->has_space))	;
	}
	else{
		free(pipe);
	}
	return 0;
}

int read_error(void* pipecb_t, char *buf,unsigned int n){
	return -1;
}

int write_error(void* pipecb_t, const char *buf, unsigned int n){
	return -1;
}

static file_ops read_fops ={
	.Read = pipe_read,
	.Write = write_error,
	.Close = pipe_reader_close
};

static file_ops write_fops = {
	.Write = pipe_write,
	.Read = read_error,
	.Close = pipe_writer_close
};


int sys_Pipe(pipe_t* pipe)
{
	Fid_t fid[2];
	FCB* fcb[2];

	int reserve_check = FCB_reserve(2,fid,fcb);

	if(reserve_check != 0){
		pipe_cb *pipe_control_block = (pipe_cb*) xmalloc(sizeof(pipe_cb));
		pipe->read = fid[0];
		pipe->write = fid[1];
		pipe_control_block->r_position = 0;
		pipe_control_block->w_position = 0;
		pipe_control_block->reader = fcb[0];
		pipe_control_block->writer = fcb[1];
		pipe_control_block->has_space = COND_INIT;
		pipe_control_block->has_data = COND_INIT;
		pipe_control_block->data_size = 0;
		fcb[0]->streamobj = pipe_control_block;
		fcb[1]->streamobj = pipe_control_block;
		fcb[0]->streamfunc = &read_fops;
		fcb[1]->streamfunc = &write_fops; 
		return 0;
	}
	else{
		return -1;
	}
}