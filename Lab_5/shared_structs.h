#ifndef __SHARED_STRUCTS_H__
#define __SHARED_STRUCTS_H__
#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "shared_structs.h"
#include <stdlib.h>
#include "realtime.h"
/** Implement your structs here */

/**
 * This structure holds the process structure information
 */
struct process_state {
	unsigned int *sp;
	unsigned int *orig_sp;
	int n;
	process_t *next;
	int blocked;	
	realtime_t * start;
	realtime_t * deadline;
	int priority;
};

/**
 * This defines the lock structure
 */
typedef struct lock_state {
	int held;
	process_t * blocked_queue;
	process_t * blocked_queue_end;
} lock_t;

/**
 * This defines the conditional variable structure
 */
typedef struct cond_var {

} cond_t;

#endif
