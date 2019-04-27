#include "realtime.h"
#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "shared_structs.h"

// Pablo & Luke's process.c

process_t * current_process 	= NULL; 
process_t * process_queue   	= NULL;
process_t * process_tail    	= NULL;

process_t * rt_queue					= NULL;

// Initialize these too!
int process_deadline_met			= 0;
int process_deadline_miss			= 0;

realtime_t current_time				= {0,0};

void PIT1_IRQHandler() {
	//__disable_irq();
	
	if ( current_time.msec >= 999 ) {
		current_time.sec++;
		current_time.msec = 0;
	} else {
		current_time.msec++;
	}
	
	PIT -> CHANNEL[1].TCTRL = 0;
	PIT -> CHANNEL[1].TFLG |= PIT_TFLG_TIF_MASK;
	PIT -> CHANNEL[1].TCTRL = PIT_TCTRL_TEN_MASK| PIT_TCTRL_TIE_MASK;
	
	//__enable_irq();
}

//-------------------------------------------------------------------
// pop_front_process ------------------------------------------------
//-------------------------------------------------------------------
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

//-------------------------------------------------------------------
// push_tail_process ------------------------------------------------
//-------------------------------------------------------------------
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

//-------------------------------------------------------------------
// pop_rt_process ---------------------------------------------------
//-------------------------------------------------------------------
// Returns a realtime process with earliest deadline out of all the
// processes that are ready (rt_queue is ordered by EDF).
process_t * pop_rt_process() {
	int curr_time = 1000 * current_time.sec + current_time.msec;
	process_t * temp = rt_queue;
	// If rt_queue is empty, return NULL.
	if ( !temp ) {
		return NULL;
	}
	// If first rt-process is "ready", pop it.
	else if( temp -> start < curr_time ) {
		rt_queue = temp -> next;
		temp -> next = NULL;
		return temp;
	}
	// Otherwise, iterate through rt_queue until you find
	// a "ready" process.
  else {
		while ( temp->next != NULL ) {
			if ( temp->next->start <= curr_time ) {
				process_t * proc = temp -> next;
				temp -> next = proc -> next;
				proc -> next = NULL;
				return proc;
			}
			else {
				temp = temp->next;
			}
		}
	}
	// No ready processes found, return NULL.
	return NULL;
}

//-------------------------------------------------------------------
// push_onto_rt_queue --------------------------------------------
//-------------------------------------------------------------------
// Push the process onto the rt_queue: order by EDF.
void push_onto_rt_queue (process_t * proc){
	// If rt queue is empty:
	if( rt_queue == NULL ) {
		rt_queue = proc;
		proc->next = NULL;
	}		
	else {
		// If proc has the earliest deadline in the entire queue, put it
		// in the front.
		if( rt_queue -> deadline > proc -> deadline ) {
			proc->next = rt_queue;
			rt_queue = proc;
			return;
		}
		// Otherwise, iterate until we reach a process whose deadline is bigger
		else {
			process_t * prev = NULL;
			process_t * itr = rt_queue;
			while ( itr != NULL && itr->deadline <= proc->deadline ) {
				prev = itr;
				itr = itr->next;
			}
			proc->next = itr;
			prev->next = proc;
		}
	}
}

//-------------------------------------------------------------------
// process_free -----------------------------------------------------
//-------------------------------------------------------------------
static void process_free(process_t *proc) {
	process_stack_free(proc->orig_sp, proc->n);
	free(proc);
}

//-------------------------------------------------------------------
// get_next_ready_time ----------------------------------------------
//-------------------------------------------------------------------
// Helper function to maintain realtime queues.
int getproc(){
	process_t * current_proc = queue_of_rt;
	int nextReadyTime = current_proc->start;
	current_proc = current_proc->next;
	while(current_proc != NULL){
		if(current_proc->start < nextReadyTime){
			nextReadyTime = current_proc->start;
		}
		current_proc = current_proc->next;
	}
	return nextReadyTime;
}

/* Called by the runtime system to select another process.
   "cursp" = the stack pointer for the currently running process
*/
unsigned int * process_select (unsigned int * cursp) {
	if ( cursp ) {
		// Suspending a process which has not yet finished, save state and make it the tail
		current_process->sp = cursp;
		if( current_process->rt == 0 ){
			push_tail_process(current_process);
		}
		else{
			push_onto_ready_queue(current_process);
		}
	}
	else {
		// Check if a process was running, free its resources if one just finished
		if ( current_process ) {
			if ( current_process->rt ) {
				unsigned int process_time_ms = 1000*current_time.sec + current_time.msec;
				// Update global variables to show whether or not deadline was met
				if ( process_time_ms > current_process->deadline ) {
					process_deadline_miss++;
				}
				else {
					process_deadline_met++;
				}
			}
			process_free( current_process );
		}
	}
	// Now, to choose the correct process.
	// Update the queues (check for any newly ready processes)
	help_maintain();
	// If there are any realtime processes waiting, pop and run.
	if( rt_ready_queue ) {
		current_process = pop_front_rt_ready_process();
	}
	// Else, if no ready realtime processes, but some in process_queue.
	else if ( rt_ready_queue == NULL && process_queue ) {
		current_process = pop_front_process();
	}
	// Else, if there are no ready realtime processes, no normal processes,
	// but some not-ready realtime processes.
	else if ( rt_ready_queue == NULL && process_queue == NULL && rt_notready_queue ) {
		unsigned int next_ready_start = rt_notready_queue->start;
		__enable_irq();
		while ( 1000*current_time.sec + current_time.msec < next_ready_start ) {	// BUSY WAIT
		}
		__disable_irq();
		help_maintain();
		// Now, rt_ready_queue has something in it. Pop and run it.
		current_process = pop_front_rt_ready_process();
	}
	// Now, return sp.
	if ( current_process ) {
		// Launch the process which was just popped off the queue
		return current_process->sp;
	} else {
		// No process was selected, exit the scheduler
		return NULL;
	}
}

/* Starts up the concurrent execution */
void process_start (void) {
	SIM->SCGC6 					 |= SIM_SCGC6_PIT_MASK;
	PIT->MCR 							= 0;
	PIT->CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK / 10;
	NVIC_EnableIRQ(PIT0_IRQn);
	// Don't enable the timer yet. The scheduler will do so itself
	// Lab5 Code
	PIT->CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000;
	NVIC_EnableIRQ(PIT1_IRQn);
	
	// Priorities
	NVIC_SetPriority(SVCall_IRQn, 1);
	NVIC_SetPriority(PIT0_IRQn, 1);
	NVIC_SetPriority(PIT1_IRQn, 0);	// PIT1 is highest priority
	
	PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK;
	PIT->CHANNEL[1].TCTRL |= PIT_TCTRL_TEN_MASK;

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

	proc->sp = proc->orig_sp 	= sp;
	proc->n 									= n;
	proc->blocked  						= 0;
	proc->rt 									= 0;
	
	proc->next								= NULL;
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
	
	unsigned int curr_time_ms	= 1000*current_time.sec + current_time.msec;
	proc->start								= curr_time_ms + 1000*start->sec + start->msec;
	proc->deadline						= proc->start + 1000*deadline->sec + deadline->msec;
	proc->sp = proc->orig_sp 	= sp;
	proc->n 									= n;
	proc->blocked 						= 0;
	proc->rt									= 1;

	proc->next								= NULL;
	push_onto_rt_queue(proc);
	return 0;
}
