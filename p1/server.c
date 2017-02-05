#include "cs510.h"
#include "request.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

struct req_queue* q;
pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_empty = PTHREAD_COND_INITIALIZER; //when q is size 0;
pthread_cond_t q_full = PTHREAD_COND_INITIALIZER; //when q is the size of the buffer;

// CS510: Parse the new arguments too
void getargs(int *port, int *threads, int *buffers, int argc, char *argv[])
{
    if (argc != 4) {
	fprintf(stderr, "Usage: %s <port> <threads> <buffers>\n", argv[0]);
	exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    if(threads <= 0) {
    	fprintf(stderr, "Err: Thread argument must be positive");
    	exit(1);
    }
    *buffers = atoi(argv[3]);
    if(buffers <= 0) {
    	fprintf(stderr, "Err: Buffers argument must be positive");
    	exit(1);
    }

}

void* worker_ready(void* data) {

//	int workerNum = *((int*)data);
//	printf("Thread #%d, online! \n", workerNum);

	while(1) {

		pthread_mutex_lock(&q_lock);
		while(size(q) == 0) {
			pthread_cond_wait(&q_empty, &q_lock);
		}
//		printf("Pulling conn from buffer\n");
		int fd = pop(q);
//		printf("%d\n",size(q));
		pthread_cond_signal(&q_full);
		pthread_mutex_unlock(&q_lock);

		requestHandle(fd);
		Close(fd);

	}

}


int main(int argc, char *argv[])
{
    int listenfd, connfd, port, clientlen;
    int threads, buffers;
    struct sockaddr_in clientaddr;

    getargs(&port, &threads, &buffers, argc, argv);

    q = (struct req_queue*)malloc(sizeof(struct req_queue));
    memset(q, 0, sizeof(struct req_queue));
    // 
    // CS510: Create some threads...
    //

    pthread_t thread_array[threads];
    int thread_num[threads];
    for(int i = 0; i < threads; i ++) {
    	thread_num[i] = i + 1;
    	pthread_create(&thread_array[i], NULL, worker_ready, (void*)&thread_num[i]);
    }

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
//	printf("Connection #: %d\n", connfd);
	pthread_mutex_lock(&q_lock);

	while(size(q) >= buffers) {
//		printf("main thread has to wait \n");
		pthread_cond_wait(&q_full, &q_lock);
	}

//	printf("Adding conn to buffer\n");
	push(q, connfd);
//	printf("Q size: %d\n",size(q));
	pthread_cond_broadcast(&q_empty);

	pthread_mutex_unlock(&q_lock);
	// 
	// CS510: In general, don't handle the request in the main thread.
	// Save the relevant info in a buffer and have one of the worker threads 
	// do the work. However, for SFF, you may have to do a little work
	// here (e.g., a stat() on the filename) ...
	// 

    }

}

    


 
