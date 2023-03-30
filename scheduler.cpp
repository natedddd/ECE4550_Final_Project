#include "scheduler.h"

#define schedUSE_TCB_ARRAY 1

int TOTAL_EXECUTION_TIME = 0;

/* Extended Task control block for managing periodic tasks within this library. */
typedef struct xExtended_TCB
{
	TaskFunction_t pvTaskCode; 		/* Function pointer to the code that will be run periodically. */
	const char *pcName; 			/* Name of the task. */
	UBaseType_t uxStackDepth; 			/* Stack size of the task. */
	void *pvParameters; 			/* Parameters to the task function. */
	UBaseType_t uxPriority; 		/* Priority of the task. */
	TaskHandle_t *pxTaskHandle;		/* Task handle for the task. */
	TickType_t xReleaseTime;		/* Release time of the task. */
	TickType_t xRelativeDeadline;	/* Relative deadline of the task. */
	TickType_t xAbsoluteDeadline;	/* Absolute deadline of the task. */
	TickType_t xPeriod;				/* Task period. */
	TickType_t xLastWakeTime; 		/* Last time stamp when the task was running. */
	TickType_t xMaxExecTime;		/* Worst-case execution time of the task. */
	TickType_t xExecTime;			/* Current execution time of the task. */

	BaseType_t xWorkIsDone; 		/* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xPriorityIsSet; 	/* pdTRUE if the priority is assigned. */
		BaseType_t xInUse; 			/* pdFALSE if this extended TCB is empty. */
	#endif

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		BaseType_t xExecutedOnce;	/* pdTRUE if the task has executed once. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 || schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		TickType_t xAbsoluteUnblockTime; /* The task will be unblocked at this time if it is blocked by the scheduler task. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME || schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		BaseType_t xSuspended; 		/* pdTRUE if the task is suspended. */
		BaseType_t xMaxExecTimeExceeded; /* pdTRUE when execTime exceeds maxExecTime. */
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
	/* add if you need anything else */	
	
} SchedTCB_t;



#if( schedUSE_TCB_ARRAY == 1 )
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle );
	static void prvInitTCBArray( void );
	/* Find index for an empty entry in xTCBArray. Return -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void );
	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex );
#endif /* schedUSE_TCB_ARRAY */

static TickType_t xSystemStartTime = 0;

static void prvPeriodicTaskCode( void *pvParameters );
static void prvCreateAllTasks( void );


#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS)
	static void prvSetFixedPriorities( void );	
#endif /* schedSCHEDULING_POLICY_RMS */

#if( schedUSE_SCHEDULER_TASK == 1 )
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB );
	static void prvSchedulerFunction( void* pvParameter );
	static void prvCreateSchedulerTask( void );
	static void prvWakeScheduler( void );

	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB );
		static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount );
		static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount );				
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
		static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask );
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	
#endif /* schedUSE_SCHEDULER_TASK */



#if( schedUSE_TCB_ARRAY == 1 )
	/* Array for extended TCBs. */
	static SchedTCB_t xTCBArray[ schedMAX_NUMBER_OF_PERIODIC_TASKS ] = { 0 };
	/* Counter for number of periodic tasks. */
	static BaseType_t xTaskCounter = 0;
#endif /* schedUSE_TCB_ARRAY */

#if( schedUSE_SCHEDULER_TASK )
	static TickType_t xSchedulerWakeCounter = 0; /* useful. why? */
	static TaskHandle_t xSchedulerHandle = NULL; /* useful. why? */
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_TCB_ARRAY == 1 )
	/* Returns index position in xTCBArray of TCB with same task handle as parameter. */
	static BaseType_t prvGetTCBIndexFromHandle( TaskHandle_t xTaskHandle )
	{
		static BaseType_t xIndex = 0;
		BaseType_t xIterator;

		for( xIterator = 0; xIterator < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIterator++ )
		{
		
			if( pdTRUE == xTCBArray[ xIndex ].xInUse && *xTCBArray[ xIndex ].pxTaskHandle == xTaskHandle )
			{
				return xIndex;
			}
		
			xIndex++;
			if( schedMAX_NUMBER_OF_PERIODIC_TASKS == xIndex )
			{
				xIndex = 0;
			}
		}
		return -1;
	}

	/* Initializes xTCBArray. */
	static void prvInitTCBArray( void )
	{
	    UBaseType_t uxIndex;
		for( uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
		{
			xTCBArray[ uxIndex ].xInUse = pdFALSE;
		}
	}

	/* Find index for an empty entry in xTCBArray. Returns -1 if there is no empty entry. */
	static BaseType_t prvFindEmptyElementIndexTCB( void )
	{
		UBaseType_t uxIndex;
		for( uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
		{
			if (xTCBArray[uxIndex].xInUse == pdFALSE) {
                return uxIndex;
            }
		}
        return -1;
	}

	/* Remove a pointer to extended TCB from xTCBArray. */
	static void prvDeleteTCBFromArray( BaseType_t xIndex )
	{
        // Serial.println("b");
        // configASSERT(xIndex >= 0 && xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS );

		// if(xTCBArray[xIndex].xInUse)
		// {
		// 	xTCBArray[xIndex].xInUse = pdFALSE;
		// 	xTaskCounter--;
		// }
        vTaskDelete(*xTCBArray[ xIndex ].pxTaskHandle);
		xTCBArray[ xIndex ].xInUse = pdFALSE;
        xTaskCounter--;
	}
	
#endif /* schedUSE_TCB_ARRAY */


/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode( void *pvParameters )
{
	SchedTCB_t *pxThisTask;	
	TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();  
	
    /* Check the handle is not NULL. */
    if (xCurrentTaskHandle == NULL) return;
	
    /* If required, use the handle to obtain further information about the task. */
    /* You may find the following code helpful...*/
    BaseType_t xIndex;
	for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
	{
		if (*xTCBArray[xIndex].pxTaskHandle == xCurrentTaskHandle ) {
			pxThisTask = &xTCBArray[xIndex];
			break;
		}
	}
    
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
        Serial.println("[prvPeriodicTaskCode] Calling prvSchedulerCheckTimingError");
        Serial.flush();
        prvCheckDeadline(pxThisTask, xTaskGetTickCount());
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
    
	if( 0 == pxThisTask->xReleaseTime ) {
		pxThisTask->xLastWakeTime = xSystemStartTime;
	}

	for( ; ; )
	{	
		/* Execute the task function specified by the user. */
        Serial.print("Starting task: ");Serial.print(pxThisTask->pcName);
        Serial.print(" with priority ");Serial.println(pxThisTask->uxPriority);
        Serial.flush();

		pxThisTask->pvTaskCode( pvParameters );
				
        TOTAL_EXECUTION_TIME = TOTAL_EXECUTION_TIME += pxThisTask->pvParameters;
        Serial.print("Finished task: ");Serial.print(pxThisTask->pcName);
        Serial.print(" with priority ");Serial.println(pxThisTask->uxPriority);
        TickType_t xTickCount = xTaskGetTickCount();
        Serial.print("Tick count at finish is: ");
        Serial.println(xTickCount);
        Serial.flush();



		pxThisTask->xExecTime = 0;   
        pxThisTask->xAbsoluteDeadline = pxThisTask->xAbsoluteDeadline + pxThisTask->xPeriod;
        
		xTaskDelayUntil(&pxThisTask->xLastWakeTime, pxThisTask->xPeriod);
	}
}

/* Creates a periodic task. */
void vSchedulerPeriodicTaskCreate( TaskFunction_t pvTaskCode, const char *pcName, UBaseType_t uxStackDepth, void *pvParameters, UBaseType_t uxPriority,
		TaskHandle_t *pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick )
{
	taskENTER_CRITICAL();
	SchedTCB_t *pxNewTCB;
	
	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex = prvFindEmptyElementIndexTCB();
		configASSERT( xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS );
		configASSERT( xIndex != -1 );
		pxNewTCB = &xTCBArray[ xIndex ];	
	#endif /* schedUSE_TCB_ARRAY */

	/* Intialize item. */  
	pxNewTCB->pvTaskCode = pvTaskCode;
	pxNewTCB->pcName = pcName;
	pxNewTCB->uxStackDepth = uxStackDepth;
	pxNewTCB->pvParameters = pvParameters;
	pxNewTCB->uxPriority = uxPriority;
	pxNewTCB->pxTaskHandle = pxCreatedTask;
	pxNewTCB->xReleaseTime = xPhaseTick;
	pxNewTCB->xPeriod = xPeriodTick;
	
    /* Populate the rest */
    // pxNewTCB->xRelativeDeadline = xDeadlineTick;
    // pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
    // pxNewTCB->xExecTime = 0;
    // pxNewTCB->xWorkIsDone = pdFALSE; // TODO, check if this should be true or false
    pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
    pxNewTCB->xAbsoluteDeadline = xDeadlineTick;


	#if( schedUSE_TCB_ARRAY == 1 )
		pxNewTCB->xInUse = pdTRUE;
	#endif /* schedUSE_TCB_ARRAY */
	

	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS)
		/* member initialization */
        pxNewTCB->xPriorityIsSet = pdFALSE;
        // prvSetFixedPriorities();
	#endif /* schedSCHEDULING_POLICY */

    // below add for DMS
    #if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
		/* member initialization */
        pxNewTCB->xPriorityIsSet = pdFALSE;
	#endif /* schedSCHEDULING_POLICY */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
		/* member initialization */
        pxNewTCB->xExecutedOnce = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	
	#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
        pxNewTCB->xSuspended = pdFALSE;
        pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */	

	#if( schedUSE_TCB_ARRAY == 1 )
		xTaskCounter++;	
	#endif /* schedUSE_TCB_ARRAY */
	taskEXIT_CRITICAL();
  //Serial.println(pxNewTCB->xMaxExecTime);
}

/* Deletes a periodic task. */
void vSchedulerPeriodicTaskDelete( TaskHandle_t xTaskHandle )
{
	// TODO, can prob refactor to get rid of the #if statements
    BaseType_t xIndex = prvGetTCBIndexFromHandle(xTaskHandle);
	if (xIndex != -1) {
		prvDeleteTCBFromArray(xIndex);
	}
	vTaskDelete( xTaskHandle );
}

/* Creates all periodic tasks stored in TCB array, or TCB list. */
static void prvCreateAllTasks( void )
{
	SchedTCB_t *pxTCB;

	#if( schedUSE_TCB_ARRAY == 1 )
		BaseType_t xIndex;
		for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
		{
			configASSERT( pdTRUE == xTCBArray[ xIndex ].xInUse );
			pxTCB = &xTCBArray[ xIndex ];

			BaseType_t xReturnValue = xTaskCreate(prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth,
                                                pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle);
		}	
	#endif /* schedUSE_TCB_ARRAY */
}

#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
	/* Initiazes fixed priorities of all periodic tasks with respect to RMS policy. */
static void prvSetFixedPriorities( void )
{
	BaseType_t xIter, xIndex;
	TickType_t xShortest, xPreviousShortest=0;
	SchedTCB_t *pxShortestTaskPointer, *pxTCB;

	#if( schedUSE_SCHEDULER_TASK == 1 )
		BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY+1; 
	#else
		BaseType_t xHighestPriority = configMAX_PRIORITIES;
	#endif /* schedUSE_SCHEDULER_TASK */

	for( xIter = 0; xIter < xTaskCounter; xIter++ )
	{
		xShortest = portMAX_DELAY;

		/* search for shortest period */
		for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
		{
            pxTCB = &xTCBArray[xIndex];
            if (pdTRUE == pxTCB->xPriorityIsSet) continue; // ignore if already set

			#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS )
				// check if the current period is <= current shortest
                if (pxTCB->xPeriod < xShortest) {
                    xShortest = pxTCB->xPeriod;
                    pxShortestTaskPointer = pxTCB;
                }
            #elif(schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS) // DM condition
                // check if the current deadline is <= current shortest
                if (pxTCB->xAbsoluteDeadline < xShortest) {
                    xShortest = pxTCB->xAbsoluteDeadline;
                    pxShortestTaskPointer = pxTCB;
                }
			#endif /* schedSCHEDULING_POLICY */
		}
		
		/* set highest priority to task with xShortest period (the highest priority is configMAX_PRIORITIES-1) */	
        // TODO, check this
        if (xPreviousShortest != xShortest) {
            xHighestPriority--;
        }
        pxShortestTaskPointer->uxPriority = xHighestPriority;
        xPreviousShortest = xShortest;
        pxShortestTaskPointer->xPriorityIsSet = pdTRUE;
        Serial.print("[prvSetFixedPriorities] Task name ");Serial.print(pxShortestTaskPointer->pcName);
        Serial.print(" w/ absolute deadline ");Serial.print(pxShortestTaskPointer->xAbsoluteDeadline);
        Serial.print(" and priority ");Serial.println(pxShortestTaskPointer->uxPriority);
        Serial.flush();

        // pxShortestTaskPointer->uxPriority = configMAX_PRIORITIES - xIter;
        // pxShortestTaskPointer->xPriorityIsSet = pdTRUE;

	}
}
#endif /* schedSCHEDULING_POLICY */


#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )

	/* Recreates a deleted task that still has its information left in the task array (or list). */
	static void prvPeriodicTaskRecreate( SchedTCB_t *pxTCB )
	{
        Serial.print("Recreating task ");
        Serial.print(pxTCB->pcName);
        Serial.println("!");
        Serial.flush();
		BaseType_t xReturnValue = xTaskCreate(prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth,
                                                pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle);
				                      		 
		if( pdPASS == xReturnValue )
		{
			// pxTCB->xExecutedOnce = pdFALSE;

            // #if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
            //     pxTCB->xSuspended = pdFALSE;
            //     pxTCB->xMaxExecTimeExceeded = pdFALSE;
            // #endif
            BaseType_t xIndex;
            for (xIndex = 0; xIndex < xTaskCounter; xIndex++) {
                if (xTCBArray[xIndex].pxTaskHandle == pxTCB->pxTaskHandle) {
                    xTCBArray[xIndex].xInUse = pdTRUE;
                    break;
                }
            }
		}
		else
		{
			/* if task creation failed, do nothing */
		}
	}

	/* Called when a deadline of a periodic task is missed.
	 * Deletes the periodic task that has missed it's deadline and recreate it.
	 * The periodic task is released during next period. */
	static void prvDeadlineMissedHook( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{
        Serial.print("Deadline missed! Task ");
        Serial.println(pxTCB->pcName);
        Serial.flush();

		/* Delete the pxTask and recreate it. */
        Serial.print("Deleting task ");
        Serial.println(pxTCB->pcName);
        Serial.flush();

		vTaskDelete(*pxTCB->pxTaskHandle);
		pxTCB->xExecTime = 0;
		prvPeriodicTaskRecreate( pxTCB );	
		
		/* Need to reset next WakeTime for correct release. */
        pxTCB->xReleaseTime = pxTCB->xLastWakeTime + pxTCB->xPeriod;
		pxTCB->xLastWakeTime = 0;
        pxTCB->xAbsoluteDeadline = pxTCB->xAbsoluteDeadline + pxTCB->xReleaseTime;
		
	}

	/* Checks whether given task has missed deadline or not. */
	static void prvCheckDeadline( SchedTCB_t *pxTCB, TickType_t xTickCount )
	{ 
        
        Serial.print("[prvCheckDeadline] Task name ");Serial.print(pxTCB->pcName);
        Serial.print(" w/ absolute deadline ");Serial.print(pxTCB->xAbsoluteDeadline);
        Serial.print(" at tick count ");Serial.println(xTickCount);
        Serial.flush();
		/* check whether deadline is missed. */     		
		if (pxTCB->xAbsoluteDeadline < xTickCount) {
		    prvDeadlineMissedHook( pxTCB, xTickCount );
        }
			
	}	
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */


#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )

	/* Called if a periodic task has exceeded its worst-case execution time.
	 * The periodic task is blocked until next period. A context switch to
	 * the scheduler task occur to block the periodic task. */
	static void prvExecTimeExceedHook( TickType_t xTickCount, SchedTCB_t *pxCurrentTask )
	{
        Serial.println("[prvExecTimeExceedHook] Periodic task has exceeded its WCET");
        Serial.flush();
        pxCurrentTask->xMaxExecTimeExceeded = pdTRUE;
        /* Is not suspended yet, but will be suspended by the scheduler later. */
        pxCurrentTask->xSuspended = pdTRUE;
        pxCurrentTask->xAbsoluteUnblockTime = pxCurrentTask->xLastWakeTime + pxCurrentTask->xPeriod;
        pxCurrentTask->xExecTime = 0;
        
        BaseType_t xHigherPriorityTaskWoken;
        vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
        xTaskResumeFromISR(xSchedulerHandle);
	}
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Called by the scheduler task. Checks all tasks for any enabled
	 * Timing Error Detection feature. */
	static void prvSchedulerCheckTimingError( TickType_t xTickCount, SchedTCB_t *pxTCB )
	{
        if (pxTCB->xInUse == pdFALSE) return;

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )
			/* check if task missed deadline */
            if (xTickCount > pxTCB->xLastWakeTime) {
                pxTCB->xWorkIsDone = pdFALSE;
            }
			prvCheckDeadline( pxTCB, xTickCount );						
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
		

		#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
            if( pdTRUE == pxTCB->xMaxExecTimeExceeded )
            {
                pxTCB->xMaxExecTimeExceeded = pdFALSE;
                Serial.print("[prvSchedulerCheckTimingError] Suspending task ");
                Serial.println(pxTCB->pcName);
                Serial.flush();
                vTaskSuspend( *pxTCB->pxTaskHandle );
            }
            if( pdTRUE == pxTCB->xSuspended )
            {
                if( ( signed ) ( pxTCB->xAbsoluteUnblockTime - xTickCount ) <= 0 )
                {
                    pxTCB->xSuspended = pdFALSE;
                    pxTCB->xLastWakeTime = xTickCount;
                    Serial.print("[prvSchedulerCheckTimingError] Resuming task ");
                    Serial.println(pxTCB->pcName);
                    Serial.flush();
                    vTaskResume( *pxTCB->pxTaskHandle );
                }
            }
		#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

		return;
	}

	/* Function code for the scheduler task. */
	static void prvSchedulerFunction( void *pvParameters )
	{
		for( ; ; )
		{ 
            // TODO add for loop for scheduler overhead here

     		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
				TickType_t xTickCount = xTaskGetTickCount();
        		SchedTCB_t *pxTCB;
                BaseType_t xIndex;
				// for(xIndex = 0; xIndex < xTaskCounter; xIndex++)
				// {
				// 	if(*xTCBArray[ xIndex ].pxTaskHandle == xTaskGetCurrentTaskHandle()) {
				// 		pxTCB = &xTCBArray[ xIndex ];
                //         Serial.println("[prvSchedulerFunction] Calling prvSchedulerCheckTimingError");
                //         Serial.flush();
				// 		prvSchedulerCheckTimingError(xTickCount, pxTCB);
				// 	}
				// }


                for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
                {
                    pxTCB = &xTCBArray[ xIndex ];
                    prvSchedulerCheckTimingError( xTickCount, pxTCB );
                }
        		
                // for (BaseType_t uxIndex = 0; uxIndex < xTaskCounter; uxIndex++) {
                //     TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();	
                //     BaseType_t prioCurrentTask = uxTaskPriorityGet(xCurrentTaskHandle);
                //     pxTCB = &xTCBArray[uxIndex];
                //     Serial.print("[prvSchedulerFunction] Task here is: "); Serial.print(pxTCB->pcName); 
                //     Serial.print(" at tickcount "); Serial.println(xTickCount); 
                //     Serial.flush();

                //     Serial.print("[prvSchedulerFunction] Current task priority is: ");
                //     Serial.println(prioCurrentTask); Serial.flush();
                //     if(*xTCBArray[ uxIndex ].pxTaskHandle == xTaskGetCurrentTaskHandle()) {
				// 		Serial.print("[prvSchedulerFunction] TESTER");
				// 	}

                //     if(pxTCB->uxPriority == prioCurrentTask) {
                //     // if(pxTCB->xInUse == pdTRUE) {
                //         Serial.println("[prvSchedulerFunction] pxTCB->xInUse"); Serial.flush();
                //         Serial.println(pxTCB->pcName); Serial.flush();
                //         prvSchedulerCheckTimingError( xTickCount, pxTCB );
				// 	}
                // }	
			#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

			ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
		}
	}

	/* Creates the scheduler task. */
	static void prvCreateSchedulerTask( void )
	{
		xTaskCreate( (TaskFunction_t) prvSchedulerFunction, "Scheduler", schedSCHEDULER_TASK_STACK_SIZE, NULL, schedSCHEDULER_PRIORITY, &xSchedulerHandle );
    }
#endif /* schedUSE_SCHEDULER_TASK */


#if( schedUSE_SCHEDULER_TASK == 1 )
	/* Wakes up (context switches to) the scheduler task. */
	static void prvWakeScheduler( void )
	{
		BaseType_t xHigherPriorityTaskWoken;
		vTaskNotifyGiveFromISR( xSchedulerHandle, &xHigherPriorityTaskWoken );
		xTaskResumeFromISR(xSchedulerHandle);    
	}

	/* Called every software tick. */
	// In FreeRTOSConfig.h,
	// Enable configUSE_TICK_HOOK
	// Enable INCLUDE_xTaskGetIdleTaskHandle
	// Enable INCLUDE_xTaskGetCurrentTaskHandle
	void vApplicationTickHook( void )
	{            
		SchedTCB_t *pxCurrentTask;		
		TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();		
        UBaseType_t flag = 0;
        BaseType_t xIndex;
		BaseType_t prioCurrentTask = uxTaskPriorityGet(xCurrentTaskHandle);
        // Serial.print("[vApplicationTickHook] task priority is: ");
        // Serial.println(prioCurrentTask); 
        // Serial.flush();

		for(xIndex = 0; xIndex < xTaskCounter ; xIndex++){
			pxCurrentTask = &xTCBArray[xIndex];
			if(pxCurrentTask -> uxPriority == prioCurrentTask){
				flag = 1;
				break;
			}
		}
		
		if( xCurrentTaskHandle != xSchedulerHandle && xCurrentTaskHandle != xTaskGetIdleTaskHandle() && flag == 1)
		{
			pxCurrentTask->xExecTime++;     
     
			#if( schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 )
            if( pxCurrentTask->xMaxExecTime <= pxCurrentTask->xExecTime )
            {
                if( pdFALSE == pxCurrentTask->xMaxExecTimeExceeded )
                {
                    if( pdFALSE == pxCurrentTask->xSuspended )
                    {
                        prvExecTimeExceedHook( xTaskGetTickCountFromISR(), pxCurrentTask );
                    }
                }
            }
			#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
		}

		#if( schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 )    
			xSchedulerWakeCounter++;      
			if( xSchedulerWakeCounter == schedSCHEDULER_TASK_PERIOD )
			{
				xSchedulerWakeCounter = 0;        
				prvWakeScheduler();
			}
		#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	}
#endif /* schedUSE_SCHEDULER_TASK */

/* This function must be called before any other function call from this module. */
void vSchedulerInit( void )
{
	#if( schedUSE_TCB_ARRAY == 1 )
		prvInitTCBArray();
	#endif /* schedUSE_TCB_ARRAY */
    Serial.println("[vSchedulerInit] Initialized");
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart( void )
{
    Serial.println("[vSchedulerStart] ---------- Starting ---------- ");

	#if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
		prvSetFixedPriorities();	
	#endif /* schedSCHEDULING_POLICY */

	#if( schedUSE_SCHEDULER_TASK == 1 )
		prvCreateSchedulerTask();
	#endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();
	  
	xSystemStartTime = xTaskGetTickCount();
	
	vTaskStartScheduler();
}
