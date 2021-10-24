#ifndef _SHARED_MEMORY_SLOT_H_
#define _SHARED_MEMORY_SLOT_H_

typedef struct slot {
  long unsigned int thread_id;
  int requests;
  int static_req;
  int dyn_req;
} slot_t;


#endif /* _SHARED_MEMORY_SLOT_H_ */
