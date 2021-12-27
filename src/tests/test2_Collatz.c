#include "tests.h"
#ifdef TEST_2


#include <stdio.h>

#include "../tboard.h"
#include <pthread.h>
#include <time.h>
#include <stdbool.h>


#define COLLATZ_ITERATIONS 100000 //100000000 // number of secondary tasks to spawn
#define SECONDARY_EXECUTORS 10

#define SAVE_SEQUENCE_TO_DISK 0
#define CHECK_COMPLETION 0
#define ISSUE_PRIORITY_TASKS 0

#define RANDOMLY_TERMINATE 0

#define ALLOC_N 1

struct collatz_iteration {
	int starting_x;
	int current_iteration;
	unsigned long current_x;
};

tboard_t *tboard = NULL;
int n = 0;
int NUM_TASKS = COLLATZ_ITERATIONS;
int completion_count = 0;
int task_count = 0;
double yield_count = 0;
int priority_count = 0;
bool print_priority = true;
bool primary_task_complete = false;
int max_tasks_reached = 0;
pthread_t killer_thread, priority_creator_thread,pcompletion;
pthread_mutex_t count_mutex;

#if SAVE_SEQUENCE_TO_DISK == 1
int sequence[COLLATZ_ITERATIONS] = {0};
#else
int *sequence = NULL;
#endif

#if CHECK_COMPLETION == 1
int n_completed[COLLATZ_ITERATIONS] = {0};
#else
int *n_completed = NULL;
#endif

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

void priority_task(void *args)
{
	int priority_count = (int)task_get_args();
	if(print_priority)
		tboard_log("priority: priority task %d executed at CPU time %d.\n", priority_count, clock());
}


void secondary_task(void *);

void check_completion(void *args){
	while(true){
		if(primary_task_complete && completion_count >= task_count){
			pthread_mutex_lock(&(tboard->tmutex));
			tboard_log("Completed Collatz Test for %d numbers with %e yields.\n",task_count, yield_count);
			tboard_log("Max tasks reached %d times. There were %d priority tasks executed.\n", max_tasks_reached, priority_count);
			pthread_cancel(killer_thread);
			pthread_cancel(priority_creator_thread);

			unsigned long cond_wait_time = clock();
			tboard_kill(tboard);
			cond_wait_time = clock() - cond_wait_time;

			int unfinished_tasks = tboard->task_count;
			//for(int i=0; i<MAX_TASKS; i++){
			//	if (tboard->task_list[i].status != 0)
			//		unfinished_tasks++;
			//}
			tboard_log("Found %d unfinished tasks, waited %ld CPU cycles for killing taskboard.\n", unfinished_tasks, cond_wait_time);
			
			history_print_records(tboard, stdout);
			pthread_mutex_unlock(&(tboard->tmutex));
			break;
		}
		usleep(300); // we are doing many tasks, can afford to have seperate thread sleep for longer. usleep(300);
		//task_yield(); yield_count++;
	}
}

void primary_task(void *args)
{
	int i = 0;
    int *n;
	int size = ALLOC_N ? sizeof(int) : 0;
    primary_task_complete = false;
	tboard_log("primary: Creating %d many different tasks to test 3x+1\n", NUM_TASKS);
	for (; i<NUM_TASKS; i++) {
        int unable_to_create_task_count = 0; // bad name i know
		int ic = i;
		if(ALLOC_N == 1){
			n = calloc(1, sizeof(int));
			*n = ic;
		}else{
        	n = &ic;
        }
		while(false == task_create(tboard, TBOARD_FUNC(secondary_task), SECONDARY_EXEC, n, size)){
            if(unable_to_create_task_count > 30){
                tboard_log("primary: Was unable to create the same task after 30 attempts. Ending at %d tasks created.\n",i);
                primary_task_complete = true;
				free(n);
                return;
            }
			max_tasks_reached++;
			usleep(300);
			task_yield(); yield_count++;
            unable_to_create_task_count++;
		}
		task_count++;
		task_yield(); yield_count++;
	}
	tboard_log("primary: Created %d tasks to test 3x+1.\n", i);
	
	task_yield(); yield_count++;
    primary_task_complete = true;

	if (SAVE_SEQUENCE_TO_DISK) {
		sequence[0] = 0;
		sequence[1] = 1;
	}
	if (CHECK_COMPLETION) {
		n_completed[0] = 1;
		n_completed[1] = 1;
	}
}
void secondary_task(void *args){
	int *xptr = ((int *)(task_get_args()));
	int x_orig = *xptr;
    long x = *xptr;
    //free(xptr);
    int i = 0;
    if (x <= 1) {
        if (x >= 0) increment_completion_count();
        else        tboard_err("secondary: Invalid value of x encountered in secondary task: %d\n", x);
        return;

    }
	while (x != 1) {
		if(x % 2 == 0)  x /= 2;
		else 			x = 3*x+1;
		i++;
		task_yield(); yield_count++;
	}
    increment_completion_count();
	if (SAVE_SEQUENCE_TO_DISK)
		sequence[x_orig] = i;
	if (CHECK_COMPLETION)
		n_completed[x_orig] = 1;

}


void tboard_killer(void *args){
    int last_completion = -1;
	long start_time = clock();
	long end_time;
    while(true){
        int cc = read_completion_count();
        if(cc != last_completion){
            last_completion = cc;
        }else{
            tboard_log("Error: Has not finished a task in 10 seconds, killing taskboard with %d completions.\n", completion_count);
            break;
        }
		end_time = clock();
		double cpu_time = (double)(end_time-start_time);
		double rate = cpu_time / last_completion;
		cpu_time = cpu_time / CLOCKS_PER_SEC;
		printf("Completed %d/%d tasks in %f CPU minutes (%f cpu time/task rate)\n",last_completion, NUM_TASKS, cpu_time/60, rate);
        if(RANDOMLY_TERMINATE && rand()%5 == 2) break;
		sleep(10);
    }

	pthread_cancel(*((pthread_t *)args));
    pthread_cancel(pcompletion);
	pthread_mutex_lock(&(tboard->tmutex));
	tboard_kill(tboard);
	int unfinished_tasks = 0;
	history_print_records(tboard, stdout);
	//pthread_cond_wait(&(tboard->tcond), &(tboard->tmutex));
	pthread_mutex_unlock(&(tboard->tmutex));

	tboard_log("Confirmed conjecture for %d of %d values with %e yields.\n", completion_count, task_count, yield_count);
	tboard_log("Max tasks reached %d times. There were %d priority tasks executed.\n", max_tasks_reached, priority_count);
}

void priority_task_creator(void *args){
	if (ISSUE_PRIORITY_TASKS == 0)
		return;
	priority_count = 0;
	while(true){
		sleep(rand() % 20);
		if(print_priority)
			tboard_log("priority: issued priority task at CPU time %d\n",clock());
		bool res = task_create(tboard, TBOARD_FUNC(priority_task), PRIORITY_EXEC, priority_count, 0);
		if(res)
			priority_count++;
	}
}

int main(int argc)
{
	if(argc > 1) print_priority = false;
	pthread_mutex_init(&count_mutex, NULL);

	tboard = tboard_create(SECONDARY_EXECUTORS);
	tboard_start(tboard);

	
	pthread_create(&priority_creator_thread, NULL, priority_task_creator, NULL);
	pthread_create(&killer_thread, NULL, tboard_killer, &priority_creator_thread);
	pthread_create(&pcompletion, NULL, check_completion, NULL);

	task_create(tboard, TBOARD_FUNC(primary_task), PRIMARY_EXEC, NULL, 0);
	
	pthread_join(priority_creator_thread, NULL);
	tboard_destroy(tboard);
    pthread_join(killer_thread, NULL);
	pthread_join(pcompletion, NULL);

	pthread_mutex_destroy(&count_mutex);

	if(CHECK_COMPLETION == 1){
		bool ncomplete_found = false;
		for(int i=0; i<NUM_TASKS; i++){
			if(n_completed[i] == 0){
				if(ncomplete_found == false){
					ncomplete_found = true;
					printf("Found incomplete n(s): ");
				}
				printf("%d ",i);
			}
		}
	}
	if(SAVE_SEQUENCE_TO_DISK == 1){
		FILE *fptr = fopen("A006877.txt", "w");
		for(int i=0; i<NUM_TASKS; i++)
			fprintf(fptr, "%d\n", sequence[i]);
		fclose(fptr);
	}
	tboard_exit();

	return (0);
}

#endif
// */