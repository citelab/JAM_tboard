#include "tests.h"

#ifdef TEST_5

#include "../tboard.h"
#include "../dummy_MQTT.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#define SECONDARY_EXECUTORS 10
#define SMALL_TASK_ITERATIONS 1000000
#define PRIMARY_TASKS 4
#define ISSUE_PRIORITY_TASK 1


bool 

tboard_t *tboard = NULL;
pthread_t msg_gen, tboard_kill, priority_t_gen;

pthread_mutex_t count_mutex;

int completion_count = 0;
double yield_count = 0;
int task_count = 0;


void increment_completion_count(){
	pthread_mutex_lock(&count_mutex);
	completion_count++;
	pthread_mutex_unlock(&count_mutex);
}

int read_completion_count(){
	pthread_mutex_lock(&count_mutex);
	int ret = completion_count;
	pthread_mutex_unlock(&count_mutex);
    return ret;
}


///////////// Task Functions ///////////////

void priority_task(void *args);
void primary_task(void *args);
void secondary_task(void *args);

void generate_MQTT_message(void *args);

#endif