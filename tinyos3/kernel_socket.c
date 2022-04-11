#include "tinyos.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_cc.h"


file_ops sfile_ops={
  .Open=NULL, .Read= socket_read, .Write= socket_write, .Close= socket_close
};

int check_if_first_time = 0;

socket_cb* PORT_MAP[MAX_PORT];

Fid_t sys_Socket(port_t port)
{
	if(port<0||port>MAX_PORT){return NOFILE;}

	FCB* fcb = NULL;
	Fid_t fid = -1;

	int j = FCB_reserve(1, &fid, &fcb);

	if (j==0){return NOFILE;}
	
	if(check_if_first_time==0){

		for(int i=0; i<=MAX_PORT; i++){
			PORT_MAP[i]=NULL;
		}

	}


	
	socket_cb* scb = (socket_cb*)xmalloc(sizeof(socket_cb));
	
	scb->fcb = fcb;
	scb->fid = fid;
	scb->type = SOCKET_UNBOUND;
	scb->port = port;
	scb->refcount = 1;
	(fcb->streamobj) = scb;
	fcb->streamfunc = &sfile_ops;

	check_if_first_time++;
	return fid;

}

int sys_Listen(Fid_t sock)
{
	FCB* fcb = get_fcb(sock);

	if(fcb == NULL){return -1;} //check if fcb is empty

	socket_cb* sc = fcb -> streamobj;

	if(sc ==NULL ){return -1;}
	
	if(PORT_MAP[sc->port] != NULL){return -1;}//check if we have a listener already

	if(sc->port <0 || sc->port > MAX_PORT) {return -1;} //check parameters of socket_cb

	if(sc->type != SOCKET_UNBOUND){return -1;}

	sc->type = SOCKET_LISTENER;
	sc->listener.req_available= COND_INIT;
	rlnode_init(&(sc->listener.queue), NULL);
	
	PORT_MAP[sc -> port] = sc;

	return 0;

} 


Fid_t sys_Accept(Fid_t lsock)
{

	FCB* fcb = get_fcb(lsock);

	if(fcb == NULL){return NOFILE;} //check if fcb is empty

	if(fcb->streamfunc != &sfile_ops){return NOFILE;}

	socket_cb* sc = fcb -> streamobj;

	if(sc == NULL){return NOFILE;}

	if(sc->port <= NOPORT || sc->port >MAX_PORT){return NOFILE;}

	if(sc->type != SOCKET_LISTENER){return NOFILE;}

	if((PORT_MAP[sc->port])->type !=SOCKET_LISTENER){return NOFILE;}

	sc->refcount++;

	while(rlist_len(&sc->listener.queue)==0){	
		
		if(PORT_MAP[sc->port]==NULL){return NOFILE;}	//listener no longer exists

		kernel_wait(&sc->listener.req_available, SCHED_IO);

	}

	connection_request* req= rlist_pop_front(&sc->listener.queue) -> connection_request;

	socket_cb* peer1 =req->peer;
	if(peer1==NULL){return NOFILE;}
	Fid_t q = sys_Socket(peer1->port);
	FCB* q2 = get_fcb(q);
	if(q2==NULL){return NOFILE;}
	socket_cb* peer2 = q2->streamobj;
	
	if(peer2==NULL){return NOFILE;}

	peer1->type = SOCKET_PEER;
	peer2->type = SOCKET_PEER;

	FCB* coms_fcb[2];

	coms_fcb[0] = peer1->fcb;
	coms_fcb[1] = peer2->fcb;

	PIPE_CB* pipe_cb1=(PIPE_CB*)xmalloc(sizeof(PIPE_CB));

	pipe_cb1-> reader= coms_fcb[0];
	pipe_cb1-> writer= coms_fcb[1];
	pipe_cb1->has_space=COND_INIT;
	pipe_cb1->has_data=COND_INIT;
	pipe_cb1->w_position=0;
	pipe_cb1->r_position=0;

	if(pipe_cb1==NULL){return NOFILE;}

	PIPE_CB* pipe_cb2=(PIPE_CB*)xmalloc(sizeof(PIPE_CB));

	pipe_cb2-> reader= coms_fcb[1];
	pipe_cb2-> writer= coms_fcb[0];
	pipe_cb2->has_space=COND_INIT;
	pipe_cb2->has_data=COND_INIT;
	pipe_cb2->w_position=0;
	pipe_cb2->r_position=0;

	if(pipe_cb2==NULL){return NOFILE;}

	peer1->peer.peer = peer2;
	peer1->peer.write = pipe_cb1;
	peer1->peer.read = pipe_cb2;

	peer2->peer.peer = peer1;
	peer2->peer.write = pipe_cb2;
	peer2->peer.read = pipe_cb1;

	req->admited=1;

	kernel_signal(&req->connected_cv);
	sc->refcount --;
	if(sc->refcount == 0){free(sc);}

	return peer2->fid;

}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	FCB* fcb = get_fcb(sock);

	if(fcb == NULL){return -1;}

	socket_cb* sc = fcb -> streamobj;

	if(sc == NULL){return -1;}

	if(port<=0||port>MAX_PORT){return -1;} 

	if(sc -> type!= SOCKET_UNBOUND || PORT_MAP[port]==NULL){return -1;}

	socket_cb* ls = PORT_MAP[port];

	connection_request* req = (connection_request*)xmalloc(sizeof(connection_request));

	req->admited=0;
	req->peer=sc;
	req->connected_cv=COND_INIT;
	rlnode_init(&req->queue_node, req);
	rlist_push_back(&ls->listener.queue, &req->queue_node);

	kernel_signal(&ls->listener.req_available);
	ls->refcount++;
	while(!req->admited){
		int aok = kernel_timedwait(&req->connected_cv, SCHED_IO, timeout);
		if(!aok){return -1;}
	}

	sc->refcount--;
	if(sc->refcount == 0){free(sc);}

	if(!req->admited){return -1;}

	return 0;
	
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{

	FCB* fcb = get_fcb(sock);

	if(fcb==NULL){return -1;}
	
	socket_cb* sc = fcb -> streamobj;

	if(how<1 || how>3){return -1;}

	if(sc->type != SOCKET_PEER){return -1;}

	if( how == SHUTDOWN_READ){

		if(close_pipe_reader(sc->peer.read)==0){
			sc->peer.read=NULL;
			return 0;
		}

		return -1;

	}

	if( how == SHUTDOWN_WRITE){

		if(close_pipe_writer(sc->peer.write)==0){
			sc->peer.write=NULL;
			return 0;
		}

		return -1;
	}

	if( how == SHUTDOWN_BOTH){

		int x = close_pipe_writer(sc->peer.write);
		int y = close_pipe_reader(sc->peer.read);

		if(x==-1||y==-1){return -1;}

		sc->peer.read=NULL;
		sc->peer.write=NULL;
		return 0;
	}

	return -1;
}


int socket_read(void* read,char*buffer , unsigned int size){
	socket_cb* sockcb = (socket_cb*)read;
	if(sockcb == NULL){return -1;}
	
	if(sockcb->type !=SOCKET_PEER||sockcb->peer.peer == NULL){return -1;}

	int x = pipe_read(sockcb -> peer.read,buffer,size);

	return x;
}

int socket_write(void* write,const char*buffer , unsigned int size){
	socket_cb* sockcb = (socket_cb*)write;
	if(sockcb == NULL){return -1;}
	if(sockcb->type !=SOCKET_PEER||sockcb->peer.peer == NULL){return -1;}

	int x = pipe_write(sockcb -> peer.write,buffer ,size);
	return x;

}

int socket_close(void* fid){
	socket_cb* sockcb = (socket_cb*) fid;
	if(sockcb == NULL){return -1;}

	if(sockcb -> type == SOCKET_PEER){

		if(sockcb->peer.peer!=NULL){

			int x =close_pipe_reader(sockcb -> peer.read);
			int y =close_pipe_writer(sockcb -> peer.write);
			if(x!=0 || y!=0){return -1;}
			sockcb->peer.peer =NULL;
		}

	}

	if(sockcb -> type == SOCKET_LISTENER){
		PORT_MAP[sockcb->port]=NULL;
		kernel_broadcast(&sockcb->listener.req_available);
	}

	sockcb -> refcount --;
	if (sockcb -> refcount ==0){free(sockcb);}

	return 0;

}