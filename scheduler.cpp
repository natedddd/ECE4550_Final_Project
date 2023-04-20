#include "scheduler.h"

#define schedUSE_TCB_ARRAY 1
#define schedUSE_SEMAPHORES 1
#define schedUSE_OCPP 1 // 1 = OCPP, 0 = ICPP


typedef struct xCriticalResource {
    SemaphoreHandle_t xSemaHandle = NULL;
    UBaseType_t resourceID;			  /* Name of the resource. */
    UBaseType_t xSemaPriority;
    // an array that keeps track of the task handles
    // of the currently blocked tasks, in order
    UBaseType_t xNumBlockedTasks;
    BaseType_t xInUse;
    TaskHandle_t xBlockedTasks[10] = {};

} xCritResource;

/* Extended Task control block for managing periodic tasks within this library. */
typedef struct xExtended_TCB
{
	TaskFunction_t pvTaskCode;	  /* Function pointer to the code that will be run periodically. */
	const char* pcName;			  /* Name of the task. */
	UBaseType_t uxStackDepth;	  /* Stack size of the task. */
	void* pvParameters;			  /* Parameters to the task function. */
	UBaseType_t uxPriority;		  /* Priority of the task. */
	TaskHandle_t* pxTaskHandle;	  /* Task handle for the task. */
	TickType_t xReleaseTime;	  /* Release time of the task. */
	TickType_t xRelativeDeadline; /* Relative deadline of the task. */
	TickType_t xAbsoluteDeadline; /* Absolute deadline of the task. */
	TickType_t xPeriod;			  /* Task period. */
	TickType_t xLastWakeTime;	  /* Last time stamp when the task was running. */
	TickType_t xMaxExecTime;	  /* Worst-case execution time of the task. */
	TickType_t xExecTime;		  /* Current execution time of the task. */
    UBaseType_t xOriginalPriority;

	BaseType_t xWorkIsDone; /* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

#if (schedUSE_TCB_ARRAY == 1)
	BaseType_t xPriorityIsSet; /* pdTRUE if the priority is assigned. */
	BaseType_t xInUse;		   /* pdFALSE if this extended TCB is empty. */
#endif

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	BaseType_t xExecutedOnce; /* pdTRUE if the task has executed once. */
#endif						  /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1 || schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	TickType_t xAbsoluteUnblockTime; /* The task will be unblocked at this time if it is blocked by the scheduler task. */
#endif								 /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME || schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
	BaseType_t xSuspended;			 /* pdTRUE if the task is suspended. */
	BaseType_t xMaxExecTimeExceeded; /* pdTRUE when execTime exceeds maxExecTime. */
#endif								 /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

	/* add if you need anything else */

} SchedTCB_t;

#if (schedUSE_TCB_ARRAY == 1)
static BaseType_t prvGetTCBIndexFromHandle(TaskHandle_t xTaskHandle);
static void prvInitTCBArray(void);
/* Find index for an empty entry in xTCBArray. Return -1 if there is no empty entry. */
static BaseType_t prvFindEmptyElementIndexTCB(void);
/* Remove a pointer to extended TCB from xTCBArray. */
static void prvDeleteTCBFromArray(BaseType_t xIndex);
#endif /* schedUSE_TCB_ARRAY */

static TickType_t xSystemStartTime = 0;

static void prvPeriodicTaskCode(void* pvParameters);
static void prvCreateAllTasks(void);

#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
static void prvSetFixedPriorities(void);
#endif /* schedSCHEDULING_POLICY_RMS */

#if (schedUSE_SCHEDULER_TASK == 1)
static void prvSchedulerCheckTimingError(TickType_t xTickCount, SchedTCB_t* pxTCB);
static void prvSchedulerFunction(void);
static void prvCreateSchedulerTask(void);
static void prvWakeScheduler(void);

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
    static void prvPeriodicTaskRecreate(SchedTCB_t* pxTCB);
    static void prvDeadlineMissedHook(SchedTCB_t* pxTCB, TickType_t xTickCount);
    static void prvCheckDeadline(SchedTCB_t* pxTCB, TickType_t xTickCount);
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
static void prvExecTimeExceedHook(TickType_t xTickCount, SchedTCB_t* pxCurrentTask);
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

#endif /* schedUSE_SCHEDULER_TASK */

#if (schedUSE_TCB_ARRAY == 1)
    /* Array for extended TCBs. */
    static SchedTCB_t xTCBArray[schedMAX_NUMBER_OF_PERIODIC_TASKS] = { 0 };
    /* Counter for number of periodic tasks. */
    static BaseType_t xTaskCounter = 0;
#endif /* schedUSE_TCB_ARRAY */

#if (schedUSE_SEMAPHORES == 1)
    /* Array for critical resources. */
    static xCritResource xCriticalResourceArray[schedMAX_NUM_CRIT_RESOURCES] = { }; // TODO, if doesn't work
#endif

#if (schedUSE_SCHEDULER_TASK)
static TickType_t xSchedulerWakeCounter = 0; /* useful. why? */
static TaskHandle_t xSchedulerHandle = NULL; /* useful. why? */
#endif										 /* schedUSE_SCHEDULER_TASK */

#if (schedUSE_TCB_ARRAY == 1)
/* Returns index position in xTCBArray of TCB with same task handle as parameter. */
static BaseType_t prvGetTCBIndexFromHandle(TaskHandle_t xTaskHandle)
{
	static BaseType_t xIndex = 0;
	BaseType_t xIterator;

	for (xIterator = 0; xIterator < schedMAX_NUMBER_OF_PERIODIC_TASKS; xIterator++)
	{

		if (pdTRUE == xTCBArray[xIndex].xInUse && *xTCBArray[xIndex].pxTaskHandle == xTaskHandle)
		{
			return xIndex;
		}

		xIndex++;
		if (schedMAX_NUMBER_OF_PERIODIC_TASKS == xIndex)
		{
			xIndex = 0;
		}
	}
	return -1;
}

/* Initializes xTCBArray. */
static void prvInitTCBArray(void)
{
	UBaseType_t uxIndex;
	for (uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
	{
		xTCBArray[uxIndex].xInUse = pdFALSE;
		xTCBArray[uxIndex].pxTaskHandle = NULL;
	}
}

/* Initializes xCriticalResourceArray. */
static void prvInitCriticalResourceArray(void)
{
    UBaseType_t uxIndex;
    for (uxIndex = 0; uxIndex < schedMAX_NUM_CRIT_RESOURCES; uxIndex++)
    {
        xCriticalResourceArray[uxIndex].xSemaHandle = xSemaphoreCreateMutex();
        xCriticalResourceArray[uxIndex].resourceID = uxIndex+1;
        xCriticalResourceArray[uxIndex].xInUse = pdFALSE;
        xCriticalResourceArray[uxIndex].xNumBlockedTasks = 0;
    }
}

static void updateCriticalResourcePriority(int critResource[], int numResources,  UBaseType_t taskPriority) {
    if (numResources == 0) return;
    for (UBaseType_t uxIndex = 0; uxIndex < numResources; uxIndex++) {
        UBaseType_t resourceIndex = critResource[uxIndex];

        xCriticalResource *xResource = &xCriticalResourceArray[resourceIndex-1];
        if (xResource->xSemaPriority < taskPriority) {
            xResource->xSemaPriority = taskPriority;
        }
        // Serial.print("Semaphore ");
        // Serial.print(xResource->resourceID);
        // Serial.print(" priority is ");
        // Serial.println(xResource->xSemaPriority);
        // Serial.flush();
    } 
}

/* Find index for an empty entry in xTCBArray. Returns -1 if there is no empty entry. */
static BaseType_t prvFindEmptyElementIndexTCB(void)
{
	/* your implementation goes here */
	UBaseType_t uxIndex;
	for (uxIndex = 0; uxIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS; uxIndex++)
	{
		if (xTCBArray[uxIndex].xInUse == pdFALSE)
			return uxIndex;
	}
	return -1;
}

/* Remove a pointer to extended TCB from xTCBArray. */
static void prvDeleteTCBFromArray(BaseType_t xIndex)
{
	/* your implementation goes here */
	if (xIndex >= 0 && xIndex < schedMAX_NUMBER_OF_PERIODIC_TASKS)
	{
		if (xTCBArray[xIndex].xInUse == pdTRUE)
		{
			xTCBArray[xIndex].xInUse == pdFALSE;
			xTaskCounter--;
		}
	}
}

#endif /* schedUSE_TCB_ARRAY */

/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode(void* pvParameters)
{

	SchedTCB_t* pxThisTask;
	TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();

	/* your implementation goes here */
	/* Check the handle is not NULL. */
	configASSERT(xCurrentTaskHandle != NULL);
	BaseType_t xIndex = prvGetTCBIndexFromHandle(xCurrentTaskHandle);
	if (xIndex < 0)
	{
		Serial.print("Invalid index\n");
		Serial.flush();
	}
	configASSERT(xIndex < 0);
	pxThisTask = &xTCBArray[xIndex];

	if (pxThisTask->xReleaseTime != 0)
	{
		xTaskDelayUntil(&pxThisTask->xLastWakeTime, pxThisTask->xReleaseTime);
	}
	/* If required, use the handle to obtain further information about the task. */
	/* You may find the following code helpful...
	BaseType_t xIndex;
	for( xIndex = 0; xIndex < xTaskCounter; xIndex++ )
	{

	}
	*/

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	/* your implementation goes here */
	pxThisTask->xExecutedOnce = pdTRUE;
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

	if (0 == pxThisTask->xReleaseTime)
	{
		pxThisTask->xLastWakeTime = xSystemStartTime;
	}

	for (;;)
	{
		/* Execute the task function specified by the user. */
        Serial.println();
        Serial.print("Starting task ");
		Serial.print(pxThisTask->pcName);
		Serial.print(" - ");
		Serial.print(xTaskGetTickCount());
		Serial.print("\n");
		Serial.flush();

		pxThisTask->xWorkIsDone = pdFALSE;
		pxThisTask->xExecTime = 0;
		pxThisTask->pvTaskCode(pvParameters);

		pxThisTask->xWorkIsDone = pdTRUE;
		pxThisTask->xExecTime = 0;

		xTaskDelayUntil(&pxThisTask->xLastWakeTime, pxThisTask->xPeriod);
	}
}

/* Creates a periodic task. */
void vSchedulerPeriodicTaskCreate(TaskFunction_t pvTaskCode, const char* pcName, UBaseType_t uxStackDepth, void* pvParameters, UBaseType_t uxPriority,
	TaskHandle_t* pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick)
{
	taskENTER_CRITICAL();
	SchedTCB_t* pxNewTCB;

#if (schedUSE_TCB_ARRAY == 1)
	BaseType_t xIndex = prvFindEmptyElementIndexTCB();
	configASSERT(xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS);
	configASSERT(xIndex != -1);
	pxNewTCB = &xTCBArray[xIndex];
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
	/* your implementation goes here */
	pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
	pxNewTCB->xRelativeDeadline = xDeadlineTick;
	pxNewTCB->xAbsoluteDeadline = xPhaseTick + xDeadlineTick;
	pxNewTCB->xLastWakeTime = 0;
	pxNewTCB->xExecTime = 0;
	pxNewTCB->xWorkIsDone = pdTRUE;
	pxNewTCB->xPriorityIsSet = pdTRUE;

#if (schedUSE_TCB_ARRAY == 1)
	pxNewTCB->xInUse = pdTRUE;
#endif /* schedUSE_TCB_ARRAY */

#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
	/* member initialization */
	/* your implementation goes here */
	pxNewTCB->xPriorityIsSet = pdFALSE;
#endif /* schedSCHEDULING_POLICY */

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	/* member initialization */
	/* your implementation goes here */
	pxNewTCB->xExecutedOnce = pdFALSE;
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
	pxNewTCB->xSuspended = pdFALSE;
	pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

#if (schedUSE_TCB_ARRAY == 1)
	xTaskCounter++;
#endif /* schedUSE_TCB_SORTED_LIST */
	taskEXIT_CRITICAL();
}

/* Creates a periodic task that needs a critical resources. */
void vSchedulerPeriodicTaskCreateWithResource(TaskFunction_t pvTaskCode, const char* pcName, UBaseType_t uxStackDepth, void* pvParameters, UBaseType_t uxPriority,
	TaskHandle_t* pxCreatedTask, TickType_t xPhaseTick, TickType_t xPeriodTick, TickType_t xMaxExecTimeTick, TickType_t xDeadlineTick, int critResource[], int numResources)
{
	taskENTER_CRITICAL();
	SchedTCB_t* pxNewTCB;

#if (schedUSE_TCB_ARRAY == 1)
	BaseType_t xIndex = prvFindEmptyElementIndexTCB();
	configASSERT(xTaskCounter < schedMAX_NUMBER_OF_PERIODIC_TASKS);
	configASSERT(xIndex != -1);
	pxNewTCB = &xTCBArray[xIndex];
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
	/* your implementation goes here */
	pxNewTCB->xMaxExecTime = xMaxExecTimeTick;
	pxNewTCB->xRelativeDeadline = xDeadlineTick;
	pxNewTCB->xAbsoluteDeadline = xPhaseTick + xDeadlineTick;
	pxNewTCB->xLastWakeTime = 0;
	pxNewTCB->xExecTime = 0;
	pxNewTCB->xWorkIsDone = pdTRUE;
	pxNewTCB->xPriorityIsSet = pdTRUE;

    // update semaphore ceilings
    updateCriticalResourcePriority(critResource, numResources, uxPriority);

#if (schedUSE_TCB_ARRAY == 1)
	pxNewTCB->xInUse = pdTRUE;
#endif /* schedUSE_TCB_ARRAY */

#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
	/* member initialization */
	/* your implementation goes here */
	pxNewTCB->xPriorityIsSet = pdFALSE;
#endif /* schedSCHEDULING_POLICY */

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	/* member initialization */
	/* your implementation goes here */
	pxNewTCB->xExecutedOnce = pdFALSE;
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
	pxNewTCB->xSuspended = pdFALSE;
	pxNewTCB->xMaxExecTimeExceeded = pdFALSE;
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

#if (schedUSE_TCB_ARRAY == 1)
	xTaskCounter++;
#endif /* schedUSE_TCB_SORTED_LIST */
	taskEXIT_CRITICAL();
}


/* Deletes a periodic task. */
void vSchedulerPeriodicTaskDelete(TaskHandle_t xTaskHandle)
{
	/* your implementation goes here */
	prvDeleteTCBFromArray(prvGetTCBIndexFromHandle(xTaskHandle));

	vTaskDelete(xTaskHandle);
}

/* Creates all periodic tasks stored in TCB array, or TCB list. */
static void prvCreateAllTasks(void)
{
	SchedTCB_t* pxTCB;

#if (schedUSE_TCB_ARRAY == 1)
	BaseType_t xIndex;
	for (xIndex = 0; xIndex < xTaskCounter; xIndex++)
	{
		configASSERT(pdTRUE == xTCBArray[xIndex].xInUse);
		pxTCB = &xTCBArray[xIndex];

		BaseType_t xReturnValue = xTaskCreate(prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth,
			pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle /* your implementation goes here */);
		if (xReturnValue == pdPASS)
		{
			Serial.print(pxTCB->pcName);
			Serial.print(", Period- ");
			Serial.print(pxTCB->xPeriod);
			Serial.print(", Released at- ");
			Serial.print(pxTCB->xReleaseTime);
			Serial.print(", Priority- ");
			Serial.print(pxTCB->uxPriority);
			Serial.print(", WCET- ");
			Serial.print(pxTCB->xMaxExecTime);
			Serial.print(", Deadline- ");
			Serial.print(pxTCB->xRelativeDeadline);
			Serial.println();
			Serial.flush();
		}
		else
		{
			Serial.println("Task creation failed\n");
			Serial.flush();
		}
	}
#endif /* schedUSE_TCB_ARRAY */
}

#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
/* Initiazes fixed priorities of all periodic tasks with respect to RMS policy. */
static void prvSetFixedPriorities(void)
{
	BaseType_t xIter, xIndex;
	TickType_t xShortest, xPreviousShortest = 0;
	SchedTCB_t* pxShortestTaskPointer, * pxTCB;

#if (schedUSE_SCHEDULER_TASK == 1)
	BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY;
#else
	BaseType_t xHighestPriority = configMAX_PRIORITIES;
#endif /* schedUSE_SCHEDULER_TASK */

	for (xIter = 0; xIter < xTaskCounter; xIter++)
	{
		xShortest = portMAX_DELAY;

		/* search for shortest period */
		for (xIndex = 0; xIndex < xTaskCounter; xIndex++)
		{
			/* your implementation goes here */
			if (xTCBArray[xIndex].xInUse == pdFALSE)
				continue;
			if (xTCBArray[xIndex].xPriorityIsSet == pdTRUE)
				continue;

		#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS)
			/* your implementation goes here */
			if (xShortest > xTCBArray[xIndex].xPeriod)
			{
				xShortest = xTCBArray[xIndex].xPeriod;
				pxShortestTaskPointer = &xTCBArray[xIndex];
			}
		#endif /* schedSCHEDULING_POLICY */
		#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
			if (xShortest > xTCBArray[xIndex].xRelativeDeadline)
			{
				xShortest = xTCBArray[xIndex].xRelativeDeadline;
				pxShortestTaskPointer = &xTCBArray[xIndex];
			}
		#endif /* schedSCHEDULING_POLICY */
		}

		/* set highest priority to task with xShortest period (the highest priority is configMAX_PRIORITIES-1) */

		/* your implementation goes here */
		if (xShortest != xPreviousShortest)
		{
			if (xHighestPriority > 0)
			{
				xHighestPriority--;
			}
			else
			{
				xHighestPriority = 0;
			}
		}
		configASSERT(0 <= xHighestPriority);
		pxShortestTaskPointer->uxPriority = xHighestPriority;
		pxShortestTaskPointer->xPriorityIsSet = pdTRUE;
		xPreviousShortest = xShortest;
        pxShortestTaskPointer->xOriginalPriority = xHighestPriority;
	}
}
#endif /* schedSCHEDULING_POLICY */

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)

/* Recreates a deleted task that still has its information left in the task array (or list). */
static void prvPeriodicTaskRecreate(SchedTCB_t* pxTCB)
{
	BaseType_t xReturnValue = xTaskCreate(prvPeriodicTaskCode, pxTCB->pcName, pxTCB->uxStackDepth,
		pxTCB->pvParameters, pxTCB->uxPriority, pxTCB->pxTaskHandle /* your implementation goes here */);

	if (pdPASS == xReturnValue)
	{
		/* your implementation goes here */
	#if (schedUSE_TCB_ARRAY == 1)
		pxTCB->xInUse = pdTRUE;
	#endif /* schedUSE_TCB_ARRAY */
	#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
		/* member initialization */
		/* your implementation goes here */
		pxTCB->xExecutedOnce = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
	#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
		pxTCB->xSuspended = pdFALSE;
		pxTCB->xMaxExecTimeExceeded = pdFALSE;
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	}
	else
	{
		/* if task creation failed */
		Serial.println("Task Recreation failed");
		Serial.flush();
	}

	Serial.print(pxTCB->pcName);
	Serial.print(" recreated - ");
	Serial.print(xTaskGetTickCount());
	Serial.print("\n");
	Serial.flush();
}

/* Called when a deadline of a periodic task is missed.
 * Deletes the periodic task that has missed it's deadline and recreate it.
 * The periodic task is released during next period. */
static void prvDeadlineMissedHook(SchedTCB_t* pxTCB, TickType_t xTickCount)
{

	Serial.print("Deadline missed - ");
	Serial.print(pxTCB->pcName);
	Serial.print(" - ");
	Serial.println(xTaskGetTickCount());
	Serial.flush();

	/* Delete the pxTask and recreate it. */
	vTaskDelete(*pxTCB->pxTaskHandle /* your implementation goes here */);
	pxTCB->xWorkIsDone = pdFALSE;
	pxTCB->xExecTime = 0;
	prvPeriodicTaskRecreate(pxTCB);

	/* Need to reset next WakeTime for correct release. */
	/* your implementation goes here */
	pxTCB->xReleaseTime = pxTCB->xLastWakeTime + pxTCB->xPeriod;
	pxTCB->xLastWakeTime = 0;
	pxTCB->xAbsoluteDeadline = pxTCB->xReleaseTime + pxTCB->xRelativeDeadline;
}

/* Checks whether given task has missed deadline or not. */
static void prvCheckDeadline(SchedTCB_t* pxTCB, TickType_t xTickCount)
{
	/* check whether deadline is missed. */
	/* your implementation goes here */
	if (pxTCB != NULL && pxTCB->xWorkIsDone == pdFALSE && pxTCB->xExecutedOnce == pdTRUE)
	{
		pxTCB->xAbsoluteDeadline = pxTCB->xLastWakeTime + pxTCB->xRelativeDeadline;
		if ((signed)(pxTCB->xAbsoluteDeadline - xTickCount) <= 0)
			prvDeadlineMissedHook(pxTCB, xTickCount);
	}
}
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)

/* Called if a periodic task has exceeded its worst-case execution time.
 * The periodic task is blocked until next period. A context switch to
 * the scheduler task occur to block the periodic task. */
static void prvExecTimeExceedHook(TickType_t xTickCount, SchedTCB_t* pxCurrentTask)
{
	Serial.print(pxCurrentTask->pcName);
	Serial.print(" Exceeded - ");
	Serial.println(xTaskGetTickCount());
	Serial.flush();

	pxCurrentTask->xMaxExecTimeExceeded = pdTRUE;
	/* Is not suspended yet, but will be suspended by the scheduler later. */
	pxCurrentTask->xSuspended = pdTRUE;
	pxCurrentTask->xAbsoluteUnblockTime = pxCurrentTask->xLastWakeTime + pxCurrentTask->xPeriod;
	pxCurrentTask->xExecTime = 0;

	BaseType_t xHigherPriorityTaskWoken;
	vTaskNotifyGiveFromISR(xSchedulerHandle, &xHigherPriorityTaskWoken);
	xTaskResumeFromISR(xSchedulerHandle);
}
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

#if (schedUSE_SCHEDULER_TASK == 1)
/* Called by the scheduler task. Checks all tasks for any enabled
 * Timing Error Detection feature. */
static void prvSchedulerCheckTimingError(TickType_t xTickCount, SchedTCB_t* pxTCB)
{
	/* your implementation goes here */
	if (pxTCB->xInUse == pdFALSE)
		return;

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	/* check if task missed deadline */
	/* your implementation goes here */
	if ((signed)(xTickCount - pxTCB->xLastWakeTime) > 0)
		pxTCB->xWorkIsDone = pdFALSE;

	prvCheckDeadline(pxTCB, xTickCount);
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */

#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
	if (pdTRUE == pxTCB->xMaxExecTimeExceeded)
	{
		pxTCB->xMaxExecTimeExceeded = pdFALSE;
		vTaskSuspend(*pxTCB->pxTaskHandle);
		Serial.print(pxTCB->pcName);
		Serial.print(" suspended - ");
		Serial.print(xTaskGetTickCount());
		Serial.print("\n");
		Serial.flush();
	}
	if (pdTRUE == pxTCB->xSuspended)
	{
		if ((signed)(pxTCB->xAbsoluteUnblockTime - xTickCount) < 0)
		{
			pxTCB->xSuspended = pdFALSE;
			pxTCB->xLastWakeTime = xTickCount;
			vTaskResume(*pxTCB->pxTaskHandle);
			Serial.print(pxTCB->pcName);
			Serial.print(" resumed - ");
			Serial.print(xTaskGetTickCount());
			Serial.print("\n");
			Serial.flush();
		}
	}
#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

	return;
}

/* Function code for the scheduler task. */
static void prvSchedulerFunction(void* pvParameters)
{

#if (schedAUGMENT_SCHEDULER_TASK == 1)
	volatile int i, j;
#endif

	for (;;)
	{
	#if (schedAUGMENT_SCHEDULER_TASK == 1)
		for (j = 0; j < 50; j++)
		{
			for (i = 0; i < 1000; i++)
			{
			}
		}
	#endif

	#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1 || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
		TickType_t xTickCount = xTaskGetTickCount();
		SchedTCB_t* pxTCB;

		/* your implementation goes here. */
		/* You may find the following helpful...
			prvSchedulerCheckTimingError( xTickCount, pxTCB );
		*/
	#if (schedUSE_TCB_ARRAY == 1)
		BaseType_t xIndex;
		for (xIndex = 0; xIndex < xTaskCounter; xIndex++)
		{
			pxTCB = &xTCBArray[xIndex];
			if ((pxTCB) && (pxTCB->xInUse == pdTRUE) && (pxTCB->pxTaskHandle != NULL))
			{
				prvSchedulerCheckTimingError(xTickCount, pxTCB);
			}
		}
	#endif

	#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE || schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */

		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	}
}

/* Creates the scheduler task. */
static void prvCreateSchedulerTask(void)
{
	xTaskCreate((TaskFunction_t)prvSchedulerFunction, "Scheduler", schedSCHEDULER_TASK_STACK_SIZE, NULL, schedSCHEDULER_PRIORITY, &xSchedulerHandle);
}
#endif /* schedUSE_SCHEDULER_TASK */

#if (schedUSE_SCHEDULER_TASK == 1)
/* Wakes up (context switches to) the scheduler task. */
static void prvWakeScheduler(void)
{
	BaseType_t xHigherPriorityTaskWoken;
	vTaskNotifyGiveFromISR(xSchedulerHandle, &xHigherPriorityTaskWoken);
	xTaskResumeFromISR(xSchedulerHandle);
}

/* Called every software tick. */
// In FreeRTOSConfig.h,
// Enable configUSE_TICK_HOOK
// Enable INCLUDE_xTaskGetIdleTaskHandle
// Enable INCLUDE_xTaskGetCurrentTaskHandle

void vApplicationTickHook(void)
{
	SchedTCB_t* pxCurrentTask;
	TaskHandle_t xCurrentTaskHandle = xTaskGetCurrentTaskHandle();
	UBaseType_t flag = 0;
	BaseType_t xIndex;
	BaseType_t prioCurrentTask = uxTaskPriorityGet(xCurrentTaskHandle);

	for (xIndex = 0; xIndex < xTaskCounter; xIndex++)
	{
		pxCurrentTask = &xTCBArray[xIndex];
		if (pxCurrentTask->uxPriority == prioCurrentTask)
		{
			flag = 1;
			break;
		}
	}
	if (xCurrentTaskHandle != xSchedulerHandle && xCurrentTaskHandle != xTaskGetIdleTaskHandle() && flag == 1)
	{
		pxCurrentTask->xExecTime++;

	#if (schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME == 1)
		if (pxCurrentTask->xMaxExecTime < pxCurrentTask->xExecTime)
		{
			if (pdFALSE == pxCurrentTask->xMaxExecTimeExceeded)
			{
				if (pdFALSE == pxCurrentTask->xSuspended)
				{
					prvExecTimeExceedHook(xTaskGetTickCountFromISR(), pxCurrentTask);
				}
			}
		}
	#endif /* schedUSE_TIMING_ERROR_DETECTION_EXECUTION_TIME */
	}

#if (schedUSE_TIMING_ERROR_DETECTION_DEADLINE == 1)
	xSchedulerWakeCounter++;
	if (xSchedulerWakeCounter == schedSCHEDULER_TASK_PERIOD)
	{
		xSchedulerWakeCounter = 0;
		prvWakeScheduler();
	}
#endif /* schedUSE_TIMING_ERROR_DETECTION_DEADLINE */
}
#endif /* schedUSE_SCHEDULER_TASK */



// vSchedulerResourceWait pass resource index, handles priority of tasks that are running or requesting the resource
// vSchedulerResourceRelease release the resource

void vSchedulerResourceWait(BaseType_t xIndex, const char* pcName) {

    xCriticalResource *xResource = &xCriticalResourceArray[xIndex-1];
    SemaphoreHandle_t xSemaphoreHandle = xResource->xSemaHandle;
    TaskHandle_t xCurrentTaskHandle = xTaskGetHandle(pcName);
    configASSERT(xCurrentTaskHandle != NULL);
        
    BaseType_t index = prvGetTCBIndexFromHandle(xCurrentTaskHandle);
    SchedTCB_t* pxCurrentTask = &xTCBArray[index];

    Serial.print("Attempting to acquire semaphore ");
    Serial.print(xResource->resourceID); Serial.print(" for task ");
    Serial.println(pxCurrentTask->pcName); Serial.flush();
    
    // for OCPP, task priority stays the same until a task is blocked
    // by current task, then task's priority of highest blocked task
    #if (schedUSE_OCPP) 
        // get the max priority ceiling of all currently locked semaphores
        UBaseType_t maxPriority = 0;
        UBaseType_t maxIndex = 0;
        for (UBaseType_t uxIndex = 0; uxIndex < schedMAX_NUM_CRIT_RESOURCES; uxIndex++)
        {
            if (xCriticalResourceArray[uxIndex].xInUse) {
                if (xCriticalResourceArray[uxIndex].xSemaPriority > maxPriority) {
                    maxPriority = xCriticalResourceArray[uxIndex].xSemaPriority;
                    maxIndex = uxIndex;
                }
            }
        }
        // Serial.print("max priority ");
        // Serial.println(maxPriority); Serial.flush();
        // Serial.print("Current task priority ");
        // Serial.println(uxTaskPriorityGet(xCurrentTaskHandle)); Serial.flush();

        // if less than, need to suspend task
        if (uxTaskPriorityGet(xCurrentTaskHandle) <= maxPriority) {
            xCriticalResource *tempResource = &xCriticalResourceArray[maxIndex];
            tempResource->xBlockedTasks[tempResource->xNumBlockedTasks] = xCurrentTaskHandle;
            tempResource->xNumBlockedTasks++;
            Serial.print("Unable to acquire! The priority is not high enough. Suspending task ");
            Serial.println(pxCurrentTask->pcName);
            Serial.flush();
            Serial.print("Resource ");
            Serial.print(tempResource->resourceID);
            Serial.print(" now blocks ");
            Serial.print(tempResource->xNumBlockedTasks);
            Serial.println(" tasks!"); Serial.flush();

            vTaskSuspend(xCurrentTaskHandle);
        } else {
            // get the max of xBlockedTasks.priority
            UBaseType_t maxPriority = uxTaskPriorityGet(xCurrentTaskHandle);

            for (UBaseType_t uxIndex = 0; uxIndex < xResource->xNumBlockedTasks; uxIndex++)
            {
                // get the current task
                BaseType_t xIndex = prvGetTCBIndexFromHandle(xResource->xBlockedTasks[uxIndex]);
                SchedTCB_t* pxCurrentTask = &xTCBArray[xIndex];

                // find the highest prioirty of all blocked tasks
                if (uxTaskPriorityGet(xCurrentTaskHandle) > maxPriority) {
                    maxPriority = pxCurrentTask->uxPriority;
                }
            }
            vTaskPrioritySet(xCurrentTaskHandle, maxPriority);

        }
    #else
        Serial.print("Current task priority before is ");
        Serial.println(uxTaskPriorityGet(xCurrentTaskHandle)); Serial.flush();
        // if resource is not available, still need to suspend ourselves
        if (xResource->xInUse == pdFALSE) {
            vTaskPrioritySet(xCurrentTaskHandle, xResource->xSemaPriority);
            Serial.print("Current task priority after is ");
            Serial.println(uxTaskPriorityGet(xCurrentTaskHandle)); Serial.flush();
        } else {
            xResource->xBlockedTasks[xResource->xNumBlockedTasks] = xCurrentTaskHandle;
            xResource->xNumBlockedTasks++;
            Serial.print("Unable to acquire! Suspending task ");
            Serial.println(pxCurrentTask->pcName);
            Serial.flush();
            vTaskSuspend(xCurrentTaskHandle);
        }
    #endif
    

    // Serial.print("Attempting to acquire semaphore ");
    // Serial.println(xResource->resourceID); Serial.flush();

    // See if we can obtain the semaphore.  If the semaphore is not available, wait
    if( xSemaphoreTake( xSemaphoreHandle, ( TickType_t ) 10 ) == pdTRUE ) { 
        // We were able to obtain the semaphore and can now access the
        // shared resource.
        xResource->xInUse = pdTRUE;
        Serial.print("Semaphore ");
        Serial.print(xResource->resourceID);
        Serial.println(" acquired!");
        Serial.flush();
    } else {
        Serial.println("Did not acquire semaphore");
    }
}

void vSchedulerResourceRelease(BaseType_t xIndex, const char* pcName) {

    xCriticalResource *xResource = &xCriticalResourceArray[xIndex-1];
    SemaphoreHandle_t xSemaphoreHandle = xResource->xSemaHandle;
    TaskHandle_t xCurrentTaskHandle = xTaskGetHandle(pcName);
    configASSERT(xCurrentTaskHandle != NULL);
    
    BaseType_t uxIndex = prvGetTCBIndexFromHandle(xCurrentTaskHandle);
    SchedTCB_t* pxCurrentTask = &xTCBArray[uxIndex];


    Serial.print("Requesting to release semaphore ");
    Serial.print(xResource->resourceID);
    Serial.print(" for task ");
    Serial.println(pxCurrentTask->pcName);
    Serial.flush();

    // unlock semaphore
    xSemaphoreGive( xSemaphoreHandle );
    xResource->xInUse = pdFALSE;
    Serial.print("Semaphore ");
    Serial.print(xIndex);
    Serial.println(" released!");
    Serial.flush();


    // check if we need to resume a task
    if (xResource->xNumBlockedTasks > 0) {
        Serial.print("Num blocked tasks: ");
        Serial.print(xResource->xNumBlockedTasks);
        Serial.println("!");
        Serial.flush();
            // check the blocked task array to resume the highest priority task
        UBaseType_t maxPriority = 0;
        UBaseType_t maxPriorityIndex = 0;
        TaskHandle_t maxPriorityTaskHandle;
        
        for (UBaseType_t uxIndex = 0; uxIndex < xResource->xNumBlockedTasks; uxIndex++)
        {
            // get the current task
            BaseType_t xIndex = prvGetTCBIndexFromHandle(xResource->xBlockedTasks[uxIndex]);
            SchedTCB_t* pxCurrentTask = &xTCBArray[xIndex];

            // find the highest prioirty of all blocked tasks
            if (pxCurrentTask->uxPriority > maxPriority) {
                maxPriority = pxCurrentTask->uxPriority;
                maxPriorityIndex = uxIndex;
                maxPriorityTaskHandle = xResource->xBlockedTasks[uxIndex];
            }
        }
        // updates task's priority
        BaseType_t xIndex = prvGetTCBIndexFromHandle(maxPriorityTaskHandle);
        SchedTCB_t* pxCurrentTask = &xTCBArray[xIndex];
        vTaskPrioritySet(maxPriorityTaskHandle, pxCurrentTask->xOriginalPriority);

        Serial.print("Resuming highest priority task ");
        Serial.print(pxCurrentTask->pcName); 
        Serial.println("!"); Serial.flush();

        // resume the highest priority task
        xResource->xBlockedTasks[maxPriorityIndex] = 0;
        xResource->xNumBlockedTasks--;
        vTaskResume(maxPriorityTaskHandle);
    } else {
        Serial.println("No blocked resources to resume!");
        Serial.flush();
    }
}


/* This function must be called before any other function call from this module. */
void vSchedulerInit(void)
{
    #if (schedUSE_TCB_ARRAY == 1)
        prvInitTCBArray();
    #endif /* schedUSE_TCB_ARRAY */

    #if (schedUSE_SEMAPHORES == 1)
        prvInitCriticalResourceArray();
    #endif /* schedUSE_TCB_ARRAY */

	Serial.println("vSchedulerInit() completed!");
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart(void)
{
#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
	prvSetFixedPriorities();
#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS)
	Serial.println("Schedule using RM.");
#elif (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
	Serial.println("Schedule using DM.");
#endif

#endif /* schedSCHEDULING_POLICY */

#if (schedUSE_OCPP) 
    Serial.println("Running OCPP");
#else
    Serial.println("Running ICPP");
#endif

#if (schedUSE_SCHEDULER_TASK == 1)
	prvCreateSchedulerTask();
#endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();

	xSystemStartTime = xTaskGetTickCount();


	vTaskStartScheduler();
}
