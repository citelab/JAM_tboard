
#include "tboard.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>

#define MINICORO_IMPL
#define MINICORO_ASM
#define MCO_ZERO_MEMORY
#define MCO_USE_VALGRIND

#include <minicoro.h>
#include "queue/queue.h"




tboard_t* tboard_create(int secondary_queues)
{
    // create tboard
    assert(secondary_queues <= MAX_SECONDARIES);

    tboard_t *tboard = (tboard_t *)calloc(1, sizeof(tboard_t)); // allocate memory for tboard



    // initiate primary queue's mutex and condition variables
    assert(pthread_mutex_init(&(tboard->cmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->tmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->hmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->emutex), NULL) == 0);
    assert(pthread_cond_init(&(tboard->tcond), NULL) == 0);

    // create and initialize primary queues
    assert(pthread_mutex_init(&(tboard->pmutex), NULL) == 0);
    assert(pthread_cond_init(&(tboard->pcond), NULL) == 0);

    tboard->pqueue = queue_create();
    tboard->pwait = queue_create();

    queue_init(&(tboard->pqueue));
    queue_init(&(tboard->pwait));

    // set number of secondaries tboard has
    tboard->sqs = secondary_queues;

    for (int i=0; i<secondary_queues; i++) {
        // create & initialize secondary i's mutex, cond, queues
        assert(pthread_mutex_init(&(tboard->smutex[i]), NULL)==0);
        assert(pthread_cond_init(&(tboard->scond[i]), NULL) == 0);

        tboard->squeue[i] = queue_create();
        tboard->swait[i] = queue_create();

        queue_init(&(tboard->squeue[i]));
        queue_init(&(tboard->swait[i]));
    }
    tboard->status = 0; // indicate its been created but not started
    tboard->shutdown = 0;
    tboard->task_count = 0; // how many concurrent tasks are running
    tboard->exec_hist = NULL;
    tboard->task_list = NULL;

    return tboard; // return address of tboard in memory
}

void tboard_start(tboard_t *tboard)
{
    // we want to start the threads for tboard executor
    // for this we allocate argument sent to executor function so it knows what to do
    // then we create the thread
    if (tboard == NULL || tboard->status != 0)
        return; // only want to start an initialized tboard
    
    exec_t *primary = (exec_t *)calloc(1, sizeof(exec_t));
    primary->type = PRIMARY_EXEC;
    primary->num = 0;
    primary->tboard = tboard;
    pthread_create(&(tboard->primary), NULL, executor, primary);

    // save it incase we call kill so we can free memory
    tboard->pexect = primary;

    for (int i=0; i<tboard->sqs; i++) {
        exec_t *secondary = (exec_t *)calloc(1, sizeof(exec_t));
        secondary->type = SECONDARY_EXEC;
        secondary->num = i;
        secondary->tboard = tboard;
        pthread_create(&(tboard->secondary[i]), NULL, executor, secondary);
        // save it incase we call kill so we can free memory
        tboard->sexect[i] = secondary;
    }

    tboard->status = 1; // started

}

void tboard_destroy(tboard_t *tboard)
{
    // wait for threads to finish before deleting
    // we tell it to shutdown and then wait for threads to terminate
    //tboard->init_shutdown = 1;
    pthread_join(tboard->primary, NULL);
    for (int i=0; i<tboard->sqs; i++) {
        pthread_join(tboard->secondary[i], NULL);
    }
    pthread_mutex_lock(&(tboard->emutex));
    pthread_cond_signal(&(tboard->tcond)); // incase multiple threads are waiting
    pthread_mutex_unlock(&(tboard->emutex));

    pthread_mutex_lock(&(tboard->tmutex));
    pthread_mutex_destroy(&(tboard->cmutex));

    pthread_mutex_destroy(&(tboard->pmutex));
    pthread_cond_destroy(&(tboard->pcond));

    for (int i=0; i<tboard->sqs; i++) {
        pthread_mutex_destroy(&(tboard->smutex[i]));
        pthread_cond_destroy(&(tboard->scond[i]));
    }
    // empty task queues and destroy any persisting contexts
    
    pthread_cond_destroy(&(tboard->tcond));

    // TODO: capture currently running tasks in some capacity
    for (int i=0; i<tboard->sqs; i++) {
        struct queue_entry *entry = queue_peek_front(&(tboard->squeue[i]));
        while (entry != NULL) {
            queue_pop_head(&(tboard->squeue[i]));
            mco_destroy(((task_t *)(entry->data))->ctx);
            if (((task_t *)(entry->data))->data_size > 0 && ((task_t *)(entry->data))->desc.user_data != NULL)
                free(((task_t *)(entry->data))->desc.user_data);
            free(entry->data);
            free(entry);
            entry = queue_peek_front(&(tboard->squeue[i]));
        }
    }
    struct queue_entry *entry = queue_peek_front(&(tboard->pqueue));
    while (entry != NULL) {
        queue_pop_head(&(tboard->pqueue));
        mco_destroy(((task_t *)(entry->data))->ctx);
        if (((task_t *)(entry->data))->data_size > 0 && ((task_t *)(entry->data))->desc.user_data != NULL)
            free(((task_t *)(entry->data))->desc.user_data);
        free(entry->data);
        free(entry);
        entry = queue_peek_front(&(tboard->pqueue));
    }
    pthread_mutex_unlock(&(tboard->tmutex));
    
    free(tboard->pexect);
    for (int i=0; i<tboard->sqs; i++) {
        free(tboard->sexect[i]);
    }
    history_destroy(tboard);
    pthread_mutex_destroy(&(tboard->hmutex));
    pthread_mutex_destroy(&(tboard->tmutex));
    pthread_mutex_destroy(&(tboard->emutex));
    free(tboard);
}

bool tboard_kill(tboard_t *t)
{
    if (t == NULL || t->status == 0)
        return false;
    
    pthread_mutex_lock(&(t->emutex));
    t->shutdown = 1;

    pthread_mutex_lock(&(t->pmutex));
    pthread_cancel(t->primary);
    pthread_cond_signal(&(t->pcond));
    pthread_mutex_unlock(&(t->pmutex));

    for (int i=0; i<t->sqs; i++) {
        pthread_mutex_lock(&(t->smutex[i]));
        pthread_cancel(t->secondary[i]);
        pthread_cond_signal(&(t->scond[i]));
        pthread_mutex_unlock(&(t->smutex[i]));
    }
    
    pthread_cond_wait(&(t->tcond), &(t->emutex)); // will be signaled by tboard_destroy once threads exit
    pthread_mutex_unlock(&(t->emutex));
    // free allocated data to exec_t
    /*free(t->pexect);
    for (int i=0; i<t->sqs; i++) {
        free(t->sexect[i]);
    }*/
    return true;
}

int tboard_get_concurrent(tboard_t *t){
    pthread_mutex_lock(&(t->cmutex));
    int ret = t->task_count;
    pthread_mutex_unlock(&(t->cmutex));
    return ret;
}

void tboard_inc_concurrent(tboard_t *t){
    pthread_mutex_lock(&(t->cmutex));
    t->task_count++;
    pthread_mutex_unlock(&(t->cmutex));
}

void tboard_deinc_concurrent(tboard_t *t){
    pthread_mutex_lock(&(t->cmutex));
    t->task_count--;
    pthread_mutex_unlock(&(t->cmutex));
}

int tboard_add_concurrent(tboard_t *t){
    int ret = 0;
    pthread_mutex_lock(&(t->cmutex));
    if (DEBUG && t->task_count < 0)
        tboard_log("tboard_add_concurrent: Invalid task_count encountered: %d\n",t->task_count);

    if (t->task_count < MAX_TASKS)
        ret = ++(t->task_count);
    pthread_mutex_unlock(&(t->cmutex));
    return ret;
}

bool task_add(tboard_t *t, task_t *task){
    if (t == NULL || task == NULL)
        return false;
    
    // check if we have reached maximum concurrent tasks
    if(tboard_add_concurrent(t) == 0)
        return false;

    // initialize internal values
    task->cpu_time = 0;
    task->yields = 0;
    task->status = TASK_INITIALIZED;
    task->hist = NULL;
    // add task to history
    history_record_exec(t, task, &(task->hist));
    task->hist->executions += 1; // increase execution count
    // add task to ready queue
    if(task->type <= PRIMARY_EXEC || t->sqs == 0) {
        // task should be added to primary ready queue
        pthread_mutex_lock(&(t->pmutex)); // lock primary mutex
        struct queue_entry *task_q = queue_new_node(task); // create queue entry
        queue_insert_tail(&(t->pqueue), task_q); // insert queue entry to tail
        pthread_cond_signal(&(t->pcond)); // signal primary condition variable as only one 
                                          // thread will ever wait for pcond
        pthread_mutex_unlock(&(t->pmutex)); // unlock mutex
    } else {
        // task should be added to secondary ready queue
        int j = rand() % (t->sqs); // randomly select secondary queue
        
        pthread_mutex_lock(&(t->smutex[j])); // lock secondary mutex
        struct queue_entry *task_q = queue_new_node(task); // create queue entry
        queue_insert_tail(&(t->squeue[j]), task_q); // insert queue entry to tail
        pthread_cond_signal(&(t->scond[j])); // signal secondary condition variable as only
                                             // one thread will ever wait for pcond

        if (SIGNAL_PRIMARY_ON_NEW_SECONDARY_TASK == 1)
            pthread_cond_signal(&(t->pcond)); // signal primary condition variable

        pthread_mutex_unlock(&(t->smutex[j])); // unlock mutex
    }
    
    return true;
}

bool task_create(tboard_t *t, function_t fn, int type, void *args, size_t sizeof_args)
{
    mco_result res;
    task_t *task = calloc(1, sizeof(task_t));

    task->status = 1;
    task->type = type;
    task->id = 0;
    task->fn = fn;
    task->desc = mco_desc_init((task->fn.fn), 0);
    task->desc.user_data = args;
    task->data_size = sizeof_args;
    if ( (res = mco_create(&(task->ctx), &(task->desc))) != MCO_SUCCESS ) {
        tboard_err("task_create: Failed to create coroutine: %s.\n",mco_result_description(res));
        
        free(task);
        return false;
    } else {
        bool added = task_add(t, task);
        if (!added){
            mco_destroy(task->ctx); // we must destroy stack allocated in mco_create() on failure
            free(task); // free task, as it turns out we cannot use it
        }
        return added;
    }
}

void tboard_exit()
{
    pthread_exit(NULL);
}

void task_yield()
{
    mco_yield(mco_running());
}

void *task_get_args()
{
    return mco_get_user_data(mco_running());
}

int task_retrieve_data(void *data, size_t size)
{
    return mco_pop(mco_running(), data, size);
}

int task_store_data(void *data, size_t size)
{
    return mco_push(mco_running(), data, size);
}


/////////// Logging functionality /////////////////

int tboard_log(char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    printf("Logging: ");
    result = vprintf(format, args);
    fflush(stdout);
    va_end(args);
    return result;
}
int tboard_err(char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Error: ");
    result = vfprintf(stderr, format, args);
    fflush(stderr);
    va_end(args);
    return result;
}