#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "realtime.h"
#include "shared_structs.h"

// Initialize global variables

process_t * current_process	 			= NULL; 
process_t * process_tail 					= NULL;
process_t * process_queue 				= NULL;

process_t * rt_queue = NULL;
realtime_t current_time = {0, 0};

int process_deadline_met = 0;
int process_deadline_miss = 0;

//-------------------------------------------------------------------
// PIT1_IRQHandler --------------------------------------------------
//-------------------------------------------------------------------
void PIT1_IRQHandler(void) {
	if(current_time.msec > 999)	{
		current_time.sec++;
		current_time.msec = 0;
	}	else {
		current_time.msec++;
	}

	PIT->CHANNEL[1].TCTRL = 0;
	PIT->CHANNEL[1].TFLG |= PIT_TFLG_TIF_MASK;
	PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TEN_MASK| PIT_TCTRL_TIE_MASK;
}

//-------------------------------------------------------------------
// push_tail_process ------------------------------------------------
//-------------------------------------------------------------------
void push_tail_process(process_t *proc) {
	// If queue is empty, then just insert it.
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
// pop_front_process ------------------------------------------------
//-------------------------------------------------------------------
process_t * pop_front_process() {
	//If queue is empty, return NULL
	if (!process_queue) return NULL;	

	process_t *proc = process_queue;
	process_queue = proc->next;

	//If proc was the only process set tail to NULL
	if (process_tail == proc) {
		process_tail = NULL;
	}
	proc->next = NULL;
	return proc;
}

//-------------------------------------------------------------------
// push_onto_rt_queue --------------------------------------------
//-------------------------------------------------------------------
// Push the process onto the rt_queue: order by EDF.
void push_onto_rt_queue(process_t *proc) {
	//If there is nothing in rt_queue, make proc head of queue
	if (!rt_queue) {
		rt_queue = proc;
		return;
	}

	//Check if earliest deadline. If so, put in front
	process_t * temp = rt_queue;
	if (temp->deadline > proc->deadline) {
		proc->next = temp;
		rt_queue = proc;
		return;
	}

	//Otherwise, iterate through queue until bigger deadline is found.
	//Insert in front of bigger deadline.
	process_t * tail = NULL;
	process_t * itr = temp;
	
	while ((itr != NULL) && (itr->deadline <= proc->deadline)) {
		tail = itr;
		itr = itr->next;
	}

	proc->next = itr;
	tail->next = proc;
}

//-------------------------------------------------------------------
// pop_rt_process ---------------------------------------------------
//-------------------------------------------------------------------
// Returns a realtime process with earliest deadline out of all the
// processes that are ready (rt_queue is ordered by EDF).
process_t * pop_rt_process() {
	int real_time = 1000 * current_time.sec + current_time.msec;
	
	//If rt_queue is empty return NULL
	if (!rt_queue) return NULL;
	
	//Otheriwse, iterate through queue until ready process found.
	process_t * itr 	= rt_queue;
	process_t * prev 	= NULL;

	while ( itr != NULL) {
		//If itr is ready
		if (itr->start <= real_time) {
			//Check if itr is the first process in rt_queue; pop accordingly if so.
			if (itr == rt_queue) {
				rt_queue 	= itr->next;
				itr->next = NULL;
				return itr;
			} else {
				prev->next = itr->next;
				itr->next  = NULL;
				return itr;		
			}
		}
		prev = itr;
		itr  = itr->next;
	}
	
	// Otherwise, no ready process found: return NULL
	return NULL;
}

//-------------------------------------------------------------------
// process_free -----------------------------------------------------
//-------------------------------------------------------------------
void process_free(process_t *proc) {
	process_stack_free(proc->orig_sp, proc->n);
	free(proc);
}

//-------------------------------------------------------------------
// get_next_start_time ----------------------------------------------
//-------------------------------------------------------------------
// Returns the integer value of the start time of the next ready process.
unsigned int get_next_start_time(){
	process_t * proc 				= rt_queue;
	unsigned int next_start = proc->start;
	proc = proc->next;
		while(proc != NULL){
			if(proc->start < next_start){
				next_start = proc->start;
			}
			proc = proc->next;
		}
	return next_start;
}

//-------------------------------------------------------------------
// process_select ---------------------------------------------------
//-------------------------------------------------------------------
unsigned int * process_select (unsigned int * cursp) {
	// If process was in the middle of executing, save cursp and
	// queue the processes to the appropriate queue (rt or process_queue).
	if (cursp) {
		current_process->sp = cursp;
		if (current_process->rt == 1) {
			push_onto_rt_queue(current_process);
		} else {
		push_tail_process(current_process);
		}
	}
	// cursp is NULL, meaing process either finished or nonexistent.
	else {
		// If a process existed, (and finished)
		if (current_process) {
			// ..and if it was real-time: update global variable (met or miss).
			if (current_process->rt == 1) {
				unsigned int real_time = 1000*current_time.sec + current_time.msec;
				if (real_time <= current_process->deadline) {
					process_deadline_met++;
				} else {
					process_deadline_miss++;
				}
			}
			// Then, free the process.
			process_free(current_process);
		}
	}
	
	// Now, need to decide what process to queue next.
	// If rt_queue is empty, run from process_queue.
	if (rt_queue == NULL) {
		current_process = pop_front_process();
	}
	// rt_queue is not empty
	else {	
		process_t * tmp = pop_rt_process();
		// If there is a rt ready process, run it. Otherwise,
		// check to see if process queue is empty. If it is not
		// empty, run non-rt process. If it is empty, busy wait
		// for a rt process to become ready.
		if (tmp!=NULL) current_process = tmp;
		else {
			if (process_queue && !tmp) current_process = pop_front_process();
			else if (!tmp && !process_queue) {
				unsigned int delay = get_next_start_time();	// absolute start time
				__enable_irq();
				while((1000*current_time.sec + current_time.msec) < delay);
				__disable_irq();
				// Now, pop_rt_process() should return a ready process, not NULL
				current_process = pop_rt_process();
			}
		}
	}
		
		/*
		  if ( (tmp == NULL) && (process_queue == NULL) )	{
			// BUSY WAIT CASE
			unsigned int delay = get_next_start_time();	// absolute start time
			__enable_irq();
			while((1000*current_time.sec + current_time.msec) < delay);
			__disable_irq();
			// Now, pop_rt_process() should return a ready process, not NULL
			current_process = pop_rt_process();
		}	else if ((process_queue != NULL) && (tmp == NULL)) {
			current_process = pop_front_process();
		} else {
			current_process = tmp;
		}
	}
		*/
	// Now, return the appropriate stack pointer.
	if (current_process) return current_process->sp;
	else {
		return NULL;
	}
}

//-------------------------------------------------------------------
// process_start ----------------------------------------------------
//-------------------------------------------------------------------
void process_start (void){
	SIM->SCGC6 					 |= SIM_SCGC6_PIT_MASK;
	PIT->MCR 							= 0;
	PIT->CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK / 10;
	PIT->CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000;
	
	NVIC_EnableIRQ(PIT0_IRQn);
	NVIC_EnableIRQ(PIT1_IRQn);
	
	// Set priorities.
	NVIC_SetPriority(PIT1_IRQn, 0);	// highest priority
	NVIC_SetPriority(PIT0_IRQn, 1);
	NVIC_SetPriority(SVCall_IRQn, 1);
	
	// Other PIT1 control registers.
	PIT->CHANNEL[1].TCTRL  = PIT_TCTRL_TIE_MASK;
	PIT->CHANNEL[1].TCTRL |= PIT_TCTRL_TEN_MASK;
	
	// If NO processes were created, bail out real quick.
	if ((process_queue == NULL) && (rt_queue == NULL)) {
		return;
	}
	process_begin();
}

//-------------------------------------------------------------------
// process_create ---------------------------------------------------
//-------------------------------------------------------------------
int process_create (void (*f)(void), int n){
	unsigned int *sp = process_stack_init(f, n);
	if (!sp) {
		return -1;
	}
	process_t *proc = (process_t*) malloc(sizeof(process_t));
	if (!proc) {
		process_stack_free(sp, n);
		return -1;
	}
	
	proc->n 									= n;
	proc->rt 									= 0;
	proc->sp = proc->orig_sp 	= sp;
	proc->next								= NULL;
	proc->start								=	NULL;
	proc->deadline						= NULL;

	push_tail_process(proc);
	return 0;
}

//-------------------------------------------------------------------
// process_rt_create ------------------------------------------------
//-------------------------------------------------------------------
int process_rt_create(void (*f)(void), int n, realtime_t *start, realtime_t *deadline){
	unsigned int *sp = process_stack_init(f, n);
	if (!sp) {
		return -1;
	}
	process_t *proc = (process_t*) malloc(sizeof(process_t));
	if (!proc) {
		process_stack_free(sp, n);
		return -1;
	}

	unsigned int curr_time 		= 1000*current_time.sec + current_time.msec;
	proc->n 									= n;
	proc->rt 									= 1;
	proc->sp = proc->orig_sp 	= sp;
	proc->next 								= NULL;
	proc->start 							= curr_time + ( 1000 * start->sec ) + start->msec;
	proc->deadline 						= proc->start + ( 1000 * deadline->sec ) + deadline->msec;
	
	push_onto_rt_queue(proc);
	return 0;
} 
