#include "3140_concur.h"
#include <fsl_device_registers.h>
#include "realtime.h"

//OURS

typedef struct process_state 
{
	unsigned int * sp; 
	unsigned int * orig_sp;
	unsigned int start;
	unsigned int deadline;
	process_t * next;
	int n;
	int rt;
} process_t;

process_t * curr_proc = NULL; 
process_t * proc_tail = NULL;
process_t * proc_queue = NULL;

process_t * queue_of_rt = NULL;
realtime_t current_time = {NULL, NULL};

int process_deadline_met = 0;
int process_deadline_miss = 0;

process_t * pop_front_proc(){
	if (!proc_queue)
	{
		return NULL;
	}
	process_t *proc = proc_queue;
	proc_queue = proc -> next;
	if (proc_tail == proc) 
	{
		proc_tail = NULL;
	}
	proc -> next = NULL;
	return proc;
}

void push_proc_tail(process_t *proc){
	if (!proc_queue) 
	{
		proc_queue = proc;
	}
	if (proc_tail)
	{
		proc_tail -> next = proc;
	}
	proc_tail = proc;
	proc -> next = NULL;
}

process_t * pop_rt_process(){
	int absolute_time = 1000 * current_time.sec + current_time.msec;
	process_t * temp = queue_of_rt;
	if (!temp) 
	{
		return NULL;
	}
	else if(temp -> start < absolute_time)
	{
		queue_of_rt = temp -> next;
		temp -> next = NULL;
		return temp;
	}
    else
    {
        while(temp->next != NULL)
        {
            if(temp->next->start <= absolute_time)
            {
                process_t *newcurr_proc = temp -> next;
                temp -> next = newcurr_proc -> next;
                newcurr_proc -> next = NULL;
                return newcurr_proc;
            }
            else
            {
                temp = temp->next;
            }
        }
    }
	return NULL;
}

void push_rt_process(process_t *proc){
	if(!queue_of_rt)
	{
		queue_of_rt = proc;
		return;
	}
	
	process_t *rt_queue_head = queue_of_rt;
	if(rt_queue_head -> ddl > proc -> ddl)
	{
		proc->next = rt_queue_head;
		queue_of_rt = proc;
		return;
	}
	
	process_t * last_proc = NULL;
	process_t * current_proc = rt_queue_head;
	while(current_proc != NULL && current_proc->ddl <= proc->ddl){
		last_proc = current_proc;
		current_proc = current_proc->next;
	}
	
	proc->next = current_proc;
	last_proc->next = proc;
	
}

void process_free(process_t *proc){
	process_stack_free(proc->orig_sp, proc->ss);
	free(proc);
}

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

unsigned int * process_select (unsigned int * cursp){
	if (cursp) 
	{
		curr_proc->sp = cursp;
		if(curr_proc -> rt != 0)
		{
			push_rt_process(curr_proc);
		}
		else
		{
			push_proc_tail(curr_proc);
		}
	} 
	else 
	{
		if (curr_proc) 
		{
			if(curr_proc->rt)
			{
				unsigned int absolute_time = 1000*current_time.sec + current_time.msec;
				if(absolute_time <= curr_proc->ddl)
				{
					process_deadline_met++;
				}
				else
				{
					process_deadline_miss++;
				}
			}
	process_free(curr_proc);
		}
	}
	
	if(queue_of_rt == NULL){
		curr_proc = pop_front_proc();
	}
	else{
		process_t * tempproc = pop_rt_process();
		if(tempproc == NULL && proc_queue == NULL)
		{
				int delay = getproc();
				__enable_irq();
				while((current_time.sec + 1000 * current_time.msec) < delay);
				__disable_irq();
				curr_proc = pop_rt_process();
		}
		else if(tempproc == NULL && proc_queue != NULL) 
		{
				curr_proc = pop_front_proc();
		}	
		else
		{
			curr_proc = tempproc;
		}
	}
	if (curr_proc) 
	{
		return curr_proc->sp;
	} 
	else 
	{
		return NULL;
	}
}

void process_start (void){
	SIM -> SCGC6 |= SIM_SCGC6_PIT_MASK;
	PIT -> MCR = 0;
	PIT -> CHANNEL[0].LDVAL = DEFAULT_SYSTEM_CLOCK / 10;
	NVIC_EnableIRQ(PIT0_IRQn);
	PIT -> CHANNEL[1].LDVAL = DEFAULT_SYSTEM_CLOCK / 1000;
	NVIC_EnableIRQ(PIT1_IRQn);
	PIT -> CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK;
	PIT -> CHANNEL[1].TCTRL |= PIT_TCTRL_TEN_MASK;
	if (!queue_of_rt && !proc_queue) 
	{
		return;
	}
	process_begin();
}

void PIT1_IRQHandler(void){
	if(current_time.msec > 999)
	{
		current_time.sec = current_time.sec + 1;
		current_time.msec = 0;
	}
	else
	{
		current_time.msec = current_time.msec + 1;
	}
	
	PIT -> CHANNEL[1].TCTRL = 0;
	PIT -> CHANNEL[1].TFLG |= PIT_TFLG_TIF_MASK;
	PIT -> CHANNEL[1].TCTRL = PIT_TCTRL_TEN_MASK| PIT_TCTRL_TIE_MASK;
}

int process_create (void (*f)(void), int ss){
	unsigned int *sp = process_stack_init(f, ss);
	if (!sp) 
	{
		return -1;
	}
	
	process_t *proc = (process_t*) malloc(sizeof(process_t));
	if (!proc) 
	{
		process_stack_free(sp, ss);
		return -1;
	}
	
	proc -> sp = proc -> orig_sp = sp;
	proc -> ss = ss;
	proc -> rt = 0;
	
	push_proc_tail(proc);
	return 0;
}

int process_rt_create(void (*f)(void), int ss, realtime_t *start, realtime_t *ddl){
	unsigned int *sp = process_stack_init(f, ss);
	unsigned int absolute_time = current_time.sec + 1000 * current_time.msec;
	if (!sp) 
	{
		return -1;
	}
	process_t *proc = (process_t*) malloc(sizeof(process_t));
	
	if (!proc) 
	{
		process_stack_free(sp, ss);
		return -1;
	}
	
	proc -> ss = ss;
	proc -> rt = 1;
	proc -> sp = proc -> orig_sp = sp;
	proc -> next = NULL;
	proc -> start = absolute_time + (1000*(start -> sec)) + start -> msec;
	proc -> deadline = proc -> start + (1000 * (ddl -> sec)) + ddl -> msec;
	push_rt_process(proc);
	return 0;
}	
