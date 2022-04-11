
#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"


file_ops pipe_writer={
	.Read= (void *)useless,
	.Write= pipe_write,
	.Close= close_pipe_writer
}; 

file_ops pipe_reader={
	.Read  = pipe_read,
	.Write = useless,
	.Close = close_pipe_reader
};  

int useless(void* pipecb_t, const char *buf, unsigned int n){
	return -1;
}

int sys_Pipe(pipe_t* pipe)
{
	Fid_t fid[2];
	FCB*  fcb[2];

	if(FCB_reserve(2,fid,fcb)==0)
		return -1;

	PIPE_CB* pipe_cb=(PIPE_CB*)xmalloc(sizeof(PIPE_CB));

	pipe_cb-> reader= fcb[0];
	pipe_cb-> writer= fcb[1];
	pipe -> read = fid[0];
	pipe -> write = fid[1];
	pipe_cb->has_space=COND_INIT;
	pipe_cb->has_data=COND_INIT;
	pipe_cb->w_position=0;
	pipe_cb->r_position=0;
	fcb[0]->streamobj=pipe_cb;
	fcb[1]->streamobj=pipe_cb;
	fcb[1]->streamfunc=&pipe_writer;
	fcb[0]->streamfunc=&pipe_reader;


	return 0;
}


int pipe_write(void* pipecb_t, const char *buf, unsigned int n){
	PIPE_CB* pipe=(PIPE_CB *)pipecb_t;
	if(pipe==NULL||pipe->reader==NULL||pipe->writer==NULL){
		return -1;
	}

	int bytes_to_write=PIPE_BUFFER_SIZE-(pipe->w_position - pipe -> r_position);
	int bytes_to_actually_write;

	while(bytes_to_write==0){
		kernel_broadcast(&pipe->has_data);
		kernel_wait(&pipe-> has_space,SCHED_PIPE);	
		bytes_to_write=PIPE_BUFFER_SIZE-(pipe->w_position - pipe -> r_position);
	}

	if(n<=bytes_to_write){
		bytes_to_actually_write=n;
	}else{
		bytes_to_actually_write=bytes_to_write;
	}

for(int i=0; i<bytes_to_actually_write;++i){
		pipe->BUFFER[pipe->w_position% PIPE_BUFFER_SIZE]=buf[i];
		pipe->w_position++;
	}

	kernel_broadcast(&pipe->has_data);
	return bytes_to_actually_write;
}


int pipe_read(void* pipecb_t, char *buf, unsigned int n){
	
	PIPE_CB* pipe=(PIPE_CB*) pipecb_t;

	if(pipe==NULL || pipe->reader==NULL)
		return -1;

	int bytes_to_read=pipe->w_position - pipe -> r_position;

	int bytes_to_actually_read=0;
	//test
	if(pipe->writer==NULL && bytes_to_read==0){
		return 0;
	}
	if(pipe->writer==NULL){
		 if(bytes_to_read>=0){
		 	if(n<=bytes_to_read){
				bytes_to_actually_read=n;
			}else{
				bytes_to_actually_read=bytes_to_read;
			}
			for(int i=0; i<bytes_to_actually_read;i++){
				buf[i] = pipe->BUFFER[pipe->r_position% PIPE_BUFFER_SIZE];
				pipe->r_position++;
			}
		 }
	 	return bytes_to_actually_read;
	}

	while(bytes_to_read==0){
		kernel_broadcast(&pipe->has_space);
		kernel_wait(&pipe-> has_data,SCHED_PIPE);	
		bytes_to_read=pipe->w_position - pipe -> r_position;
	}
	if(n<=bytes_to_read){
		bytes_to_actually_read=n;
	}else{
		bytes_to_actually_read=bytes_to_read;
	}
	for(int i=0;i<bytes_to_actually_read;++i){
		buf[i] = pipe->BUFFER[pipe->r_position% PIPE_BUFFER_SIZE];
		pipe->r_position++;
	}

	kernel_broadcast(&pipe->has_space);
	return bytes_to_actually_read;
}

int close_pipe_writer(void* pipecb_t){

	PIPE_CB* pipe=(PIPE_CB*)pipecb_t;
	if(pipe==NULL){
		return -1;
	}
	pipe->writer=NULL;
	//pipe->w_position=-1;
	
	if(pipe->reader==NULL){
		free(pipe-> reader);
		free(pipe-> writer);
		free(pipe);

	}
	return 0;
}

int close_pipe_reader(void* pipecb_t){

	PIPE_CB* pipe=(PIPE_CB*)pipecb_t;
	if(pipe==NULL){
		return -1;
	}
	pipe->reader=NULL;
	//pipe->r_position=-1;
	free(pipe-> reader);
	if(pipe->writer==NULL){
		
		free(pipe-> reader);
		free(pipe-> writer);
		free(pipe);
	}
return 0;
}