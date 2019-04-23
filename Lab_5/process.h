#ifndef __PROCESS_H_INCLUDED__
#define __PROCESS_H_INCLUDED__

//We make use of a header file here so that we can have access
//to the global variables we want as well as the global function
//push_tail_process.

#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "shared_structs.h"
#include <stdlib.h>
#include "realtime.h"

extern process_t * current_process;
extern process_t * process_queue;
extern process_t * process_tail;
extern realtime_t current_time;

void push_tail_process(process_t *proc);

#endif
