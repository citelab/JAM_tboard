
#include "tboard.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>

#define MINICORO_IMPL
#define MINICORO_ASM

// Uncomment following to zero stack memory. Affects performance
//#define MCO_ZERO_MEMORY
// Uncomment following to run with valgrind properly (otherwise valgrind
// will be unable to access memory). Affects performance
#define MCO_USE_VALGRIND

#include <minicoro.h>
#include "queue/queue.h"

////////////////////////////////////////////
//////////// TBOARD FUNCTIONS //////////////
////////////////////////////////////////////

tboard_t* tboard_create(int secondary_queues)
{
    // create tboard
    assert(secondary_queues <= MAX_SECONDARIES);

    tboard_t *tboard = (tboard_t *)calloc(1, sizeof(tboard_t)); // allocate memory for tboard
                                                                // free'd in tboard_destroy()

    // assert that remote_task_t and task_t are different sizes. If they are the same size,
    // then undefined behavior will occur when issuing blocking/remote tasks.
    assert(sizeof(remote_task_t) != sizeof(task_t));

    // initiate primary queue's mutex and condition variables
    assert(pthread_mutex_init(&(tboard->cmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->tmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->hmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->emutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->msg_mutex), NULL) == 0);
    assert(pthread_cond_init(&(tboard->tcond), NULL) == 0);
    assert(pthread_cond_init(&(tboard->msg_cond), NULL) == 0);

    // create and initialize primary queues
    assert(pthread_mutex_init(&(tboard->pmutex), NULL) == 0);
    assert(pthread_cond_init(&(tboard->pcond), NULL) == 0);

    tboard->pqueue = queue_create();

    queue_init(&(tboard->pqueue));

    // set number of secondaries tboard has
    tboard->sqs = secondary_queues;

    for (int i=0; i<secondary_queues; i++) {
        // create & initialize secondary i's mutex, cond, queues
        assert(pthread_mutex_init(&(tboard->smutex[i]), NULL)==0);
        assert(pthread_cond_init(&(tboard->scond[i]), NULL) == 0);

        tboard->squeue[i] = queue_create();

        queue_init(&(tboard->squeue[i]));
    }

    // initialize remote message queues
    tboard->msg_sent = queue_create();
    tboard->msg_recv = queue_create();
    queue_init(&(tboard->msg_sent));
    queue_init(&(tboard->msg_recv));



    tboard->status = 0; // indicate its been created but not started
    tboard->shutdown = 0;
    tboard->task_count = 0; // how many concurrent tasks are running
    tboard->exec_hist = NULL;

    return tboard; // return address of tboard in memory
}

void tboard_start(tboard_t *tboard)
{
    // we want to start the threads for tboard executor
    // for this we allocate argument sent to executor function so it knows what to do
    // then we create the thread
    if (tboard == NULL || tboard->status != 0)
        return; // only want to start an initialized tboard
    
    // create primary executor
    exec_t *primary = (exec_t *)calloc(1, sizeof(exec_t));
    primary->type = PRIMARY_EXEC;
    primary->num = 0;
    primary->tboard = tboard;
    pthread_create(&(tboard->primary), NULL, executor, primary);

    // save it incase we call kill so we can free memory
    tboard->pexect = primary;

    // create secondary executors
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
    // wait for threads to terminate before destroying task board
    pthread_join(tboard->primary, NULL);
    for (int i=0; i<tboard->sqs; i++) {
        pthread_join(tboard->secondary[i], NULL);
    }
    
    // broadcast that threads have all terminated to any thread sleeping on condition variable
    pthread_mutex_lock(&(tboard->emutex)); // exit mutex
    pthread_cond_broadcast(&(tboard->tcond)); // incase multiple threads are waiting
    pthread_mutex_unlock(&(tboard->emutex));

    // lock tmutex. If we get lock, it means that user has taken all necessary data
    // from task board as it should be locked before tboard_kill() is run
    pthread_mutex_lock(&(tboard->tmutex));

    // destroy mutex and condition variables 
    pthread_mutex_destroy(&(tboard->cmutex));
    pthread_mutex_destroy(&(tboard->pmutex));
    pthread_cond_destroy(&(tboard->pcond)); // broadcasted in tboard_kill()
    for (int i=0; i<tboard->sqs; i++) {
        pthread_mutex_destroy(&(tboard->smutex[i]));
        pthread_cond_destroy(&(tboard->scond[i])); // broadcasted in tboard_kill();
    }
    pthread_cond_destroy(&(tboard->tcond));


    // empty task queues and destroy any persisting contexts
    for (int i=0; i<tboard->sqs; i++) {
        struct queue_entry *entry = queue_peek_front(&(tboard->squeue[i]));
        while (entry != NULL) {
            queue_pop_head(&(tboard->squeue[i]));
            task_destroy((task_t *)(entry->data)); // destroys task_t and coroutine
            free(entry);
            entry = queue_peek_front(&(tboard->squeue[i]));
        }
    }
    struct queue_entry *entry = queue_peek_front(&(tboard->pqueue));
    while (entry != NULL) {
        queue_pop_head(&(tboard->pqueue));
        task_destroy((task_t *)(entry->data)); // destroys task_t and coroutine
        free(entry);
        entry = queue_peek_front(&(tboard->pqueue));
    }

    // empty outgoing remote task message queues
    struct queue_entry *msg = queue_peek_front(&(tboard->msg_sent));
    while (msg != NULL) {
        queue_pop_head(&(tboard->msg_sent));
        remote_task_destroy((remote_task_t *)(msg->data)); // destroys remote_task_t
                                                           // and any parent task_t's
        free(msg);
        msg = queue_peek_front(&(tboard->msg_sent));
    }
    // empty incoming remote task message queues
    msg = queue_peek_front(&(tboard->msg_recv));
    while (msg != NULL) {
        queue_pop_head(&(tboard->msg_recv));
        remote_task_destroy((remote_task_t *)(msg->data)); // destroys remote_task_t
                                                           // and any parent task_t's
        free(msg);
        msg = queue_peek_front(&(tboard->msg_recv));
    }

    // unlock tmutex so we can destroy it
    pthread_mutex_unlock(&(tboard->tmutex));
    
    // broadcast MQTT msg_cond condition variable before destroying
    pthread_cond_broadcast(&(tboard->msg_cond)); // incase MQTT is waiting on this
    pthread_cond_destroy(&(tboard->msg_cond));

    // free executor arguments
    free(tboard->pexect);
    for (int i=0; i<tboard->sqs; i++) {
        free(tboard->sexect[i]);
    }
    
    // destroy history mutex
    history_destroy(tboard);

    // destroy rest of task board mutexes
    pthread_mutex_destroy(&(tboard->hmutex));
    pthread_mutex_destroy(&(tboard->tmutex));
    pthread_mutex_destroy(&(tboard->emutex));
    pthread_mutex_destroy(&(tboard->msg_mutex));

    // free task board object
    free(tboard);
}

bool tboard_kill(tboard_t *t)
{
    if (t == NULL || t->status == 0)
        return false;
    
    // lock emutex to before queueing executor thread cancellation
    pthread_mutex_lock(&(t->emutex));
    // indicate to taskboard that shutdown is occuring
    t->shutdown = 1;

    // queue primary executor thread cancellation, signal condition variable
    pthread_mutex_lock(&(t->pmutex));
    pthread_cancel(t->primary);
    pthread_cond_signal(&(t->pcond));
    pthread_mutex_unlock(&(t->pmutex));

    for (int i=0; i<t->sqs; i++) {
        // queue secondary executor thread i cancellation, signal condition variable
        pthread_mutex_lock(&(t->smutex[i]));
        pthread_cancel(t->secondary[i]);
        pthread_cond_signal(&(t->scond[i]));
        pthread_mutex_unlock(&(t->smutex[i]));
    }
    
    // wait for executor threads to terminate fully
    pthread_cond_wait(&(t->tcond), &(t->emutex)); // will be signaled by tboard_destroy once threads exit
    // unlock emutex so it can be destroyed
    pthread_mutex_unlock(&(t->emutex));
    // task board has been killed so we return true
    return true;
}

void tboard_exit()
{
    pthread_exit(NULL);
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
    // non-zero value indicates we can add a new task
    int ret = 0;
    pthread_mutex_lock(&(t->cmutex));
    if (DEBUG && t->task_count < 0)
        tboard_log("tboard_add_concurrent: Invalid task_count encountered: %d\n",t->task_count);

    if (t->task_count < MAX_TASKS)
        ret = ++(t->task_count);
    pthread_mutex_unlock(&(t->cmutex));
    return ret;
}


////////////////////////////////////////
/////////// TASK FUNCTIONS /////////////
////////////////////////////////////////

void remote_task_place(tboard_t *t, remote_task_t *rtask, bool send)
{
    // place remote task in appropriate queue
    if (t == NULL || rtask == NULL)
        return;

    // lock queue mutex before modifications
    pthread_mutex_lock(&(t->msg_mutex));
    struct queue_entry *entry = queue_new_node(rtask);

    if (send) { // we want it in outgoing remote message queue
        queue_insert_tail(&(t->msg_sent), entry);
        pthread_cond_signal(&(t->msg_cond));
    } else { // we want it in incoming remote message queue
        queue_insert_tail(&(t->msg_recv), entry);
        pthread_cond_signal(&(t->pcond)); // wake at least one executor so sequencer can run
    }
    pthread_mutex_unlock(&(t->msg_mutex));
}

bool remote_task_create(tboard_t *t, char *message, void *args, size_t sizeof_args, bool blocking)
{
    (void)t;
    if (mco_running() == NULL) // must be called from a coroutine!
        return false;

    mco_result res;

    // create rtask object
    remote_task_t rtask = {0};
    rtask.status = TASK_INITIALIZED;
    rtask.data = args;
    rtask.data_size = sizeof_args;
    rtask.blocking = blocking;
    int length = strlen(message);
    if(length > MAX_MSG_LENGTH){
        tboard_err("remote_task_create: Message length exceeds maximum supported value (%d > %d).\n",length, MAX_MSG_LENGTH);
        return false;
    }
    // copy message to rtask object
    memcpy(rtask.message, message, length);

    // push rtask into storage. This copies memory in current thread so we dont have
    // to worry about invalid reads
    res = mco_push(mco_running(), &rtask, sizeof(remote_task_t));
    if (res != MCO_SUCCESS) {
        tboard_err("remote_task_create: Failed to push remote task to mco storage interface.\n");
        return false;
    }
    // issued remote task, yield
    task_yield();
    // we have received control again of the task.
    if (!blocking) // if not blocking, return true and continue execution
        return true;

    // blocking: get if remote_task_t is currently in storage. If so we must parse it
    if (mco_get_bytes_stored(mco_running()) == sizeof(remote_task_t)) {
        res = mco_pop(mco_running(), &rtask, sizeof(remote_task_t));
        if (res != MCO_SUCCESS) {
            tboard_err("remote_task_create: Failed to pop remote task from mco storage interface.\n");
            return false;
        }
        // check if task completed
        if (rtask.status == TASK_COMPLETED) {
            return true;
        } else {
            tboard_err("remote_task_create: Blocking remote task is not marked as completed: %d.\n",rtask.status);
            return false;
        }
    } else {
        tboard_err("remote_task_create: Failed to capture blocking remote task after termination.\n");
        return false;
    }
    
}

void remote_task_destroy(remote_task_t *rtask)
{
    if (rtask == NULL)
        return;
    // check if task is blocking. If it is, then we must destroy task
    if (rtask->blocking) {
        // this will recursively destroy parents if nested blocking tasks have been issued
        task_destroy(rtask->calling_task);
    }
    // free alloc'd data if applicable
    if (rtask->data_size > 0 && rtask->data != NULL)
        free(rtask->data);
    
    // free rtask object
    free(rtask);
}

bool blocking_task_create(tboard_t *t, function_t fn, int type, void *args, size_t sizeof_args)
{
    if (mco_running() == NULL) // must be called from a coroutine!
        return false;
    
    mco_result res;

    // create task object
    task_t task = {0};
    task.status = TASK_INITIALIZED;
    task.type = type; // tagged arbitrarily, will assume parents position
    task.id = TASK_ID_BLOCKING;
    task.fn = fn;
    task.desc = mco_desc_init((task.fn.fn), 0);
    task.desc.user_data = args;
    task.data_size = sizeof_args;
    task.parent = NULL;
    task.hist = NULL;

    // add task to history
    history_record_exec(t, &task, &(task.hist));
    task.hist->executions += 1; // increase execution count

    // create coroutine context
    if ( (res = mco_create(&(task.ctx), &(task.desc))) != MCO_SUCCESS ) {
        tboard_err("blocking_task_create: Failed to create coroutine: %s.\n",mco_result_description(res));
        return false;
    } else { // context creation successful
        // push task_t to storage
        res = mco_push(mco_running(), &task, sizeof(task_t));
        if (res != MCO_SUCCESS) {
            tboard_err("blocking_task_create: Failed to push task to mco storage interface.\n");
            return false;
        }
        // yield so executor can run blocking task
        task_yield();
        // we got control back meaning blocking task should have executed.
        // check if task_t worth of memory is in storage
        if (mco_get_bytes_stored(mco_running()) == sizeof(task_t)) {
            // attempt to pop task_t from storage
            res = mco_pop(mco_running(), &task, sizeof(task_t));
            if (res != MCO_SUCCESS) {
                tboard_err("blocking_task_create: Failed to pop task from mco storage interface.\n");
                return false;
            }
            if (task.status == TASK_COMPLETED) { // task completed
                return true;
            } else {
                tboard_err("blocking_task_create: Blocking task is not marked as completed: %d.\n", task.status);
                return false;
            }
        } else {
            tboard_err("blocking_task_create: Failed to capture blocking task after termination.\n");
            return false;
        }
    }
}

void task_place(tboard_t *t, task_t *task)
{
    // add task to ready queue
    if(task->type <= PRIMARY_EXEC || t->sqs == 0) {
        // task should be added to primary ready queue
        pthread_mutex_lock(&(t->pmutex)); // lock primary mutex
        struct queue_entry *task_q = queue_new_node(task); // create queue entry
        if (task->type == PRIORITY_EXEC)
            queue_insert_head(&(t->pqueue), task_q); // insert queue entry to head
        else
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
}

bool task_add(tboard_t *t, task_t *task)
{
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
    task_place(t, task);
    return true;
}

bool task_create(tboard_t *t, function_t fn, int type, void *args, size_t sizeof_args)
{
    if (t == NULL)
        return false;

    mco_result res;

    // create task_t object
    task_t *task = calloc(1, sizeof(task_t));
    task->status = TASK_INITIALIZED;
    task->type = type;
    task->id = TASK_ID_NONBLOCKING;
    task->fn = fn;
    // create description and populate it with argument
    task->desc = mco_desc_init((task->fn.fn), 0);
    task->desc.user_data = args;
    task->data_size = sizeof_args;
    // non-blocking task so no parent
    task->parent = NULL;
    // create coroutine
    if ( (res = mco_create(&(task->ctx), &(task->desc))) != MCO_SUCCESS ) {
        tboard_err("task_create: Failed to create coroutine: %s.\n",mco_result_description(res));
        
        free(task);
        return false;
    } else {
        // attempt to add task to tboard
        bool added = task_add(t, task);
        if (!added){
            mco_destroy(task->ctx); // we must destroy stack allocated in mco_create() on failure
            free(task); // free task, as it turns out we cannot use it
        }
        return added;
    }
}

void task_destroy(task_t *task)
{
    if (task == NULL)
        return;
    // check if parent task exists. If it does, it is not in any ready
    // queue so we must destroy it (does it recursively)
    if (task->parent != NULL)
        task_destroy(task->parent);
    // destroy user data if applicable
    if (task->data_size > 0 && task->desc.user_data != NULL)
        free(task->desc.user_data);
    // destroy coroutine
    mco_destroy(task->ctx);
    // free task_t
    free(task);
}

void task_yield()
{
    // yield currently running task
    mco_yield(mco_running());
}

void *task_get_args()
{
    // get arguments of currently running task
    return mco_get_user_data(mco_running());
}

///////////////////////////////////////////////////
/////////// Logging functionality /////////////////
///////////////////////////////////////////////////

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