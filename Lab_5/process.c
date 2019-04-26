//#include "process.h"
#include "realtime.h"
#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "shared_structs.h"

/* the currently running process. current_process must be NULL if no process is running,
    otherwise it must point to the process_t of the currently running process
*/

process_t * current_process 	= NULL; 
process_t * process_queue   	= NULL;
process_t * process_tail    	= NULL;

process_t * rt_ready_queue		= NULL;
process_t * rt_notready_queue = NULL;

// Initialize these too!
int process_deadline_met			= 0;
int process_deadline_miss			= 0;

realtime_t current_time				= {0,0};


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
// pop_front_rt_ready_process ---------------------------------------
//-------------------------------------------------------------------
process_t * pop_front_rt_ready_process() {
	if (!rt_ready_queue) return NULL;
	process_t *proc = rt_ready_queue;
	rt_ready_queue = proc->next;
	return proc;	
}

//-------------------------------------------------------------------
// pop_front_rt_notready_process ------------------------------------
//-------------------------------------------------------------------
process_t * pop_front_rt_notready_process() {
	if (!rt_notready_queue) return NULL;
	process_t *proc = rt_notready_queue;
	rt_notready_queue = proc->next;
	return proc;	
}

//-------------------------------------------------------------------
// push_onto_ready_queue --------------------------------------------
//-------------------------------------------------------------------
// Push process onto ready queue, by earliest absolute deadline.
void push_onto_ready_queue (process_t * proc){
	if(rt_ready_queue == NULL && proc != NULL){
		rt_ready_queue = proc;
	}		
	else{
		if(proc != NULL)
			{
			process_t *temp = rt_ready_queue;
			process_t *prev = NULL;
			while (temp != NULL && proc->deadline > temp->deadline){ 
				prev = temp;
				temp = temp-> next;
			}
			if(temp == rt_ready_queue) { // Condition for earliest deadline in queue
				proc->next = rt_ready_queue;
				rt_ready_queue = proc;
			} else if(temp != NULL && prev != NULL) { // Condition for deadline in middle of queue
				prev->next = proc;
				proc->next = temp;				
			} else { // Condition for latest deadline
				prev->next = proc;
				proc->next = NULL;
			}
		}
	}
}

//-------------------------------------------------------------------
// push_onto_notready_queue -----------------------------------------
//-------------------------------------------------------------------
// Pushing onto not ready queue, by earliest start time.
void push_onto_notready_queue(process_t * proc){
	if(rt_notready_queue == NULL && proc != NULL){
		rt_notready_queue = proc;
	}		
	else {
		if(proc != NULL)
			{
			process_t *temp = rt_notready_queue;
			process_t *prev = NULL;
			while (temp != NULL && proc->start > temp->start){
				prev = temp;
				temp = temp-> next;
			}
			if(temp == rt_notready_queue) { // Condition for earliest start in queue
				proc->next = rt_notready_queue;
				rt_notready_queue = proc;
			} else if(temp != NULL && prev != NULL) { // Condition for start in middle of deadline
				prev->next = proc;
				proc->next = temp;				
			} else { // Condition for latest start in queue
				prev->next = proc;
				proc->next = NULL;
			}
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
// help_maintain ----------------------------------------------------
//-------------------------------------------------------------------
// Helper function to maintain realtime queues.
void help_maintain() {
	process_t* itr = rt_notready_queue;
	while ( itr!=NULL ) { 	
	//iterate through entire queue (or
	//until itr reaches still-not-ready process)
		unsigned int current_time_ms = 1000*current_time.sec + current_time.msec;
		if ( itr->start > current_time_ms ) {	
		//if itr is ready
			push_onto_ready_queue( itr );
			itr = itr->next;
		}
		else {
			break;
		}	
	}	
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
		__enable_irq();
		while ( rt_ready_queue == NULL ) {	// BUSY WAIT
			help_maintain();
		}
		__disable_irq();
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
	current_time.msec 		= 0;
	current_time.sec  		= 0;
	process_deadline_met 	= 0;
	process_deadline_miss = 0;
	NVIC_EnableIRQ(PIT1_IRQn);
	NVIC_SetPriority(SVCall_IRQn, 1);
	NVIC_SetPriority(PIT0_IRQn, 2);
	NVIC_SetPriority(PIT1_IRQn, 0);
	// Don't enable the timer yet. The scheduler will do so itself
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

	proc->sp = proc->orig_sp 		= sp;
	proc->n 										= n;
	proc->blocked  							= 0;
	proc->rt 										= 0;
	
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

	push_onto_notready_queue(proc);
	return 0;
}
