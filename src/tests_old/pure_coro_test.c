#include "legacy_tests.h"

#ifdef LTEST_CORO

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// IF RUNNING STANDALONE, YOU MUST REMOVE THIS COMMENT!!!
//#define MINICORO_IMPL
#define MINICORO_ASM
#define MCO_ZERO_MEMORY


#include <minicoro.h>
#include "../queue/queue.h"


#define VERBOSE 0
#define NUMBER_TASKS 100
#define MAX_CONCURRENT_TASKS 128
#define TIME_BETWEEN_STATUS_UPDATE 30

typedef struct {
    int id;
    mco_desc desc;
    mco_coro *ctx;
    void (*fn)(mco_coro *);
} task;

struct queue ready_queue;
pthread_t primary_exec;
pthread_t secondary_exec;
pthread_t third_exec;

pthread_mutex_t rq_mutex, incrementor;

long start_t, current_t, end_t;

int yield_count = 0;
int task_count = 0;
int completed_tasks = 0;

int concurrent_tasks = 0;
int task_creation_failures = 0;

struct timespec ts = {
	.tv_sec = 0,
	.tv_nsec = 300000
};

void new_concurrent_task(){
    pthread_mutex_lock(&incrementor);
    concurrent_tasks++;
    pthread_mutex_unlock(&incrementor);
}

void increment_completion(){
    pthread_mutex_lock(&incrementor);
    completed_tasks++;
    concurrent_tasks--;
    pthread_mutex_unlock(&incrementor);
}

void secondary_function(mco_coro *ctx){
    (void)ctx;
    int x = *(int *)(mco_get_user_data(mco_running()));
    int y = x*x;
    mco_yield(mco_running()); yield_count++;
    int z = y/3;
    (void)z;
    if(VERBOSE) printf("Completed %d\n",x);
}

void primary_function(mco_coro *ctx){
    (void)ctx;
    start_t = clock();
    printf("Creating tasks at CPU time %ld.\n",start_t);
    task_count = -1;
    int i = 0;
    int res;
    int attempts = 0;
    while(i < NUMBER_TASKS){
        mco_yield(mco_running()); yield_count++;
        if(concurrent_tasks >= MAX_CONCURRENT_TASKS){
            nanosleep(&ts, NULL);
            mco_yield(mco_running()); yield_count++;
            continue;
        }
        task *t = calloc(1, sizeof(task));
        t->id = i;
        t->fn = secondary_function;
        t->desc = mco_desc_init((t->fn),0);
        t->desc.user_data = &(t->id);
        if( (res = mco_create(&(t->ctx), &(t->desc))) != MCO_SUCCESS ){
            if(++attempts > 30){
                printf("Failed to create task %d after 30 attempts. %s\n",i,mco_result_description(res));
                mco_yield(mco_running()); yield_count++;
                i++;
                free(t);
                continue;
                // break;
            }else{
                printf("Failed to create task %d.",i);
                mco_yield(mco_running()); yield_count++;
                free(t);
                continue;
            }
        }
        if(attempts > 0)
            task_creation_failures++;
        attempts = 0;
        assert(mco_status(t->ctx) == MCO_SUSPENDED);
        new_concurrent_task();
        pthread_mutex_lock(&rq_mutex);
        struct queue_entry *entry = queue_new_node(t);
        queue_insert_tail(&ready_queue, entry);
        pthread_mutex_unlock(&rq_mutex);
        i++;
    }
    task_count = i;
    printf("Created %d of %d tasks.\n",task_count, NUMBER_TASKS);
}

void *primary_executor(void *args){
    (void)args;
    mco_coro *ctx = NULL;
    mco_desc desc = mco_desc_init(primary_function, 0);
    desc.user_data = NULL;
    int cres = mco_create(&ctx, &desc);
    assert(cres == MCO_SUCCESS);
    int pyields = 0;
    while(true){
        int res = mco_resume(ctx);
        if(res != MCO_SUCCESS){
            printf("Unexpected result in primary executor: %s\n",mco_result_description(res));
            mco_destroy(ctx);
            break;
        }else if(mco_status(ctx) == MCO_DEAD){
            printf("Primary executor finished with %d yields.\n",pyields);
            mco_destroy(ctx);
            break;
        }
        pyields++;
        nanosleep(&ts, NULL);
    }
    return NULL;
}

void *secondary_executor(void *args){
    (void)args;
    struct queue_entry *head = NULL;
    nanosleep(&ts, NULL); // wait for head to at least begin being populated
    int count=0;
    while(true){
        pthread_mutex_lock(&rq_mutex);
        head = queue_peek_front(&ready_queue);
        if(head == NULL){
            pthread_mutex_unlock(&rq_mutex);
            if(completed_tasks < NUMBER_TASKS){
                nanosleep(&ts, NULL);
                continue;
            }else{
                break;
            }
        }else{
            queue_pop_head(&ready_queue);
            pthread_mutex_unlock(&rq_mutex);
        }
        task *t = (task *)(head->data);
        assert(t != NULL);
        int res = mco_resume(t->ctx);
        if(res != MCO_SUCCESS){
            printf("Resuming secondary thread failed: %s\n",mco_result_description(res));
            free(head->data);
            free(head);
        }else if(mco_status(t->ctx) == MCO_SUSPENDED){
            pthread_mutex_lock(&rq_mutex);
            queue_insert_tail(&ready_queue, head);
            pthread_mutex_unlock(&rq_mutex);
        }else if(mco_status(t->ctx) == MCO_DEAD){
            mco_destroy(t->ctx);
            free(head->data);
            free(head);
            increment_completion();
            count++;
        }else{
            printf("Unexpected status received: %s\n",mco_result_description(res));
            mco_destroy(t->ctx);
            free(head->data);
            free(head);
        }
        nanosleep(&ts, NULL);
        
    }
    printf("%d tasks successfully terminated, secondary executor exiting.\n", count);
    return NULL;
}


void *third_executor(void *args){
    (void)args;
    while(true){
        end_t = clock();
        int completed = completed_tasks; // need a copy as this will likely change during execution
        double wtime = (double)(end_t - start_t) / CLOCKS_PER_SEC;
        double rate  = (double)(end_t - start_t) / completed;
        printf("After %f seconds, %d/%d tasks completed with CPU time per task: %f\n", wtime, completed, NUMBER_TASKS, rate);
        sleep(TIME_BETWEEN_STATUS_UPDATE);
    }
    return NULL;
}

int main(){
    pthread_mutex_init(&rq_mutex, NULL);
    pthread_mutex_init(&incrementor, NULL);

    ready_queue = queue_create();
    queue_init(&ready_queue);

    yield_count = 0;
    task_count = 0;
    completed_tasks = 0;

    pthread_create(&primary_exec, NULL, primary_executor, NULL);
    nanosleep(&ts, NULL);
    pthread_create(&secondary_exec, NULL, secondary_executor, NULL);
    nanosleep(&ts, NULL);
    pthread_create(&third_exec, NULL, third_executor, NULL);


    pthread_join(primary_exec, NULL);
    printf("pthread: joined primary executor\n");
    pthread_join(secondary_exec, NULL);
    printf("pthread: joined secondary executor\n");
    pthread_cancel(third_exec); // we needa cancel this since primary and secondary finished
    pthread_join(third_exec, NULL);
    end_t = clock();
    printf("Finished test with %d yields in %d completed tasks, and %d creation failures.\n",completed_tasks,yield_count,task_creation_failures);
    double total_time = (double)(end_t - start_t) / CLOCKS_PER_SEC / 60;
    printf("Test ending in %f minutes.\n",total_time);
    pthread_exit(NULL);

    return 0;

}

// */
#endif