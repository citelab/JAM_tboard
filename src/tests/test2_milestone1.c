/**
 * Test 2: Milestone 1. In this test, we attempt rapid execution to capture execution rate
 * 
 * spawning_task() - Spawns finite number of tasks NUM_TASKS and then exits
 * sub_task() - Rapidly tests collatz conjecture and yields, one at a time
 * 
 * In order to show intermediate print statements, call function with any argument
 */
#include "tests.h"
#ifdef TEST_2

#include "../tboard.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stdbool.h>

#define BLOCKING_TASKS 0
#define TASK_TYPE PRIMARY_EXEC

int completion_count = 0;
int task_count = 0;
long aprx_yield_count = 0;
bool spawning_task_complete = false;

clock_t test_time, kill_time;

void sub_task(context_t ctx);
void spawning_task(context_t ctx);

int main()
{
    test_time = clock();

    init_tests();

    task_create(tboard, TBOARD_FUNC(spawning_task), TASK_TYPE, NULL, 0);

    destroy_tests();
    test_time = clock() - test_time;

    printf("\n=================== TEST STATISTICS ================\n");
    printf("\t%d/%d sub tasks completed with %ld yields.\n", completion_count, NUM_TASKS, aprx_yield_count);
    printf("Test took %ld CPU cycles to complete, killing taskboard took %ld CPU cycles to complete.\n",test_time, kill_time);
    
    tboard_exit();
    return 0;

}

void init_tests()
{
    tboard = tboard_create(SECONDARY_EXECUTORS);
    pthread_mutex_init(&count_mutex, NULL);

    tboard_start(tboard);

    pthread_create(&chk_complete, NULL, check_completion, tboard);

    printf("Taskboard created, all threads initialized.\n");
}

void destroy_tests()
{
    tboard_destroy(tboard);
    pthread_join(chk_complete, NULL);
    pthread_mutex_destroy(&count_mutex);
}

void *check_completion(void *args)
{
    tboard_t *t = (tboard_t *)args;
    while (true) {
        if (spawning_task_complete && completion_count >= task_count) {
            pthread_mutex_lock(&(t->tmutex));
            kill_time = clock();
            tboard_kill(t);
            kill_time = clock() - kill_time;
            printf("=================== TASK STATISTICS ================\n");
            history_print_records(t, stdout);
            pthread_mutex_unlock(&(t->tmutex));
            break;
        } else {
            fsleep(0.01);
        }
    }
    return NULL;
}



void sub_task(context_t ctx)
{
    (void)ctx;
    long x = *((long *)task_get_args());
    if (x <= 0)
        return increment_count(&completion_count);
    
    while (x > 1) {
        if (x % 2 == 0) x /= 2;
        else            x = 3*x + 1;
        task_yield(); aprx_yield_count++;
    }
    increment_count(&completion_count);
}

void spawning_task(context_t ctx)
{
    (void)ctx;
    for (long i=0; i<NUM_TASKS; i++) {
        long *n = calloc(1, sizeof(long));
        *n = i;
        if (BLOCKING_TASKS == 1) {
            bool res = blocking_task_create(tboard, TBOARD_FUNC(sub_task), TASK_TYPE, n, sizeof(long));
            if (!res) {
                tboard_err("spawning_task: Unable to create blocking task at x=%ld.\n",i);
                free(n);
                spawning_task_complete = true;
                return;
            }
            task_count++;
        } else {
            int attempt = 0;
            while (false == task_create(tboard, TBOARD_FUNC(sub_task), TASK_TYPE, n, sizeof(long))) {
                if (attempt > MAX_TASK_ATTEMPT) {
                    tboard_err("spawning_task: Unable to create sub task at iteration %ld after %d attempts.\n", i, MAX_TASK_ATTEMPT);
                    spawning_task_complete = true;
                    free(n);
                    return;
                }
                attempt++;
                free(n);
                task_yield(); aprx_yield_count++; // no locking to test speed
                n = calloc(1, sizeof(long)); *n = i;
            }
            task_count++;
        }
    }
    spawning_task_complete = true;
    return;
}


#endif