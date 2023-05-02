#include "scheduler.h"

TaskHandle_t xHandle1 = NULL;
TaskHandle_t xHandle2 = NULL;

TaskHandle_t xHandle3 = NULL;
TaskHandle_t xHandle4 = NULL;



// the loop function runs over and over again forever
void loop() {}


static void Task1_Set1( void *pvParameters )
{
  int c = *(int *)pvParameters;
	volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 6;
  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 1000; i ++) {j+=1;}
  }

}

static void Task2_Set1( void *pvParameters )
{
  int c = *(int *)pvParameters;
	volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 12;
  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 1000; i ++) {j+=1;}
  }
}

static void Task3_Set1( void *pvParameters )
{
  int c = *(int *)pvParameters;
	volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 9;
  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 1000; i ++) {j+=1;}
  }
}

static void Task4_Set1( void *pvParameters )
{ 
  int c = *(int *)pvParameters;
	volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 18;
  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 1000; i ++) {j+=1;}
  }
}

static void Task1_Set2( void *pvParameters )
{
  int c = *(int *)pvParameters;
	volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 6;
  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 5000; i ++) {j+=1;}
  }
}

static void Task2_Set2( void *pvParameters )
{
  int c = *(int *)pvParameters;
  volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 10;
    
  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 5000; i ++) {j+=1;}
  }
}

static void Task3_Set2( void *pvParameters )
{
  int c = *(int *)pvParameters;
  volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 12;

  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 5000; i ++) {j+=1;}
  }
}

static void Task4_Set2( void *pvParameters )
{ 
  int c = *(int *)pvParameters;
  volatile int i,j, k;
  TickType_t xTickCountStart = xTaskGetTickCount();
  TickType_t xTickCountEnd = xTickCountStart;
  TickType_t xTickCountInterval = 9;

  while ((xTickCountEnd - xTickCountStart) < xTickCountInterval) {
    xTickCountEnd = xTaskGetTickCount();
    for (i = 0; i < 5000; i ++) {j+=1;}
  }

}

int main( void )
{
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB, on LEONARDO, MICRO, YUN, and other 32u4 based boards.
  }
	char c1 = 'a';
	char c2 = 'b';			
  char c3 = 'c';
	char c4 = 'd';			
  
	vSchedulerInit();

// vSchedulerPeriodicTaskCreate ()
// TaskFunction_t pvTaskCode (aka void (*)(void *))
// const char * pcName
// UBaseType_t uxStackDepth (aka unsigned char)
// void * pvParameters
// UBaseType_t uxPriority (aka unsigned char)
// TaskHandle_t * pxCreatedTask (aka TaskControlBlock_t **)
// TickType_t xPhaseTick (aka unsigned int)
// TickType_t xPeriodTick (aka unsigned int)
// TickType_t xMaxExecTimeTick (aka unsigned int)
// TickType_t xDeadlineTick (aka unsigned int)

	// vSchedulerPeriodicTaskCreate(Task1_Set1, "t1", configMINIMAL_STACK_SIZE, &c1, 1, &xHandle1, pdMS_TO_TICKS(0), pdMS_TO_TICKS(400), pdMS_TO_TICKS(100), pdMS_TO_TICKS(400));
	// vSchedulerPeriodicTaskCreate(Task2_Set1, "t2", configMINIMAL_STACK_SIZE, &c2, 2, &xHandle2, pdMS_TO_TICKS(0), pdMS_TO_TICKS(800), pdMS_TO_TICKS(200), pdMS_TO_TICKS(700));
  // vSchedulerPeriodicTaskCreate(Task3_Set1, "t3", configMINIMAL_STACK_SIZE, &c3, 3, &xHandle3, pdMS_TO_TICKS(0), pdMS_TO_TICKS(1000), pdMS_TO_TICKS(150), pdMS_TO_TICKS(1000));
	// vSchedulerPeriodicTaskCreate(Task4_Set1, "t4", configMINIMAL_STACK_SIZE, &c4, 4, &xHandle4, pdMS_TO_TICKS(0), pdMS_TO_TICKS(5000), pdMS_TO_TICKS(300), pdMS_TO_TICKS(5000));

  vSchedulerPeriodicTaskCreate(Task1_Set2, "t1", configMINIMAL_STACK_SIZE, &c1, 1, &xHandle1, pdMS_TO_TICKS(0), pdMS_TO_TICKS(700), pdMS_TO_TICKS(100), pdMS_TO_TICKS(700));
	vSchedulerPeriodicTaskCreate(Task2_Set2, "t2", configMINIMAL_STACK_SIZE, &c2, 1, &xHandle2, pdMS_TO_TICKS(0), pdMS_TO_TICKS(300), pdMS_TO_TICKS(150), pdMS_TO_TICKS(300));
  vSchedulerPeriodicTaskCreate(Task3_Set2, "t3", configMINIMAL_STACK_SIZE, &c3, 1, &xHandle3, pdMS_TO_TICKS(0), pdMS_TO_TICKS(1100), pdMS_TO_TICKS(200), pdMS_TO_TICKS(1100));
	vSchedulerPeriodicTaskCreate(Task4_Set2, "t4", configMINIMAL_STACK_SIZE, &c4, 1, &xHandle4, pdMS_TO_TICKS(0), pdMS_TO_TICKS(1500), pdMS_TO_TICKS(150), pdMS_TO_TICKS(1500));



	vSchedulerStart();

	/* If all is well, the scheduler will now be running, and the following line
	will never be reached. */
	
	for( ;; );
}

