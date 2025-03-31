#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

socket_cb* PORT_MAP[MAX_PORT + 1] = {NULL};


int socket_read(void* socketcb_t, char *buf, unsigned int n){

	socket_cb* socket = (socket_cb*)socketcb_t;

	return pipe_read(socket->peer.read_pipe, buf, n);
}

int socket_write(void* socketcb_t, const char *buf, unsigned int n){
	socket_cb* socket = (socket_cb*)socketcb_t;
	
	return pipe_write(socket->peer.write_pipe, buf, n);
}

int socket_close(void* socketcb_t) {
    if (socketcb_t == NULL) {
        return -1;  // Check for NULL pointer
    }

    socket_cb* socket = (socket_cb*)socketcb_t;

    switch (socket->type) {
        case SOCKET_UNBOUND:
        		if(socket->refcount == 0){
            		free(socket);
        	}
            break;

        case SOCKET_LISTENER:
            	PORT_MAP[socket->port] = NULL;
            	kernel_broadcast(& socket->listener.req_available);
            	if(socket->refcount == 0){
            		free(socket);
        		}
            break;

        case SOCKET_PEER:
                pipe_writer_close(socket->peer.write_pipe);
                pipe_reader_close(socket->peer.read_pipe);
                if(socket->refcount == 0 && socket != NULL){
            		free(socket);
        		}
            break;

        default:
            return -1;  // Invalid socket type
    }

    return 0;
}


static file_ops socket_ops ={
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

Fid_t sys_Socket(port_t port)
{
	Fid_t fid;
	FCB* fcb;

	if(port < 0 || port > MAX_PORT){
		return NOFILE;
	}	

	if(FCB_reserve(1,&fid,&fcb) == 1){
		socket_cb* socket = (socket_cb*)xmalloc(sizeof(socket_cb));
		socket->fid = fid;
		socket->fcb = fcb;
		socket->type = SOCKET_UNBOUND;
		socket->port = port;

		socket->refcount = 0;

		fcb->streamfunc = &socket_ops;
		fcb->streamobj = socket;

		return fid;
	}
	else{
		return NOFILE;
	}
	
}

int sys_Listen(Fid_t sock)
{
	FCB* fcb = get_fcb(sock);

	if((fcb == NULL) || (fcb->streamfunc != &socket_ops)){
		return -1;
	}

	socket_cb* socket = fcb->streamobj;

	//Check if the socket is not bound to a port
	if((socket->port == NOPORT) || (socket->port >= MAX_PORT)) {
		return -1;
	}

	//Check if the port bound to the socket is occupied by another listener
	if(PORT_MAP[socket->port] != NULL){
		return -1;
	}


	//Check if the socket has already initialized as a listener
	if(socket->type == SOCKET_LISTENER){
		return -1;
	}

	PORT_MAP[socket->port] = socket;
	socket->type = SOCKET_LISTENER;
	socket->listener.req_available = COND_INIT;
	rlnode_init(&socket->listener.queue,NULL);


	return 0;
}

void decrease_refcount(socket_cb* socket){
	socket->refcount --;
	return;
}

void increase_refcount(socket_cb* socket){
	socket->refcount ++;
	return;
}

Fid_t sys_Accept(Fid_t lsock)
{
	FCB* fcb = get_fcb(lsock);
	

	if(fcb == NULL || fcb->streamfunc != &socket_ops){
		return NOFILE;
	}

	socket_cb* socket_cb1 = fcb->streamobj;

	if(socket_cb1 == NULL || socket_cb1->type != SOCKET_LISTENER){
		return NOFILE;
	}

	increase_refcount(socket_cb1);

	while(is_rlist_empty(&socket_cb1->listener.queue) && PORT_MAP[socket_cb1->port]!=NULL){
		kernel_wait(&socket_cb1->listener.req_available,SCHED_PIPE);
	}

	if(PORT_MAP[socket_cb1->port] == NULL){
		return NOFILE;
	}


	request *first_connection_req = rlist_pop_front(&socket_cb1->listener.queue)->obj;

	/*if(first_connection_req == NULL){
		return NOFILE;
	}*/

	first_connection_req->admitted = 1;

	socket_cb* socket_cb2 = first_connection_req->peer_s;

	Fid_t file_id = sys_Socket(socket_cb1->port);


	if (file_id < 0 || file_id > MAX_FILEID){
		return NOFILE;
	}
	
	FCB* fcb3 = get_fcb(file_id);

	if(fcb3 == NULL){
		return NOFILE;
	}


	socket_cb* socket_cb3 = fcb3->streamobj;

	socket_cb2->peer.peer = socket_cb2;
	socket_cb3->peer.peer = socket_cb3;

	socket_cb2->type = SOCKET_PEER;
	socket_cb3->type = SOCKET_PEER;

	pipe_cb *pipe_cb1 = (pipe_cb*)xmalloc(sizeof(pipe_cb));
	pipe_cb *pipe_cb2 = (pipe_cb*)xmalloc(sizeof(pipe_cb));


	pipe_cb1->reader = socket_cb2->fcb;
	pipe_cb1->writer = fcb3;
	pipe_cb1->has_data = COND_INIT;
	pipe_cb1->has_space = COND_INIT;
	pipe_cb1->w_position = 0;
	pipe_cb1->r_position = 0;
	pipe_cb1->data_size = 0;

	pipe_cb2->reader = fcb3;
	pipe_cb2->writer = socket_cb2->fcb;
	pipe_cb2->has_data = COND_INIT;
	pipe_cb2->has_space = COND_INIT;
	pipe_cb2->w_position = 0;
	pipe_cb2->r_position = 0;
	pipe_cb2->data_size = 0;

	socket_cb2->peer.read_pipe = pipe_cb1;
	socket_cb2->peer.write_pipe = pipe_cb2;

	socket_cb3->peer.read_pipe = pipe_cb2;
	socket_cb3->peer.write_pipe = pipe_cb1;

	kernel_signal(&first_connection_req->connected_cv);

	decrease_refcount(socket_cb1);

	return file_id;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	FCB* fcb = get_fcb(sock);

	if(fcb == NULL || fcb->streamfunc != &socket_ops){
		return -1;
	}

	socket_cb* socket = fcb->streamobj;

	//the given port is illegal
	if(port <= 0 || port >= MAX_PORT || PORT_MAP[port] == NULL){
		return -1;
	}

	//the port does not have a listening socket bount to it 
	if(PORT_MAP[port]->type != SOCKET_LISTENER){
		return -1;
	}

	if(socket->type != SOCKET_UNBOUND){
		return -1;
	}

	increase_refcount(socket);

	request *req = (request*)xmalloc(sizeof(request));
	req->peer_s = socket;
	req->connected_cv = COND_INIT;
	req->admitted = 0;

	
	rlnode_init(&req->queue_node,req);
	rlist_push_back(&PORT_MAP[port]->listener.queue,&req->queue_node);

	kernel_signal(&PORT_MAP[port]->listener.req_available);

	while(req->admitted != 1){
		int result = kernel_timedwait(&req->connected_cv,SCHED_PIPE,timeout*1000);

		if(result == 0){
			break;
		}
	}

	decrease_refcount(socket);

	if(req->admitted != 1){
		return -1;
	}

	return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{

	FCB* fcb = get_fcb(sock);

	if(fcb == NULL || fcb->streamfunc != &socket_ops){
		return -1;
	}

	socket_cb* socket = fcb->streamobj;

	if(socket->type != SOCKET_PEER){
		return -1;
	}

	switch(how)
	{
		case SHUTDOWN_READ:
			pipe_reader_close(socket->peer.read_pipe);
			break;

		case SHUTDOWN_WRITE:
			pipe_writer_close(socket->peer.write_pipe);
			break;

		case SHUTDOWN_BOTH:
			pipe_writer_close(socket->peer.write_pipe);
			pipe_reader_close(socket->peer.read_pipe);
			break;

		default:
			break;	 
	}

	return 0;
}

