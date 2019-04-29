/*************************************************************************
 * Lab 5 "Easy test" used for grading
 * 
 * pNRT: ^_______r r r r v
 * pRT1: ^b b b v
 *
 *   You should see the sequence of processes depicted above:
 *     - Non real-time process pNRT and real-time process pRT1 both start
 *       at time zero. pRT1 has priority, and blinks green LED 5x @ 2.5Hz.
 *     - After pRT1 completes, pNRT begins and blinks red LED 10x @ 5Hz.
 * 
 *   pRT1 should miss its deadline, if you check in the debugger.
 * 
 ************************************************************************/
 
#include "utils.h"
#include "3140_concur.h"
#include "realtime.h"

/*--------------------------*/
/* Parameters for test case */
/*--------------------------*/

 
/* Stack space for processes */
#define NRT_STACK 40
#define RT_STACK  50
 
/*--------------------------------------*/
/* Time structs for real-time processes */
/*--------------------------------------*/

/* Constants used for 'work' and 'deadline's */
realtime_t t_1msec = {0, 1};
realtime_t t_1sec = {1, 0};
realtime_t t_5sec = {5, 0};

/* Process start time */
realtime_t t_pRT1 = {8, 0};
realtime_t t_pRT2 = {1, 0};
 
/*------------------*/
/* Helper functions */
/*------------------*/
void shortDelay(){delay();}
void mediumDelay() {delay(); delay();}

/*----------------------------------------------------
 * Non real-time process
 *----------------------------------------------------*/
 
void pNRT(void) {
	int i;
	for (i=0; i<4;i++){
	LEDRed_On();
	shortDelay();
	LEDRed_Toggle();
	shortDelay();
	}
}

/*-------------------
 * Real-time processes
 *-------------------*/

void pRT1(void) {
	int i;
	for (i=0; i<3;i++){
	LEDBlue_On();
	mediumDelay();
	LEDBlue_Toggle();
	mediumDelay();
	}
}

void pRT2(void) {
	int i;
	for (i=0; i<3;i++){
	LEDGreen_On();
	mediumDelay();
	LEDGreen_Toggle();
	mediumDelay();
	}
}

/*--------------------------------------------*/
/* Main function - start concurrent execution */
/*--------------------------------------------*/
int main(void) {	
	 
	LED_Initialize();

    /* Create processes */ 
	  if (process_create(pNRT, NRT_STACK) < 0) { return -1; }
		if (process_rt_create(pRT2, RT_STACK, &t_pRT2, &t_1msec) < 0) { return -1; }
		if (process_rt_create(pRT1, RT_STACK, &t_pRT1, &t_1sec) < 0) { return -1; } 

    /* Launch concurrent execution */
	process_start();

  LED_Off();
  while(process_deadline_miss>0) {
		LEDGreen_On();
		shortDelay();
		LED_Off();
		shortDelay();
		process_deadline_miss--;
	}
	
	/* Hang out in infinite loop (so we can inspect variables if we want) */ 
	while (1);
	return 0;
}
