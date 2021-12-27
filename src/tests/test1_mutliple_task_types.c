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
#include "tests.h"

#ifdef TEST_1

#include <stdio.h>
#include <unistd.h>

#include "../tboard.h"
#include <pthread.h>
#include <time.h>

struct timespec timeout = {
	.tv_nsec = 1000000,
};


tboard_t *tboard = NULL;
int n = 0;
void task_one(void *args){
	printf("Task one started! %d\n",pthread_self());
	task_yield();
	for(int i=0; i<10; i++){
		printf("Task one on %d: %d\n",i,pthread_self());
		task_yield();
	}
}

void task_two(void *args){
	printf("Task two started! %d\n",pthread_self());
	task_yield();
	for(int i=0; i<10; i++){
		printf("Task two on %d: %d\n",i,pthread_self());
		task_yield();
	}
}
void task_spawnling(void *arg){
	int i = (n++);
	printf("Spawnling %d Start on %d\n",i,pthread_self());
	task_yield();
	printf("Spawnling %d Ended on %d\n",i,pthread_self());
}

void task_spawning_tasks(void *args){
	printf("=== Spawning some tasks and ending ===\n");
	for(int i=0; i<100; i++){
		task_create(tboard, TBOARD_FUNC(task_spawnling), 1, NULL);
	}
	task_create(tboard, TBOARD_FUNC(task_spawnling), 0, NULL);
	task_yield();
	for (int i=0; i<10000; i++) {
		if (false == task_create(tboard, TBOARD_FUNC(task_spawnling), 1, NULL)) {
			i--;
			task_yield();
			//nanosleep(&timeout, NULL);
			continue;
		}	
	}
	task_yield();
	printf("==== Spawning Ended ===");
}

void kill_tboard_at_some_point(void *args){
	tboard_t *t = (tboard_t *)args;
	while(true){
		if(rand() % 100 == 5){
			pthread_mutex_lock(&(t->tmutex));
			tboard_kill(tboard);
			history_print_records(t, stdout);
			pthread_mutex_unlock(&(t->tmutex));
			break;
		}else{
			nanosleep(&timeout, NULL);
		}
	}
}


int main()
{
	printf("Creating tboard\n\n");
	tboard = tboard_create(10);

	pthread_t killer_thread;
	pthread_create(&killer_thread, NULL, &kill_tboard_at_some_point, tboard);

	printf("Tboard created\n\n");
	tboard_start(tboard);
	printf("Tboard started\n\n");

	task_create(tboard, TBOARD_FUNC(task_one), 0, NULL);
	printf("Task 1 created\n\n");
	task_create(tboard, TBOARD_FUNC(task_two), 1, NULL);
	printf("Task 2 created\n\n");
	
	task_create(tboard, TBOARD_FUNC(task_spawning_tasks), 0, NULL);
	printf("\n\n============== NEXT BATCH ===============");

	task_create(tboard, TBOARD_FUNC(task_one), 0, NULL);
	printf("Task 1 created\n\n");
	task_create(tboard, TBOARD_FUNC(task_two), 1, NULL);
	printf("Task 2 created\n\n");



	tboard_destroy(tboard);
	printf("Tboard Destroyed.\n");
	printf("Tboard is done\n\n");
	pthread_join(killer_thread, NULL);
	tboard_exit();
	return (0);
} 


#endif