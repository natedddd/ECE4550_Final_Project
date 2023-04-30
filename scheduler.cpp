#include "scheduler.h"
#include "list.h"

#if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF) 
    #define schedUSE_TCB_LIST 1
#else
    #define schedUSE_TCB_ARRAY 1
#endif /* schedSCHEDULING_POLICY */

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

	BaseType_t xWorkIsDone; /* pdFALSE if the job is not finished, pdTRUE if the job is finished. */

#if (schedUSE_TCB_ARRAY == 1)
	BaseType_t xPriorityIsSet; /* pdTRUE if the priority is assigned. */
	BaseType_t xInUse;		   /* pdFALSE if this extended TCB is empty. */
#else
    ListItem_t xTCBListItem;
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
#elif (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF)
    static void initializePrioritiesEDF(void);
    static void updatePrioritiesEDF(void);
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
#elif (schedUSE_TCB_LIST == 1)
    static List_t *xTCBList = NULL; // linked list for period tasks
    static List_t *xTCBTempList = NULL; // temp list for switching tasks
    static List_t *xTCBPastDeadlineList = NULL; // extra list if an tasks go pass their deadline
#endif /* schedUSE_TCB_ARRAY */

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
} /* schedUSE_TCB_ARRAY */
#elif (schedUSE_TCB_LIST == 1)
    static void prvAddToTCBList(SchedTCB_t *pxTCB) {
        // Initialize list
        vListInitialiseItem(&pxTCB->xTCBListItem);

        // set item owner
        listSET_LIST_ITEM_OWNER(&pxTCB->xTCBListItem, pxTCB);

        // sort list by absolute deadline
        listSET_LIST_ITEM_VALUE(&pxTCB->xTCBListItem,pxTCB->xAbsoluteDeadline);

        // item the items
        vListInsert(xTCBList, &pxTCB->xTCBListItem);
    }

    static void prvDeleteFromTCBList(SchedTCB_t *pxTCB) {
        uxListRemove(&pxTCB->xTCBListItem);
        vPortFree(pxTCB);
    }
#endif 

/* The whole function code that is executed by every periodic task.
 * This function wraps the task code specified by the user. */
static void prvPeriodicTaskCode(void* pvParameters)
{
    #if (schedUSE_TCB_ARRAY == 1)
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
    #else
        SchedTCB_t *pxThisTask = (SchedTCB_t *) pvTaskGetThreadLocalStoragePointer(xTaskGetCurrentTaskHandle(), 0);

    #endif

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
        #if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
            prvWakeScheduler(); // wake scheduler to update task priorities
        #endif 
		/* Execute the task function specified by the user. */
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

        #if( schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF )
            pxThisTask->xAbsoluteDeadline = pxThisTask->xLastWakeTime + pxThisTask->xPeriod + pxThisTask->xRelativeDeadline;
            prvWakeScheduler(); // wake scheduler to update task priorities
        #endif 
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
	// pxNewTCB->uxPriorityIsSet = pdTRUE;

    #if (schedUSE_TCB_ARRAY == 1)
        pxNewTCB->xInUse = pdTRUE;
    #endif /* schedUSE_TCB_ARRAY */

    #if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
        /* member initialization */
        /* your implementation goes here */
        pxNewTCB->xPriorityIsSet = pdFALSE;
    #elif (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF)
        pxNewTCB->xAbsoluteDeadline = pxNewTCB->xRelativeDeadline + pxNewTCB->xReleaseTime + xSystemStartTime;
        pxNewTCB->uxPriority = -1;
    #endif
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
    #else 
        prvAddToTCBList(pxNewTCB);
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
#else 
    const ListItem_t *pxTCBListEnd = listGET_END_MARKER(pxTCBList);
    ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY(pxTCBList);

    while(pxTCBListItem != pxTCBListEnd) {
        curPxTCB = listGET_LIST_ITEM_OWNER(pxTCBListItem);

        xTaskCreate(prvPeriodicTaskCode, curPxTCB->pcName, curPxTCB->uxStackDepth, curPxTCB->pvParameters, curPxTCB->uxPriority, curPxTCB->pxTaskHandle);

        vTaskSetThreadLocalStoragePointer(*curPxTCB->pxTaskHandle, schedTHREAD_LOCAL_STORAGE_POINTER_INDEX, curPxTCB);
        pxTCBListItem = listGET_NEXT( pxTCBListItem );
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
	}
}
#elif (schedSCHEDULING_POLICY== schedSCHEDULING_POLICY_EDF)
    // Initializes priorities of all tasks using EDF scheduling
    static void initializeEDF(void) {
        SchedTCB_t *pxTCB;

        #if( schedUSE_SCHEDULER_TASK == 1 )
            UBaseType_t uxHighestPriority = schedSCHEDULER_PRIORITY - 1;
        #else
            UBaseType_t uxHighestPriority = configMAX_PRIORITIES - 1;
        #endif /* schedUSE_SCHEDULER_TASK */

			const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER( pxTCBList );
			ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );

			while( pxTCBListItem != pxTCBListEndMarker )
			{
				pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );

				pxTCB->uxPriority = uxHighestPriority;
				uxHighestPriority--;

				pxTCBListItem = listGET_NEXT( pxTCBListItem );
			}
    }
    
    // swap the pointers to two linked lists
    static void swapList( List_t **ppxList1, List_t **ppxList2 ) {
		List_t *pxTemp;
        pxTemp = *ppxList1;
        *ppxList1 = *ppxList2;
        *ppxList2 = pxTemp;
    }

    static void updatePrioritiesEDF( void ) {
		SchedTCB_t *pxTCB;
        ListItem_t *pxTCBListItem;
        ListItem_t *pxTCBListItemTemp;
    
        if (listLIST_IS_EMPTY(pxTCBList) && !listLIST_IS_EMPTY(xTCBPastDeadlineList)) {
            swapList( &pxTCBList, &xTCBPastDeadlineList );
        }

        const ListItem_t *pxTCBListEndMarker = listGET_END_MARKER(pxTCBList);
        pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );

        while( pxTCBListItem != pxTCBListEndMarker ) {
            pxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );

            // update priority
            listSET_LIST_ITEM_VALUE( pxTCBListItem, pxTCB->xAbsoluteDeadline );

            pxTCBListItemTemp = pxTCBListItem;
            pxTCBListItem = listGET_NEXT( pxTCBListItem );
            uxListRemove( pxTCBListItem->pxPrevious );

            // insert into the extra list if its before deadline
            if( pxTCB->xAbsoluteDeadline < pxTCB->xLastWakeTime ) {
                vListInsert( xTCBPastDeadlineList, pxTCBListItemTemp );

            } else { // interest into the temp list
                vListInsert( pxTCBTempList, pxTCBListItemTemp );
            }
        }

        // swap current list with the temp list
        swapList( &pxTCBList, &pxTCBTempList );

        BaseType_t xHighestPriority = schedSCHEDULER_PRIORITY - 1;

        const ListItem_t *pxTCBListEndMarkerAfterSwap = listGET_END_MARKER( pxTCBList );
        pxTCBListItem = listGET_HEAD_ENTRY( pxTCBList );
        while( pxTCBListItem != pxTCBListEndMarkerAfterSwprvAddTCBToListap )
        {
            curPxTCB = listGET_LIST_ITEM_OWNER( pxTCBListItem );
            curPxTCB->uxPriority = xHighestPriority;
            vTaskPrioritySet( *curPxTCB->pxTaskHandle, curPxTCB->uxPriority );

            xHighestPriority--;
            pxTCBListItem = listGET_NEXT( pxTCBListItem );
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
        if (schedSCHEDULING_POLICY != schedSCHEDULING_POLICY_EDF) {
		    pxTCB->xAbsoluteDeadline = pxTCB->xLastWakeTime + pxTCB->xRelativeDeadline;
        }
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

    #if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF) 
        updatePrioritiesEDF();
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
        #else 
            const ListItem_t *pxTCBListEnd = listGET_END_MARKER(pxTCBList);
            ListItem_t *pxTCBListItem = listGET_HEAD_ENTRY(pxTCBList);

            while (pxTCBListItem != pxTCBListEnd) {
                pxTCB = listGET_LIST_ITEM_OWNER(pxTCBListItem);

                prvSchedulerCheckTimingError(xTickCount, pxTCB);

                pxTCBListItem = listGET_NEXT(pxTCBListItem);
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

/* This function must be called before any other function call from this module. */
void vSchedulerInit(void)
{
    #if (schedUSE_TCB_ARRAY == 1)
        prvInitTCBArray();
    #endif /* schedUSE_TCB_ARRAY */

	Serial.println("vSchedulerInit() completed!");
}

/* Starts scheduling tasks. All periodic tasks (including polling server) must
 * have been created with API function before calling this function. */
void vSchedulerStart(void)
{
    #if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS || schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
        prvSetFixedPriorities();
    #elif (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF) 
        initializeEDF();
    #endif /* schedSCHEDULING_POLICY */

    #if (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_RMS)
        Serial.println("Schedule using RM.");
    #elif (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_DMS)
        Serial.println("Schedule using DM.");
    #elif (schedSCHEDULING_POLICY == schedSCHEDULING_POLICY_EDF)
        Serial.println("Schedule using EDF.");
    #endif


    #if (schedUSE_SCHEDULER_TASK == 1)
        prvCreateSchedulerTask();
    #endif /* schedUSE_SCHEDULER_TASK */

	prvCreateAllTasks();

	xSystemStartTime = xTaskGetTickCount();

	vTaskStartScheduler();
}
