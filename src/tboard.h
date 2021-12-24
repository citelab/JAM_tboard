/* The main header file for our project */

#ifndef __TBOARD_H__
#define __TBOARD_H__


#include <sys/queue.h>
#include "queue/queue.h"


#include <minicoro.h>

#include <stdbool.h>
#include <pthread.h>


// TODO: figure out proper way to document macros, and determine all required macros

#define SMALL_TASK_TIME 300
// EST = earliest start time, LST = latest start time
#define MAX_TASKS 256
#define MAX_SECONDARIES 10

#define PRIORITY_EXEC -1
#define PRIMARY_EXEC 0
#define SECONDARY_EXEC 1

#define STACK_SIZE 58196

#define SPIN_BLOCK_ITERATIONS 50

#define TASK_EXEC 0 // for msg_processor
#define TASK_SCHEDULE 1 // for msg_processor

#define SIGNAL_PRIMARY_ON_NEW_SECONDARY_TASK 1


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
 * task_t - Data type containing task information
 * @id:     Task ID representing location in memory for preallocated task_list.
 * @status: Status of current task
 *          @status == 0: task was issued
 *          @status == 1: task is running
 *          @status == 2: task has terminated
 * @type:   Task type.
 *          @type == PRIORITY_EXEC:  Highest priority primary task
 *          @type == PRIMARY_EXEC:   Primary task
 *          @type == SECONDARY_EXEC: Secondary task
 * @fn:     Task function to be run by task executor.
 * @ctx:    Task function context.
 * @desc:   Coroutine description structure.
 * 
 * Structure contains all necessary information relating to a task.
 * TODO: add more description
 */
typedef struct {
    int id;
    int status;
    int type;
    tb_task_f fn;
    context_t ctx;
    context_desc desc;
} task_t;


struct exec_t;
/**
 * tboard_t - Task Board object.
 * @primary:    Thread of primary task executor (pExecutor)
 * @secondary:  Threads of secondary task executors (sExecutor)
 * @pcond:      Condition variable of pExecutor
 * @scond:      Condition variables of sExecutor
 * @pmutex:     Mutex of pExecutor
 * @smutex:     Mutexs of sExecutor
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

    pthread_mutex_t tmutex;
    pthread_cond_t tcond;

    struct queue pqueue;
    struct queue pwait;
    struct queue squeue[MAX_SECONDARIES];
    struct queue swait[MAX_SECONDARIES];

    int sqs;

    task_t task_list[MAX_TASKS];

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



////////////////////////////////////////////////
////////////// Task Functions //////////////////
////////////////////////////////////////////////

bool task_create(tboard_t *t, tb_task_f fn, int type, void *args);
/**
 * task_create() - Creates task, adds to appropriate ready queue to be executed
 *                 by task executor.
 * @t:    tboard_t pointer of task board.
 * @fn:   Task function with signature `void fn(void *)` to be executed.
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