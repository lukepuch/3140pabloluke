#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "realtime.h"

/* the currently running process. current_process must be NULL if no process is running,
    otherwise it must point to the process_t of the currently running process
*/

typedef struct process_state {
	unsigned int * sp;   /* the stack pointer for the process */
	unsigned int * orig_sp;
	unsigned int start;
	unsigned int deadline;
	process_t * next;
	int n; //stack size
	int rt;
} process_t;

process_t * current_process = NULL; 
process_t * process_queue = NULL;
process_t * process_tail = NULL;

process_t * realtime_queue = NULL;

realtime_t current_time = {0,0};
int process_deadline_met = 0;
int process_deadline_miss = 0;


static process_t * pop_front_process() {
	if (!process_queue) return NULL;
	process_t *proc = process_queue;
	process_queue = proc->next;
	if (process_tail == proc) {
		process_tail = NULL;
	}
	proc->next = NULL;
	return proc;
}

static void push_tail_process(process_t *proc) {
	if (!process_queue) {
		process_queue = proc;
	}
	if (process_tail) {
		process_tail->next = proc;
	}
	process_tail = proc;
	proc->next = NULL;
}

static process_t * pop_rt_process(){
	int processtimems = 1000*current_time.sec + current_time.msec; //formula from lab description
	process_t * tempproc = realtime_queue;
	if (!tempproc) return NULL;
	//compare start times with process time to switch current process

	if(tempproc->start <= processtimems){
		realtime_queue = tempproc->next;
		tempproc->next = NULL;
		return tempproc;
	}
	//go through queue to find shortest start time relative to current process time
	while(tempproc->next != NULL){
		if(tempproc->next->start <= processtimems){
			process_t *newcurrent_process = tempproc->next;//tempproc's next now current process, swap with next value in queue
			tempproc->next = newcurrent_process->next;
			newcurrent_process->next = NULL;
			return newcurrent_process;
		}
		else{
			tempproc = tempproc->next;
		}
	}
	return NULL; //nothing popped
	
}

static void push_rt_process(process_t *proc) {
	//decide whether to put this process at the head of the realtime queue
	if(!realtime_queue){
		realtime_queue = proc;
		return;
	}
	
	//swap this process with current queue head
	process_t *rt_queue_head = realtime_queue;
	if(rt_queue_head->deadline > proc->deadline){
		proc->next = rt_queue_head;
		realtime_queue = proc;
		return;
	}
	
	//move proc back in the queue until its deadline is no longer later than the process behind it
	process_t * last_proc = NULL;
	process_t * current_proc = rt_queue_head;
	while(current_proc != NULL && current_proc->deadline <= proc->deadline){
		last_proc = current_proc;
		current_proc = current_proc->next;
	}
	//deadline of current process is now later than proc; insert proc in appropriate place in rt queue
	proc->next = current_proc;
	last_proc->next = proc;
	
}

static void process_free(process_t *proc) {
	process_stack_free(proc->orig_sp, proc->n);
	free(proc);
}

static int find_next_ready_process(){
	process_t * current_proc = realtime_queue;
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
	if (cursp) {
		// Suspending a process which has not yet finished, save state and make it the tail
		current_process->sp = cursp;
		if(current_process->rt == 0){
			push_tail_process(current_process);
		}
		else{
			push_rt_process(current_process);
		}
	} 
	else {
		// Check if a process was running, free its resources if one just finished
		if (current_process) {
			if(current_process->rt){
				unsigned int processtimems = 1000*current_time.sec + current_time.msec;
				//increment global variables to show whether or not deadline was met
				if(processtimems > current_process->deadline){
					process_deadline_miss++;
				}
				else{
					process_deadline_met++;
				}
			}
			process_free(current_process);
		}
	}
	
	// Select the new current process from the front of the process queue if no rt processes to check
	if(realtime_queue == NULL){
		current_process = pop_front_process();
	}
	else{
		process_t * proctocheck = pop_rt_process();
		if(proctocheck == NULL){
			if(process_queue == NULL){
				//wait until next closest process has arrived
				int waittime = find_next_ready_process();
				__enable_irq(); //use clock & wait until the next process arrives
				while((1000*current_time.sec + current_time.msec) < waittime);
				__disable_irq();
				current_process = pop_rt_process();
			}
			else {
				current_process = pop_front_process();
			}	
		}
		else{
			current_process = proctocheck; // if process queue is empty, automatically use rt process queue
		}
	}
	if (current_process) {
		// Launch the process which was just popped off the queue
		return current_process->sp;
	} else {
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
	PIT->CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000; //using PIT1 to generate interrupts every ms
	NVIC_EnableIRQ(PIT1_IRQn);
	NVIC_SetPriority(SVCall_IRQn, 1);
	NVIC_SetPriority(PIT0_IRQn, 2);
	NVIC_SetPriority(PIT1_IRQn, 0);
	// Don't enable the timer yet. The scheduler will do so itself
	PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK;
	PIT->CHANNEL[1].TCTRL |= PIT_TCTRL_TEN_MASK;
	// Bail out fast if no processes were ever created
	if (!process_queue && !realtime_queue) return;
	process_begin();
	
	
}

void PIT1_IRQHandler(void){
	if(current_time.msec != 999){ //increment millisecond counter every ms
		current_time.msec++;
	}
	else{ //increment seconds, we have reached 1000 ms
		current_time.msec = 0;
		current_time.sec++;
	}
	PIT->CHANNEL[1].TCTRL = 0;
	PIT->CHANNEL[1].TFLG |= PIT_TFLG_TIF_MASK;
	PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TEN_MASK| PIT_TCTRL_TIE_MASK;
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
	
	proc->sp = proc->orig_sp = sp;
	proc->n = n;
	proc->rt = 0;
	
	push_tail_process(proc);
	return 0;
}

int process_rt_create(void (*f)(void), int n, realtime_t *start, realtime_t *deadline){
	unsigned int *sp = process_stack_init(f,n);
	if (!sp) return -1;
	
	process_t *proc = (process_t*) malloc(sizeof(process_t));
	if (!proc) {
		process_stack_free(sp, n);
		return -1;
	}
	
	proc->sp = proc->orig_sp = sp;
	proc->n = n;
	proc->rt = 1;
	proc->next = NULL;
	
	unsigned int processtimems = 1000*current_time.sec + current_time.msec;
	proc->start = processtimems + (1000*(start->sec)) + start->msec;
	proc->deadline = proc->start + (1000*(deadline->sec)) + deadline->msec;
	push_rt_process(proc);
	return 0;
}	
