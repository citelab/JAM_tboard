/**
 * Test #: #NAME#, #DESCRIPTION#
 * 
 * The types of local tests we create are:
 * * Priority tasks: These tasks must complete as soon as possible
 * * Primary tasks: These tasks spawn other tasks
 * * Secondary tasks: These tasks run and then terminate
 * The types of remote tests we create are:
 * * #TASKNAME#: #TASKDESC#
 * 
 * #Explain Test#
 * 
 * #Explain test usage#
 */

#include "tests.h"
#ifdef TEST_N

#include "../tboard.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stdbool.h>

/**
 * 
 * Variable declaration
 */
pthread_t thread_f_pt;
long kill_time, test_time;
int unfinished_tasks;


void local_task(context_t ctx);
void remote_task(context_t ctx);

void *thread_func(void *args);

int main()
{
    test_time = clock();
    init_tests();

    task_create(tboard, TBOARD_FUNC(local_task), PRIMARY_EXEC, NULL, 0);

    destroy_tests();
    test_time = clock() - test_time;

    printf("\n=================== TEST STATISTICS ================\n");
    printf("Test took %ld CPU cycles to complete, killing taskboard took %ld CPU cycles to complete.\n",test_time, kill_time);
    
    tboard_exit();
}

void init_tests()
{
    tboard = tboard_create(SECONDARY_EXECUTORS);
    pthread_mutex_init(&count_mutex, NULL);

    tboard_start(tboard);

    pthread_create(&chk_complete, NULL, check_completion, tboard);
    pthread_create(&tb_killer, NULL, kill_tboard, tboard);
    pthread_create(&thread_f_pt, NULL, thread_func, tboard);

    printf("Taskboard created, all threads initialized.\n");
}

void destroy_tests()
{
    tboard_destroy(tboard);
    pthread_join(chk_complete, NULL);
    pthread_join(tb_killer, NULL);
    pthread_join(thread_f_pt, NULL);
    pthread_mutex_destroy(&count_mutex);
}

void *kill_tboard (void *args)
{
    tboard_t *t = (tboard_t *)args;
    fsleep(MAX_RUN_TIME);
    // initiate killing task board
    pthread_mutex_lock(&(t->tmutex));
    // Kill tboard, record run time
    kill_time = clock();
    tboard_kill(t);
    kill_time = clock() - kill_time;
    // print task history
    printf("=================== TASK STATISTICS ================\n");
    history_print_records(t, stdout);
    unfinished_tasks = t->task_count;
    pthread_mutex_unlock(&(t->tmutex));
    // task board has been killed, return to terminate thread
    return NULL;
}

void *check_completion(void *args)
{
    tboard_t *t = (tboard_t *)args;
    (void)t;
    return NULL;
}

void *thread_func(void *args)
{
    (void)args;
    return NULL;
}

void local_task(context_t ctx)
{
    (void)ctx;
    printf("Local task occured.\n");
}

void remote_task(context_t ctx)
{
    (void)ctx;
    printf("Remote task occured.\n");
}


#endif