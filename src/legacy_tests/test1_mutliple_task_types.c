/**
 * 
 * Test creates several tasks with different task types
 * 
 * Primary tasks spawn other secondary tasks in this test
 * Secondary tasks run a finite amount of iterations, printing at each step and yielding.
 * 
 * Extra thread is created which will randomly kill task board via tboard_kill() to test killing task board
 * 
 * 
 * 
 */
#include "legacy_tests.h"
#ifdef LTEST_1

#include <stdio.h>
#include <unistd.h>

#include "../tboard.h"
#include <pthread.h>
#include <time.h>

struct timespec timeout = {
	.tv_sec = 0,
	.tv_nsec = 1000000,
};

tboard_t *tboard = NULL;
int n = 0;
void task_one(context_t ctx){
	(void)ctx;
	printf("Task one started! %ld\n",pthread_self());
	task_yield();
	for(int i=0; i<10; i++){
		printf("Task one on %d: %ld\n",i,pthread_self());
		task_yield();
	}
}

void task_two(context_t ctx){
	(void)ctx;
	printf("Task two started! %ld\n",pthread_self());
	task_yield();
	for(int i=0; i<10; i++){
		printf("Task two on %d: %ld\n",i,pthread_self());
		nanosleep(&timeout, NULL);
		task_yield();
	}
}
void task_spawnling(context_t ctx){
	(void)ctx;
	int i = (n++);
	printf("Spawnling %d Start on %ld\n",i,pthread_self());
	task_yield();
	printf("Spawnling %d Ended on %ld\n",i,pthread_self());
}

void task_spawning_tasks(context_t ctx){
	(void)ctx;
	printf("=== Spawning some tasks and ending ===\n");
	for(int i=0; i<100; i++){
		task_create(tboard, TBOARD_FUNC(task_spawnling), 1, NULL, 0);
	}
	task_create(tboard, TBOARD_FUNC(task_spawnling), 0, NULL, 0);
	task_yield();
	for (int i=0; i<100000; i++) {
		if (false == task_create(tboard, TBOARD_FUNC(task_spawnling), 1, NULL, 0)) {
			i--;
			nanosleep(&timeout, NULL);
			task_yield();
			continue;
		}	
	}
	task_yield();
	printf("==== Spawning Ended ===");
}

void *kill_tboard_at_some_point(void *args){
	tboard_t *t = (tboard_t *)args;
	while(true){
		if(rand() % 50 == 5){
			printf("Killing tboard.\n");
			pthread_mutex_lock(&(t->tmutex));
			tboard_kill(tboard);
			history_print_records(t, stdout);
			pthread_mutex_unlock(&(t->tmutex));
			break;
		}else{
			nanosleep(&timeout, NULL);
		}
	}
	return NULL;
}


int main()
{
	printf("Creating tboard\n\n");
	tboard = tboard_create(10);

	pthread_t killer_thread;
	pthread_create(&killer_thread, NULL, kill_tboard_at_some_point, tboard);

	printf("Tboard created\n\n");
	tboard_start(tboard);
	printf("Tboard started\n\n");

	task_create(tboard, TBOARD_FUNC(task_one), 0, NULL, 0);
	printf("Task 1 created\n\n");
	task_create(tboard, TBOARD_FUNC(task_two), 1, NULL, 0);
	printf("Task 2 created\n\n");
	
	task_create(tboard, TBOARD_FUNC(task_spawning_tasks), 0, NULL, 0);
	printf("\n\n============== NEXT BATCH ===============");

	task_create(tboard, TBOARD_FUNC(task_one), 0, NULL, 0);
	printf("Task 1 created\n\n");
	task_create(tboard, TBOARD_FUNC(task_two), 1, NULL, 0);
	printf("Task 2 created\n\n");



	tboard_destroy(tboard);
	printf("Tboard Destroyed.\n");
	pthread_join(killer_thread, NULL);
	tboard_exit();
	return (0);
} 


#endif