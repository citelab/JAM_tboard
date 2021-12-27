/* The main header file for our project */

#ifndef __TBOARD_H__
#define __TBOARD_H__


#include <sys/queue.h>
#include "queue/queue.h"


#include <minicoro.h>
#include <uthash.h>

#include <stdbool.h>
#include <pthread.h>

#define TBOARD_SIGNAL_ON_FREE_TASK 1


// TODO: figure out proper way to document macros, and determine all required macros
#define SMALL_TASK_TIME 300
// EST = earliest start time, LST = latest start time
#define MAX_TASKS 1024
#define MAX_SECONDARIES 10

#define PRIORITY_EXEC -1
#define PRIMARY_EXEC 0
#define SECONDARY_EXEC 1

#define STACK_SIZE 58196

#define SPIN_BLOCK_ITERATIONS 50

#define TASK_EXEC 0 // for msg_processor
#define TASK_SCHEDULE 1 // for msg_processor

#define SIGNAL_PRIMARY_ON_NEW_SECONDARY_TASK 1

#define DEBUG 0

#define TASK_INITIALIZED 1
#define TASK_RUNNING 2
#define TASK_COMPLETED 3


///////////////////////////////////////////////////////////////////
///////////////////////// Important Typedefs //////////////////////
///////////////////////////////////////////////////////////////////

/**
 * context_t - Coroutine context type.
 * 
 * As we are using minicoro library to handle coroutines, type is mco_coro*
 */
typedef mco_coro* context_t;

/**
 * context_desc - Coroutine description object.
 * 
 * As we are using minicoro library to handle coroutines, type is mco_desc
 */
typedef mco_desc context_desc;

/**
 * tb_task_f - Task function prototype.
 * @arg: Passed by coroutine library.
 * 
 * Task functions must have signature `void fn(void *args)`. This typedef reflects
 * this signature when passing functions.
 */
typedef void (*tb_task_f)(void *);


//////////////////////////////////////////////////////
/////////// TBoard Structure Definitions /////////////
//////////////////////////////////////////////////////

/**
 * function_t - Structure containing crucial function information
 * @fn:      function pointer
 * @fn_name: common function name
 * 
 * This structure is essential for efficiently recording and serializing function
 * execution information in our history hash table. To pass a function to task_t,
 * instead of calling task_create(..., fn, ...) or setting task->fn = fn, one can 
 * simply call task_create(..., TBOARD_FUNC(fn), ...) or set task->fn = TBOARD_FUNC(fn);
 */
typedef struct {
    tb_task_f fn;
    const char *fn_name;
} function_t;

#define TBOARD_FUNC(func) (function_t){.fn = func, .fn_name = #func}

struct history_t;
struct exec_t;

/**
 * task_t - Data type containing task information
 * @id:         Task ID representing location in memory for preallocated task_list.
 * @status:     Status of current task
 *              @status == 0: task was issued
 *              @status == 1: task is running
 *              @status == 2: task has terminated
 * @type:       Task type.
 *              @type == PRIORITY_EXEC:  Highest priority primary task
 *              @type == PRIMARY_EXEC:   Primary task
 *              @type == SECONDARY_EXEC: Secondary task
 * @cpu_time:   CPU time of task execution 
 * @yields:     Count of yields by task
 * @fn:         Task function to be run by task executor as function_t.
 * @ctx:        Task function context.
 * @desc:       Coroutine description structure.
 * @hist:       Pointer to history_t object in hash table
 * 
 * Structure contains all necessary information relating to a task.
 * TODO: add more description
 */
typedef struct {
    int id;
    int status;
    int type;
    int cpu_time;
    int yields;
    function_t fn;
    context_t ctx;
    context_desc desc;
    struct history_t *hist;
} task_t;





/**
 * tboard_t - Task Board object.
 * @primary:    Thread of primary task executor (pExecutor)
 * @secondary:  Threads of secondary task executors (sExecutor)
 * @pcond:      Condition variable of pExecutor
 * @scond:      Condition variables of sExecutor
 * @pmutex:     Mutex of pExecutor
 * @smutex:     Mutexs of sExecutor
 * @cmutex:     Task count mutex, locked when changing concurrent task count
 * @tmutex:     Task board mutex, locking only when significantly modifying tboard 
 * @tcond:      Task board condition variable. This signals once all task executor threads
 *              have been joined in tboard_destroy()
 * @pqueue:     Primary task ready queue
 * @pwait:      Primary wait queue
 * @squeue:     Secondary task ready queues
 * @swait:      Secondary wait queues
 * @sqs:        Number of secondary ready queues and executors
 * @task_list:  List of task_t task objects. Number of possible concurrent
 *              tasks is defined in MAX_TASKS macro
 * @task_count: Tracks the number of concurrent tasks running in task board
 * @exec_hist:  Task execution history hash table
 * @pexect:     pointer to pExecutor argument
 * @sexect:     pointer to sExecutor arguments
 * @status:     Task board status.
 *              @status == 0: Task Board has been created
 *              @status == 1: Task Board has started
 * 
 * Task board object contains all relevant information of task board, which is passed between task board
 * functions. All task board functionality is dependant on this object. This object is created and
 * initialized in function tboard_create(). Task Board is started in tboard_start(). Task board object is
 * properly destroyed in tboard_destroy().
 */
typedef struct {

    pthread_t primary;
    pthread_t secondary[MAX_SECONDARIES];

    pthread_cond_t pcond;
    pthread_cond_t scond[MAX_SECONDARIES];

    pthread_mutex_t pmutex;
    pthread_mutex_t smutex[MAX_SECONDARIES];

    pthread_mutex_t cmutex;
    pthread_mutex_t tmutex;
    pthread_cond_t tcond;

    pthread_mutex_t hmutex;

    struct queue pqueue;
    struct queue pwait;
    struct queue squeue[MAX_SECONDARIES];
    struct queue swait[MAX_SECONDARIES];

    int sqs;

    // task_t task_list[MAX_TASKS];
    task_t *task_list;
    int task_count;

    struct history_t *exec_hist;

    struct exec_t *pexect;
    struct exec_t *sexect[MAX_SECONDARIES];

    //int init_shutdown; // should be set to 0 unless told to end after all tasks are completed

    int status;
} tboard_t;

/**
 * exec_t - Argument passed to task executor.
 * @type:   indicates whether task executor is primary or secondary.
 * @num:    If TExec is sExecutor, then @num identifies sExecutor.
 * @tboard: Reference to task board.
 * 
 * This type is exclusively used by tboard_start(), where it is created, and tboard_destroy() where
 * it is freed.
 * 
 * Objects of this type are passed to executor() by tboard_start(), dictating executor() functionality.
 */
typedef struct exec_t { // passed to executor thread so it knows what to do
    int type;
    int num;
    tboard_t *tboard;
} exec_t;



///////////////////////////////////////////////
/////////// Scheduler Definitions /////////////
///////////////////////////////////////////////

/**
 * struct __schedule_t - Schedule type
 * @tboard: Reference to task board
 * 
 * Currently unimplemented.
 */
struct __schedule_t{
    tboard_t *tboard;
};

///////////////////////////////////////////////
/////////// Sequencer Definitions /////////////
///////////////////////////////////////////////

void task_sequencer(tboard_t *tboard);
/** task_sequencer() - TSeq; Rearranges ready queues for priority task execution.
 * @tboard: pointer to taskboard object.
 * 
 * Not fully implemented, the idea of this function is to resequence the ready queues
 * so that tasks with higher priorities and/or closer deadlines are executed in a
 * timely manner. We run this function in the executor before popping the head from
 * the respective ready queue. 
 * 
 * It is at the discretion of the sequencer to determine if it has been run recently. it
 * is a good idea to resequence the queues when a new priority tasks are added.
 */

/////////////////////////////////////////////////
//////////// Executor Definitions ///////////////
/////////////////////////////////////////////////

void *executor(void *arg);
/** 
 * executor() - Task Executor (TExec); Thread function that handles task execution.
 * @arg: pthread argument passed from pthread_create().
 *       this argument is a pointer to type exec_t.
 * 
 * The task executor runs tasks from the respective ready queues. Based on the provided 
 * argument, this function determines whether it is a primary or secondary executor thread.
 * 
 * If primary executor (pExecutor), this is the "main thread" of the tBoard. This executor
 * handles the primary queues. Essential tasks (tasks that have dependancies/deadlines) are
 * run by this executor. If there are no tasks pending in the primary ready queue, or if 
 * there are tasks before earliest start time (EST), then pExecutor may run tasks from a
 * secondary ready queue, returning them to their original queue on task_yield(). Should
 * pExecutor not find a task to run, it will sleep on the primary condition variable 
 * tBoard->pCond (no_work).
 * 
 * If secondary executor (sExecutor), then tasks will be pulled only from respective
 * secondary ready queue. If there are no tasks in queue, sExecutor will sleep on 
 * respective condition variable tBoard->sCond[i].
 * 
 * Pulling tasks from ready queues has two phases:
 * * spin-block phase: 
 * * *    to save overhead from frequent sleeping/waking on condition variables, executor
 * * *    will poll the ready queue for a preset number of iterations before entering the
 * * *    sleep-wake phase. Number of iterations is defined in SPIN_BLOCK_ITERATIONS macro.
 * 
 * * sleep-wake phase:
 * * *    after spin-block phase, executor will sleep on condition variable defined above.
 * * *    Condition variable will signal when a task is added into respective ready queue.
 * 
 * Task executors will run as described indefinitely until task board is instructed to
 * terminate via special function tboard_kill().
 * 
 * Context: Function will run in it's own thread, created in tboard_start().
 * Context: Function will sleep on condition variables described above
 * Context: TODO: add contextx
 * 
 * 
 * 
 */




//////////////////////////////////////////////////
///////////// TBoard Definitions /////////////////
//////////////////////////////////////////////////

tboard_t* tboard_create(int secondary_queues);
/**
 * tboard_create() - Creates task board object.
 * @secondary_queues: Number of secondary queues tboard should have.
 * 
 * This function allocates and initializes task board object.
 * 
 * Primary and secondary ready queues and wait queues are created and initialized.
 * Primary and secondary mutex and condition variables are initialized. tboard->status
 * will be set to 0, indicating that task board was created but has not started yet.
 * 
 * 
 * Context: Free allocated memory associated with task board object is freed in tboard_destroy()
 * 
 * Return: tboard_t type pointer refering to allocated task board object is returned.
 */

void tboard_start(tboard_t *tboard);
/**
 * tboard_start() - Starts task board.
 * @tboard: tboard_t pointer of task board to start
 * 
 * This function will create task executor threads (pExecutor and sExecutor). Thread references
 * are stored on pthread_t variables tboard->primary and tboard->secondary[]. It will allocate
 * exec_t arguments passed to executor() to indicate executor type. Pointer to allocated memory
 * is stored in respective exec_t pointer of tboard object (pexect and sexect for pExecutor and
 * sExecutor respectively).
 * 
 * Context: Creates threads referenced in @tboard.
 */

void tboard_destroy(tboard_t *t);
/**
 * tboard_destroy() - Destroy task board on completion.
 * @t: tboard_t pointer of task board to destroy
 * 
 * This function joins task board executor threads. When threads are terminated, task executor
 * mutex and condition variables are destroyed and task board object is freed.
 * 
 * Context: Function will block thread it is called on until task board threads are terminated
 *          via tboard_kill().
 */

void tboard_exit();
/**
 * tboard_exit() - Terminates program
 * 
 * This function calls pthread_exit(). This should be run only after tboard_destroy() at the end
 * of program main().
 * 
 * Context: User is expected to run tboard_destroy() before tboard_exit().
 * Context: This function will terminate program.
 * 
 */

bool tboard_kill(tboard_t *t);
/**
 * tboard_kill() - Kill task board threads.
 * @t: tboard_t pointer of task board to kill.
 * 
 * Terminates task board executor threads via pthread_cancel(). This will unblock 
 * tboard_destroy() allowing program to terminate. exec_t variables passed to task
 * executor is freed.
 * 
 * Context: Executor threads stored in @t->primary and @t->secondary[] are canceled.
 * 
 * Return:
 * * true   - task board was killed sucessfully. 
 * * false  - task board was not killed, indicating @t is NULL or @t has not begun.
 */



int tboard_get_concurrent(tboard_t *t);
/**
 * tboard_get_concurrent() - Returns number of concurrently running tasks
 * @t:  tboard_t pointer of task board.
 * 
 * returns number of currently running tasks in taskboard, in all queues/executors.
 * this number will always be less than or equal to MAX_TASKS macro.
 * 
 * Context: locks mutex @t->tmutex to access @t->task_count
 * 
 * Return: @t->task_count
 */

void tboard_inc_concurrent(tboard_t *t);
/**
 * tboard_inc_concurrent() - Increments number of concurrently running tasks
 * @t:  tboard_t pointer of task board.
 * 
 * Increments the number of concurrently running tasks in task board. This function performs no
 * checks whether or not MAX_TASKS has been exceeded. This should only be run when adding a new task
 * to any executor ready queue, in order to keep track of the number of unique tasks in all queues.
 * 
 * Context: locks mutex @t->tmutex to access @t->task_count, and increments it
 */

void tboard_deinc_concurrent(tboard_t *t);
/**
 * tboard_deinc_concurrent() - Deincrements number of concurrently running tasks
 * @t:  tboard_t pointer of task board.
 * 
 * Deincrements the number of concurrently running tasks in task board. This function performs no
 * checks whether or not current value is zero. This should only be run when adding any task completes
 * in any executor to indicate that a unique task is being removed from the ready task queue pool.
 * 
 * Context: locks mutex @t->tmutex to access @t->task_count, and increments it
 */


int tboard_add_concurrent(tboard_t *t);
/**
 * tboard_add_concurrent() - Increments number of concurrently running tasks iff current
 *                           value is less than MAX_TASKS
 * @t:  tboard_t pointer of task board.
 * 
 * This function essentially combines tboard_get_concurrent() and tboard_inc_concurrent. It will
 * check the current value of @t->task_count, and increment it if possible. This function assumes
 * that the value of task_count is not less than zero. If DEBUG is defined, it will perform this check
 * but will proceed anyways, logging any invalid values. It will return the new number of concurrently
 * running tasks, 0 on error.
 * 
 * Context: locks mutex @t->tmutex to access @t->task_count
 * 
 * Return: 0    - On Error: Unable to increment, as incrementing would exceed MAX_TASKS 
 *         else - @t->task_count after incrementing
 */


////////////////////////////////////////////////
////////////// Task Functions //////////////////
////////////////////////////////////////////////

bool task_create(tboard_t *t, function_t fn, int type, void *args);
/**
 * task_create() - Creates task, adds to appropriate ready queue to be executed
 *                 by task executor.
 * @t:    tboard_t pointer of task board.
 * @fn:   Task function with signature `void fn(void *)` as function_t to be executed.
 * @type: Task type. Value is PRIMARY_EXEC or SECONDARY_EXEC.
 * @args: Task arguments made available to task function @fn.
 * 
 * Creates task to be run by task board and adds it to respective ready queue, dependent on
 * task type @type. Should a task have side effects, @type is expected to reflect this. Once added
 * to a ready queue, it will signal condition variable of relevant executor to indicate that a new task
 * has been added to the ready queue.
 * 
 * Task functions return on task completion. Data can be made available to task function by setting
 * @args argument to data pointer. Although tasks cannot return data, modifications to @args by task
 * function will persist after execution. It is the user's responsibility to handle task allocation of
 * task data.
 * 
 * Should a task issue I/O requests or be required to wait for an event, it is expected to call
 * task_yield() to be non-blocking. When task yields, it will be added back to the ready queue. It is
 * the user's prerogative to create tasks that are non-blocking and efficient. Task execution will occur
 * until task function yields or terminates. Poorly construction functions will prevent multi-tasking.
 * 
 * Tasks can be executed for a finite amount of time, or iterations can run indefinitely. For the latter,
 * tasks will typically be run in an infinite loop and are expected to yield after every iteration, otherwise
 * the single task will run the entire time, blocking other tasks from executing.
 * 
 * Tasks created via task_create() are local tasks. Remote procedure tasks (RPC) can be issued by MQTT
 * and are sent in the form of a message, handled by msg_processor().
 * 
 * Context: Process context. Takes and releases task executor mutex (@t->pmutex, @t->smutex[] for 
 *          pExecutor and sExecutor)
 * Context: Process Context. Signal task executor condition variable (@t->pcond, @t->scond[] for 
 *          pExecutor and sExecutor)
 * 
 * Return:
 * * true   - task was added to task board successfully.
 * * false  - task was not added to task board.
 */

bool task_add(tboard_t *t, task_t *task);
/**
 * task_add() - Adds task to task board.
 * @t:    tboard_t pointer to task board.
 * @task: task_t pointer to task
 * 
 * Adds task to task board. This function is called internally by task_create() and other functions
 * that create tasks. Local tasks should be added by task_create() call.
 * 
 * It is assumped that task_t pointers to a properly formatted task object.
 * 
 * Function determines which TExec ready queue task should be added to. It will lock the appropriate
 * TExec mutex and signal condition variable after adding to ready queue.
 * 
 * Context: Process context. Takes and releases task executor mutex (@t->pmutex, @t->smutex[] for 
 *          pExecutor and sExecutor)
 * Context: Process Context. Signal task executor condition variable (@t->pcond, @t->scond[] for 
 *          pExecutor and sExecutor)
 * 
 * Return:
 * * true   - task was added to task board successfully.
 * * false  - task was not added to task board.
 */

void task_yield();
/**
 * task_yield() - yields currently run task
 * 
 * This should only be called by task function. Otherwise functionality is undefined.
 * 
 * When it is called, context will be returned to task executor in an executor thread, and task
 * will be added to the back of the appropriate ready queue.
 * 
 */

void *task_get_args();
/**
 * task_get_args() - Gets @args passed to task_create on task creation
 * 
 * This function returns @args defined on task creation, which are arguments that are meant to be
 * made available to the running task. Using the minicoro library, we simply request user data for
 * the currently running coroutine via minicoro api call.
 * 
 * Return: Function arguments issued on task creation, as a void pointer.
 */

int task_store_data(void *data, size_t size);
/**
 * task_store_data() - Store data between yielding and resuming tasks
 * @data: Pointer to data object to store
 * @size: Size of data object to store
 * 
 * If storing data to retrieve later, it is necessary to call this function before task_yield(). In
 * order to prevent stack overflow, calling task_retrieve_data() must occur after task resumes before
 * next call of task_store_data().
 * 
 * Stored data is not expected to persist after task completion.
 * 
 * Return: Status, corresponding to mco_result enumeration. Will return 0 on success, non-zero on error
 */

int task_retrieve_data(void *data, size_t size);
/**
 * task_store_data() - Store data between yielding and resuming tasks
 * @data: Pointer to data object to store
 * @size: Size of data object to store
 * 
 * Retrieve data that was stored previously by task using task_store_data() call. In order to prevent
 * stack overflow, this should be called between task_store_data() calls.
 * 
 * Return: Status, corresponding to mco_result enumeration. Will return 0 on success, non-zero on error
 */


//////////////////////////////////////////////////
////////////// Processor Definitions /////////////
//////////////////////////////////////////////////

/**
 * msg_t - Message data type
 * @type: Message type
 * @subtype: Message subtype
 * @has_side_effects: Indicates if task has side effects.
 * @data: Data recieved from MQTT Adapter
 * @user_data: Data passed to task, determined by MQTT Adapter.
 * @ud_allocd: Boolean representing if @user_data points to allocated memory
 * 
 * msg_t objects are created by MQTT Adapter when message is recieved, and is used to pass message
 * information appropriated to task board.
 */
typedef struct {
    int type;
    int subtype;
    bool has_side_effects;
    void *data; // must be castable to task_t or bid_t
    void *user_data;
    bool ud_allocd; // whether user_data was alloc'd
} msg_t;

/**
 * bid_t - Bid data object.
 * 
 * bid_t objects are created by Redis adapter.
 * 
 * Current values are placeholders, as implementation specifications have not been issued.
 * 
 * TODO: Implement.
 */
typedef struct {
    int type;
    int EST;
    int LST;
    void *data;
} bid_t;

bool msg_processor(tboard_t *t, msg_t *msg); // when a message is received, it interprets message and adds to respective queue
/**
 * msg_processor() - Handles message issued remotely by MQTT
 * @t:   tboard_t pointer to task board.
 * @msg: message recieved to be processed.
 * 
 * This function should only be called by MQTT Adapter. MQTT Adapter is expected to handle memory
 * associated with @msg and to properly format @msg.
 * 
 * msg_processor() will add either add a task to task board, or will modify schedule
 * 
 * TODO: update when fully implemented
 */
bool data_processor(tboard_t *t, msg_t *msg); // when data is received, it interprets message and proceeds accordingly (missing requiremnts)
/**
 * data_processor() - Handles data issued remotely by Redis Adapter.
 * @t:   tboard_t pointer to task board.
 * @msg: message recieved to be processed.
 * 
 * TODO: Implement
 */
bool bid_processing(tboard_t *t, bid_t *bid); // missing requirements
/**
 * bid_processing() - Processes remote schedule changes issued by MQTT
 * @t:   tboard_t pointer to task board.
 * @bid: bid issued remotely that dictates schedule changes
 * 
 * TODO: Need requirements and implementation
 */




////////////////////////////////////////////////////////////////
/////////////////// Task history functionality /////////////////
////////////////////////////////////////////////////////////////

/**
 * history_t - tracks task execution history
 * @fn_name:     task function name (also hash table key, must be unique)
 * @mean_t:      average run time in CPU time units for complete executions
 * @mean_yield:  average number of yields for all complete executions
 * @yields:      total number of yields for all executions (incremented at each yield)
 * @executions:  number of exections
 * @completions: number of complete executions
 * 
 * TODO: complete design and implementation
 */
typedef struct history_t {
    char *fn_name;
    double mean_t;
    double mean_yield;
    double yields;
    int executions;
    int completions;
    UT_hash_handle hh;
} history_t;


void history_record_exec(tboard_t *t, task_t *task, history_t **hist);
/**
 * history_record_exec() - Record task execution in history hash table
 * @t:    tboard_t pointer to task board
 * @task: task_t pointer of task to record
 * @hist: history_t in hash table to return
 * 
 * This function will record execution information to the history_t hash table
 * of the task board @t. If there is currently no record in the hash table,
 * history_record_exec() will add it to the table, attaching entry to @hist. If
 * record does exist, @hist will reflect its location and its data will be modified.
 * 
 * @hist is expected to point to a null pointer.
 * 
 * Context: locks @t->hmutex in order to modify hash table
 */

void history_fetch_exec(tboard_t *t, function_t *func, history_t **hist);
/**
 * history_fetch_exec() - Fetch history hash table entry corresponding to function_t @func.
 * @t:    tboard_t pointer to task board
 * @func: function_t pointer of function to fetch
 * @hist: history_t in hash table to return
 * 
 * This function will fetch execution information from the history_t hash table
 * of the task board @t. If there is currently no record in the hash table,
 * @hist will point to NULL pointer. If record does exist, @hist will reflect it's 
 * location and its data will be modified.
 * 
 * @hist will be rewritten.
 * 
 * Context: locks @t->hmutex in order to modify hash table
 */

void history_destroy(tboard_t *t);
/**
 * history_destroy() - Destroys history hash table
 * @t:    tboard_t pointer to task board
 * 
 * Function should only be called in tboard_destroy(). This function will iterate through
 * hash table, freeing every entry. Should the user wish to serialize the hash table, they must
 * do so before this function is called in tboard_destroy()
 * 
 * 
 * Context: locks @t->hmutex in order to destroy hash table
 */

void history_save_to_disk(tboard_t *t, FILE *fptr);
/**
 * history_save_to_disk() - Saves task board history to disk
 * 
 * TODO: implement
 * 
 */

void history_load_from_disk(tboard_t *t, FILE *fptr);
/**
 * history_load_from_disk()
 * 
 * TODO: implement
 */

void history_print_records(tboard_t *t, FILE *fptr);
/**
 * history_print_records() - Prints execution history to @fptr
 * @t:    tboard_t pointer to task board
 * @fptr: file pointer to print records to.
 * 
 * @fptr is assumed opened, and is assumed to be closed. Default value should be stdout.
 * format will print as:
 * 
 * "task 'func_name' completed %d/%d times, yielding %ld times with mean execution time %ld"\
 * 
 * Context: locks @t->hmutex in order to access hash table
 */




////////////////////////////////////////////////////////////////
/////////////////////// Logging functionality //////////////////
////////////////////////////////////////////////////////////////

int tboard_log(char *format, ...);
/**
 * tboard_log() - Log string to stdout.
 * @format: format of log string.
 * @...:    list of arguments corresponding to provided format.
 * 
 * identical syntax to functions we love like printf
 */
int tboard_err(char *format, ...);
/**
 * tboard_log() - Report string to stderr.
 * @format: format of error string.
 * @...:    list of arguments corresponding to provided format.
 * 
 * identical syntax to functions we love like printf
 */

#endif