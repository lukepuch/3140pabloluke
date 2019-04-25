#include "process.h"
#include "realtime.h"

/* the currently running process. current_process must be NULL if no process is running,
    otherwise it must point to the process_t of the currently running process
*/

process_t * current_process 	= NULL; 
process_t * process_queue   	= NULL;
process_t * process_tail    	= NULL;

process_t * rt_ready_queue		= NULL;
process_t * rt_notready_queue = NULL;


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

// Pop off ready queue
process_t * pop_front_rt_ready_process() {
	if (!rt_ready_queue) return NULL;
	process_t *proc = rt_ready_queue;
	rt_ready_queue = proc->next;
	return proc;	
}

// Pop off notready queue
process_t * pop_front_rt_notready_process() {
	if (!rt_notready_queue) return NULL;
	process_t *proc = rt_notready_queue;
	rt_notready_queue = proc->next;
	return proc;	
}

// Adding together two realtime variables
realtime_t* realtime_add( realtime_t* t1, realtime_t* t2 ) {  
	realtime_t* new_time;
  new_time->sec  = t1->sec + t2->sec;
  new_time->msec = t1->msec + t2->msec;
  if ( new_time->msec > 999 ){
    new_time->msec = new_time->msec - 1000;
    new_time->sec = new_time->sec + 1;
	}
	
	return new_time;
}

// Pushing onto ready queue by earliest absolute deadline
void push_onto_ready_queue (process_t * proc){
	if(rt_ready_queue == NULL && proc != NULL){
		rt_ready_queue = proc;
	}		
	else{
		if(proc != NULL)
			{
			process_t *temp = rt_ready_queue;
			process_t *prev = NULL;
			while (temp != NULL && proc->abs_deadline > temp->abs_deadline){ 
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

// Pushing onto not ready queue by earliest start time
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
	proc->abs_deadline				= NULL;
	proc->sp = proc->orig_sp 	= sp;
	proc->n 									= n;
	proc->blocked  						= 0;
	proc->ready 							= 0;
	
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
	proc->abs_deadline				= realtime_add(start,deadline);
	proc->sp = proc->orig_sp 	= sp;
	proc->n 									= n;
	proc->blocked 						= 0;
	proc->ready								= 0;
	
	// TODO: push to rt_ready_queue
	push_onto_notready_queue(proc);
	return 0;
}
