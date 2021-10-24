#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "helper.h"
#include "request.h"
#include "shared-memory-slot.h"

pthread_mutex_t smutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;

int buffers; 
int *work_buffer;
int add_fd_ptr = 0;
int rem_fd_ptr = 0;
int numFd = 0;
int shm_idx = 0;
int threads;

slot_t *shm_ptr;
int pagesize;
char shm_name[1000]; 

void addFd (int value) {
  work_buffer[add_fd_ptr] = value;
  add_fd_ptr = (add_fd_ptr + 1) %  buffers;
  numFd++;
}

int getFd () {
  int tmp = work_buffer[rem_fd_ptr];
  rem_fd_ptr = (rem_fd_ptr + 1) % buffers;
  numFd --;
  return tmp; 
}

// 
// server.c: A very, very simple web server
//
// To run:
//  server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

void getargs(int *port, int *threads, int *buffers, char *shm_name, int argc, char *argv[])
{
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <port> <threads> <buffers> <shm_name>\n", argv[0]);
    exit(-1);
  }
  *port = atoi(argv[1]);
  *threads = atoi(argv[2]);
  *buffers = atoi(argv[3]);
  strcpy(shm_name, argv[4]);
}

static void *
worker_func(void *arg) {
        // NOTE: on different platforms, pthread_self()
        // might return different things. I know that on
        // Macintosh it returns a pointer to a structure
        // describing the thread. On UNIX it returns a
        // long unsigned int.

  //printf("Printing thread id %lu\n", pthread_self());

  pthread_mutex_lock(&smutex);
  shm_ptr[shm_idx].thread_id = pthread_self();
  shm_ptr[shm_idx].requests = 0;
  shm_ptr[shm_idx].static_req = 0;
  shm_ptr[shm_idx].dyn_req = 0;
  shm_idx++;
  pthread_mutex_unlock(&smutex);

  while (1) {
    pthread_mutex_lock(&mutex);
    while (numFd == 0) {
      pthread_cond_wait(&fill, &mutex);
    }
    int connfd = getFd();
    pthread_cond_signal(&empty);
    pthread_mutex_unlock(&mutex);

    int ret = requestHandle(connfd);
    int idx;

    if (ret == 1) {
      for (idx = 0; idx < threads; idx++) {
        if (shm_ptr[idx].thread_id == pthread_self()) {
          shm_ptr[idx].static_req += 1;
          shm_ptr[idx].requests += 1;
        }
      }
    }
    else if (ret == 2) {
      for (idx = 0; idx < threads; idx++) {
        if (shm_ptr[idx].thread_id == pthread_self()) {
          shm_ptr[idx].dyn_req += 1;
          shm_ptr[idx].requests += 1;
        }
      }
    }
    else {
      for (idx = 0; idx < threads; idx++) {
        if (shm_ptr[idx].thread_id == pthread_self()) {
          shm_ptr[idx].requests += 1;
        }
      }
    }

    Close(connfd);
  }

  return NULL;
}

static void
sigint_handler(int sig) {
  // Unmap.
  int ret = munmap(shm_ptr, pagesize);
  if (ret != 0) {
    perror("munmap");
    exit(1);
  }

  // Delete the shared memory region.
  ret = shm_unlink(shm_name);
  if (ret != 0) {
    perror("shm_unlink");
    exit(1);
  }
  free(work_buffer);
  exit(0);
}

int main(int argc, char *argv[])
{
  int listenfd, connfd, port, clientlen; 
  struct sockaddr_in clientaddr;

  getargs(&port, &threads, &buffers, shm_name, argc, argv);

  if (buffers <= 0 || threads <= 0 || port <= 2000 || port > 65535) {
    perror("Incorrect arguments\n");
    exit(-1);
  } 

  pagesize = getpagesize();

  // Create the shared memory region.
  int shmfd = shm_open(shm_name, O_RDWR | O_CREAT, 0660); 
  if (shmfd < 0) {
    perror("shm_open");
    return 1;
  }

  // Extend its size.
  int ret = ftruncate(shmfd, pagesize);
  if (ret != 0) {
    perror("ftruncate");
    return 1;
  }

  // Memory map.
  shm_ptr = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
  if (shm_ptr == MAP_FAILED) {
    perror("mmap");
    return 1;
  }


  work_buffer = malloc(buffers * sizeof *work_buffer);

  pthread_t workers[threads];
  for (int i = 0; i < threads; ++i) {
    pthread_create(&workers[i], NULL, worker_func, NULL);
  }

  listenfd = Open_listenfd(port);
  while (1) {
    signal(SIGINT,  sigint_handler);

    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
    // 
    // Save the relevant info in a buffer and have one of the worker threads 
    // do the work. Also let the worker thread close the connection.
    // 

    pthread_mutex_lock(&mutex);
    while (numFd == buffers) {
      pthread_cond_wait(&empty, &mutex);
    }
    addFd(connfd);
    pthread_cond_signal(&fill);
    pthread_mutex_unlock(&mutex);

  }
}
