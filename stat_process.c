#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "shared-memory-slot.h"
#include <time.h>
#include <stdlib.h>

void getargs(char *shm_name, int *sleep, int *threads, int argc, char *argv[])
{
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <shm_name> <sleep_ms> <threads>\n", argv[0]);
    exit(-1);
  }
  strcpy(shm_name, argv[1]);
  *sleep = atoi(argv[2]);
  *threads = atoi(argv[3]);
}

int
main(int argc, char *argv[]) {

  char shm_name[1000];
  int sleep_ms, threads;

  getargs(shm_name, &sleep_ms, &threads, argc, argv);

  if (threads <= 0 || sleep_ms <= 0) {
    perror("Incorrect arguments\n");
    return 1;
  }

  int sleep_s = sleep_ms/1000;
  struct timespec remaining, request = {sleep_s, 0}; 

  int pagesize = getpagesize();

  // Create the shared memory region.
  int shmfd = shm_open(shm_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (shmfd < 0) {
    perror("shm_open");
    return 1;
  }

  // Memory map.
  slot_t *shm_ptr = mmap(NULL, pagesize, PROT_READ, MAP_SHARED, shmfd, 0);
  if (shm_ptr == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  while (1) {
    nanosleep(&request, &remaining);
    for (int i = 0; i < threads; i++) {
      printf("%d\n", i);
      printf("%lu : %d %d %d\n", shm_ptr[i].thread_id, shm_ptr[i].requests, shm_ptr[i].static_req, shm_ptr[i].dyn_req);
    }
  }

  // Unmap.
  int ret = munmap(shm_ptr, pagesize);
  if (ret != 0) {
    perror("munmap");
    return 1;
  }

  return 0;    
}
