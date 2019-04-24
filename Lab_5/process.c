#include "process.h"
#include "realtime.h"

/* the currently running process. current_process must be NULL if no process is running,
    otherwise it must point to the process_t of the currently running process
*/

process_t * current_process = NULL; 
process_t * process_queue   = NULL;
process_t * process_tail    = NULL;

process_t * realtime_queue	= NULL;

realtime_t current_time;
//current_time.sec = 0;

void PIT1_IRQHandler() {
	__disable_irq();
	
	if ( current_time.msec >= 999 ) {
		current_time.sec++;
		current_time.msec = 0;
	} else {
		current_time.msec++;
	}
	
	__enable_irq();
}

process_t * pop_front_process() {
	if (!process_queue) return NULL;
	process_t *proc = process_queue;
	process_queue = proc->next;
	if (process_tail == proc) {
		process_tail = NULL;
	}
	proc->next = NULL;
	return proc;
}

void push_tail_process(process_t *proc) {
	if (!process_queue) {
		process_queue = proc;
	}
	if (process_tail) {
		process_tail->next = proc;
	}
	process_tail = proc;
	proc->next = NULL;
}

// Realtime functions:
process_t * pop_front_rt_process() {
	if (!realtime_queue) return NULL;
	process_t *proc = realtime_queue;
	realtime_queue = proc->next;
	return proc;
}

void push_tail_rt_process(process_t *proc) {
	if (!realtime_queue) {
		process_queue = proc;
	}
	// push back until 
}

static void process_free(process_t *proc) {
	process_stack_free(proc->orig_sp, proc->n);
	free(proc);
}

/* Called by the runtime system to select another process.
   "cursp" = the stack pointer for the currently running process
*/
unsigned int * process_select (unsigned int * cursp) {
	if (cursp) {
		// Suspending a process which has not yet finished, save state and make it the tail
		current_process->sp = cursp;
		
		if(current_process->blocked == 0){
			push_tail_process(current_process);
		}
	} else {
		// Check if a process was running, free its resources if one just finished
		if (current_process) {	
			process_free(current_process);
		}
	}
	
	// Select the new current process from the front of the queue
	current_process = pop_front_process();
	
	if (current_process && current_process->blocked == 0) {
		// Launch the process which was just popped off the queue
		return current_process->sp;
	}	else {
		// No process was selected, exit the scheduler
		return NULL;
	}
}

/* Starts up the concurrent execution */
void process_start (void) {
	SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
	PIT->MCR = 0;
	PIT->CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK / 10;
	NVIC_EnableIRQ(PIT0_IRQn);
	// Don't enable the timer yet. The scheduler will do so itself
	
	// Lab5 Code
	PIT->CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000;
	current_time.msec = 0;
	current_time.sec  = 0;
	NVIC_EnableIRQ(PIT1_IRQn);
	
	// Bail out fast if no processes were ever created
	if (!process_queue) return;
	process_begin();
}

/* Create a new process */
int process_create (void (*f)(void), int n) {
	unsigned int *sp = process_stack_init(f, n);
	if (!sp) return -1;
	
	process_t *proc = (process_t*) malloc(sizeof(process_t));
	if (!proc) {
		process_stack_free(sp, n);
		return -1;
	}
	
	proc->deadline 						= NULL;
	proc->start    						= NULL;
	proc->sp = proc->orig_sp 	= sp;
	proc->n 									= n;
	proc->blocked  						= 0;
	proc->priority 						= 0;
	
	push_tail_process(proc);
	return 0;
}

int process_rt_create(void (*f)(void), int n, realtime_t *start, realtime_t *deadline){
	unsigned int *sp = process_stack_init(f, n);
	if (!sp) return -1;

	process_t *proc = (process_t*) malloc(sizeof(process_t));

	if (!proc) {
		process_stack_free(sp, n);
		return -1;
	}	
	  
	proc->deadline 						= deadline;
	proc->start              	= start;
	proc->sp = proc->orig_sp 	= sp;
	proc->n 									= n;
	proc->blocked 						= 0;
	proc->priority						= 1;
	
	// TODO: push to realtime_queue
	push_tail_rt_process(proc);
	return 0;
}
