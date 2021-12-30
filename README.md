# Task Board
Multi-threaded task library with user-level schedule supporting both local and remote task execution.

## Features
* Multiple task boards supported in a single application.
* Flexibility between local task board and remote tasks.
* Multiple types of tasks supported.
* Designed for multithreaded application.
* Fast, robust and highly efficient while being customizable.
* Readable sources and documentation.
* Support for running in Valgrind.
* Testing library is included.
* Remote MQTT support via flexible user-defined MQTT adapter.
* Virtually any remote protocol is supported via flexibility in remote task adapter implementation.
* Built in logging functions

## Introduction
### Task Board (tboard)
To create a task board and start it, include `#include "tboard.h"` and call the following code at the start of the program:
```c
int secondary_executors = 10; // number of sExec threads to run
tboard_t *tboard = tboard_create(secondary_executors); // create task board
tboard_start(tboard); // start pExec and sExec threads
```
To join `pExec` and `sExec`, call `tboard_destroy(tboard)` at the end of the function. This will cause the calling thread to wait until `pExec` and `sExec` terminate. Once all task boards are destroyed, call `tboard_exit()` to exit application.

By default, the task board will run indefinitely, with executor threads completing tasks until no tasks are left. Once that occurs, the executor threads will sleep on a condition variable until new tasks are inserted into the task board.

To manually kill the task board, in a separate thread call `tboard_kill(tboard)`. In order to capture task board data before task board is destroyed after executor threads terminate, the following structure must be followed:
```c
pthread_mutex_lock(&(tboard->tmutex)); // prevent immediate destruction
tboard_kill(tboard); // kill task board
/*
 * capture task board data
 */
pthread_mutex_unlock(&(tboard->tmutex)); // unlock task board mutex
// task board will now be destroyed
```
Task execution history is saved by default in `history.c`. In order to print task execution history to `stdout`, simply call `history_print_records(tboard, stdout)`.
### Components of `tboard`

The task board structure contains the following elements:
* Primary executor thread (`pExec`)
* Secondary executor threads (`sExec`)
* Primary and Secondary ready queues that hold tasks waiting to execute
* Incoming and outgoing message queues for remote task execution
* Task execution history hash table (type:`history_t`)
* Concurrent task count, readable at any time via `int tboard_get_concurrent(tboard);` call.
* Various mutex locks and condition variables to ensure consistent data across threads and predictable behavior

Task board structure is type `tboard_t`. Definitions can be found in `tboard.h`.

### Task Executors
In the task board, the task executors (`TExec`) runs indefinitely until task board terminates. It runs the Task Sequencer function `TSeq` to interface worker and controller communication over MQTT and schedule task execution. If there are no tasks in the executor's task ready queue, executor will go to sleep on a condition variable. Once a task is pulled out of the task ready queue, `TExec` will switch to that task, returning only once task has yielded or terminates. If task yields, it will be returned back into the task ready queue to be executed later. If task terminates, execution statistics will be recorded in history hash table and it's stack will be destroyed.

Task executors can be split into two categories:

- Primary task executor (`pExec`): Primary task executor is the main thread of the task board. It will run tasks that are in the primary task ready queue. Should there be no tasks present, it will attempt to run tasks from secondary task ready queues. To prevent the possibility of a deadlock, if no tasks are present, it will initiate a timed wait on condition variable so that `TSeq` can run.
- Secondary task executor (`sExec`): Secondary task executor runs pulls tasks from it's secondary task ready queue exclusively. If no tasks are present in ready queue, it will sleep on it's own condition variable, awakening only when a task is placed in it's ready queue.

All essential tasks that need to be run on `pExec` will be contained within the primary task ready queue.

### Tasks
#### Local tasks
There are several different types of tasks. The first kind are local tasks, which can terminate or run indefinitely, yielding at every iteration. Local tasks can be classified into three different types:
1. `PRIORITY_EXEC`: Priority tasks will be placed at the head of the primary ready queue, for execution to happen in a timely manner. Priority tasks will block other tasks from running on `pExec` until priority task has terminated. Priority tasks most closely follow `LIFO` task scheduling. Priority tasks do not block secondary tasks from running in `sExec`.
2. `PRIMARY_EXEC`: Primary tasks will be placed at the tail of the primary ready queue. These tasks will run exclusively on the primary executor thread.
3. `SECONDARY_EXEC`: Secondary tasks will be placed at the tail of some secondary ready queue, selected arbitrarily. If task board is made with `secondary_queues = 0`, they will be placed in the primary ready queue. These tasks are defined as tasks without any major dependencies or side effects. Secondary tasks can be run in either the primary execution thread or a secondary execution thread.

Local tasks can be created in any thread or any task. Local tasks must have the function signature `void task_func(context_t ctx)`. The following example shows how to create a local primary task:
```c
void task_func(context_t ctx); // task function to run by executor
...
bool res = task_create(tboard, TBOARD_FUNC(task_func), PRIMARY_EXEC, NULL, 0);
if (res)
	printf("Task created successfully.\n");
```
To specify arguments to a local task, one must simply pass the pointer of the argument to `task_create()`. If the user wishes for the arguments to be free'd from the memory on task termination, they must specify the size of the allocated data passed as an argument. Task arguments can then be retrieved by the `task_get_args()` function call.
```c
void task_func(context_t ctx) {
	type_t *args = (type_t *)task_get_args();
	...
}
...
type_t *args = calloc(1, sizeof(type_t));
// modify argument
bool res = task_create(tboard, TBOARD_FUNC(task_func), PRIMARY_EXEC, args, sizeof(type_t));
if (res)
	printf("Task created successfully.\n");
```
If size is not specified, then it is the users responsibility to handle garbage collection.

#### Blocking tasks
Blocking tasks are local tasks that are created within another parent task that must terminate before parent task will be allowed to resume execution. Within a task, blocking tasks can be created in the following way:
```c
void blocking_task(context_t ctx);
void parent_task(context_t ctx) {
	type_t *args;
	...
	bool res = blocking_task_create(tboard, TBOARD_FUNC(blocking_task), SECONDARY_EXEC, args, 0);
	if (res) {
		// use args
	}
	...
}
```
Should the user wish for argument modifications passed to a blocking child task to persist after blocking task termination, they must specify an argument size of zero (if persisting argument is manually allocated, the user is responsible for garbage collection). More detailed examples can be found provided tests.

#### Remote tasks
Remote tasks are tasks issued by local tasks that are to be sent to controller via the MQTT adapter. There are two kinds of remote tasks: Blocking tasks and non-blocking tasks. Blocking tasks will yield issuing task after sending, preventing issuing task from continuing until a response from controller is received by the MQTT adapter. Non-blocking tasks will send the message via MQTT adapter and continue execution. Creating an remote task via calls to `remote_task_create()`. By default, remote tasks are issued as a message with maximum length set in `MAX_MSG_LENGTH` macro, with return values being saved to `void *response`. Blocking is specified by setting `bool blocking` equal to true. Freedom is given to the user in terms of response data type, as it ultimately comes down to the implementation of the MQTT adapter. An example of sending remote tasks from worker to controller can be found in `tests/test6_milestone2.c`.

### MQTT Adapter
Provided in this package is an example of an MQTT adapter, called `dummy_MQTT.c`. Freedom with the actual MQTT Adapter is given to the user, as it is an independent entity from the task board, but the following approaches should be followed:

#### Controller to Worker

For controller to worker communication, the MQTT adapter must receive and parse messages. After parsing message, MQTT should create a `task_t` object corresponding to the local task that is to be run, including any arguments specified in the message. It will then generate a `msg_t` object to send to the task board via `processor.c:msg_processor()`. `msg_t` has `int type`, `int subtype`, `bool has_side_effects`, `void *data`, `void *user_data`and `size_t ud_allocd`as fields. For local task execution messages, `data` field should correspond to an allocated `task_t` object. An example of `msg_t` requirements can be found in `dummy_MQTT.c:MQTT_recv()` function.  If `MQTT_ADD_BACK_TO_QUEUE_ON_FAILURE` is specified, MQTT will return message to message queue to be added to task board later. This is enabled by default.

#### Worker to controller

For worker to controller communication, the MQTT adapter must pull messages from the `tboard->msg_send` message queue, and return responses to the `tboard->msg_recv` message queue. The user is responsible for locking mutex `tboard->msg_mutex` before accessing these queues. Objects in these queues have type `remote_task_t`, with fields `int status`, `char message[]`, `void *data`, `size_t data_size `, `task_t *calling_task`, and `bool blocking`. Responses should be written to `data` and `status` should be updated before returning a message to the task board. All requests must be returned to the task board in order for proper garbage collection to occur, even if the request is non-blocking. Example implementation can be found in `dummy_MQTT.c:MQTT_issue_remote_task()`.

In my implementation of a dummy MQTT, I have two threads running. One thread polls task board for outgoing message requests, sleeping on condition variable `tboard->msg_cond`. This thread is responsible for pulling messages out of the outgoing message queue, sending them to the controller, and awaiting a response. The other thread waits to receive requests from the controller as a string, at which point it processes the request and takes appropriate action. Tests 5-8 contain examples of MQTT implementations.

## Milestones and tests
Running tests can be specified in `main.h`. Milestone tests are located in `tests`, and they show usage for specific achievements associated with each milestone. Functionality tests are found in `legacy_tests` and they were designed to test different edge cases of the task board. I have decided to leave the legacy tests intact with brief explanations within the test files for the next person who continues this project where I left off.

A testing library has been including in with the task board, with prototypes and definitions in `tests/tests.h` and `tests/tests.c` respectively. A template for creating test can be found in `tests/test_template.c`.

All tests can find more detailed explanations in the source code files.

### Running tests

To run a milestone test, set `RUN_TEST = 1` in `main.h` with `TEST_NUM` corresponding to desired milestone test to run. Milestone tests are located in `/src/tests/` directory

To run a legacy test, set `RUN_LTEST = 1` in `main.h` with `TEST_LNUM` corresponding to the desired legacy test to run. Legacy tests are located in `/src/legacy_tests/` directory

Afterwards, running a task can be achieved by calling in `make clean && make && ./output/main` in the project root directory.

### Milestone 1
Milestone 1 is to create a task board object and run a variety of local tasks and we let them run. There are a variety of possible local tests, some run indefinitely yielding at each iteration, and others run and terminate after spawning zero or more local tasks. Although this milestone does not indicate task type, they will adopt the same type which can be specified by `TASK_TYPE`.

- `test1` creates 4 types of local tasks: indefinite tasks, completing tasks, spawning tasks, and blocking tasks. The task board will end at some amount of time after starting, selected randomly between `[0, MAX_RUN_TIME]`.
- `test2` creates spawning tasks and sub tasks rapidly to measure task execution rate. If `BLOCKING_TASKS` is specified, then sub tasks will be initiated as blocking tasks. After `NUM_TASKS` number of sub tasks have completed, task board will terminate and relevant execution information will be printed.

### Milestone 1b
Milestone 1b was to create local tasks with multiple task types, determining whether they are executed by `pExec` or `sExec`. 

- `test3` creates 3 types of tasks: primary tasks, and secondary tasks, as well as priority tasks if `ISSUE_PRIORITY_TASKS` is specified. Max time between priority tasks can be set in `MAX_TIME_BETWEEN_PRIORITY`. Task board will terminate once `NUM_TASKS` amount of secondary tasks are issued by primary task. Priority tasks will be issued at random.
- `test4` creates primary tasks and secondary tasks. Primary tasks will issue a single secondary task. Both tasks will terminate rapidly, causing task board to sleep due to no work. Tasks will be issued at `<1s` intervals. The task board will end at some amount of time after starting, selected randomly between `[0, MAX_RUN_TIME]`.

### Milestone 2

Milestone 2 was to create remote tasks that connect the worker and controller. Worker-to-controller tasks, can be both blocking and non-blocking. Controller-to-worker tasks can have varying degree of complications, ultimately left to the user to implement.

- `test5` simulates controller-to-worker tasks exclusively. It does this by creating an independent thread that generates MQTT messages, sending them to MQTT via `MQTT_send()` function call in the form of a string. If `RAPID_GENERATION` is set to 1, then generator thread will rapidly issue commands to MQTT for up to 2 seconds before terminating. Otherwise, test will run for a maximum of `MAX_RUN_TIME` before terminating.
- `test6` tests worker-to-controller exclusively, simulating response from controller via `dummy_MQTT`. The two types of remote tasks I have implemented in MQTT is the blocking arithmetic task and non-blocking printing task. For the arithmetic task, task board will issue a request for the controller to perform some type of arithmetic, blocking the issuing task from continuing until the controller has responded with the calculation. The issuing task will then print the result to `stdout`. For the printing task, issuing task will continue execution, printing the message from the controller via `dummy_MQTT` to `stdout`.
- `test7` tests both controller-to-worker tasks and worker-to-controller tasks by combining both `test5` and `test6`

### All Milestones

`test8` combines all of the aforementioned tests into a single task board. It will create worker-to-controller tasks, controller-to-worker tasks, priority tasks, primary tasks, secondary tasks, and blocking tasks. If `RAPID_GENERATION` is specified, it will terminate after up to `MAX_RUN_TIME` seconds. Otherwise, it will generate `NUM_TASKS` remote and local tasks, terminating once all tasks complete.

## Library customization
The following can be defined to change behavior
- `MAX_TASKS` will change the maximum number of concurrent tasks that the task board can run. Default is 65536. After the maximum number of concurrent tasks have been reached, no non-blocking local tasks can be created until at least 1 task terminates. The only way the maximum number of concurrent tasks can be exceeded is by MQTT adapter placing blocking worker-to-controller back in a ready queue after response is received.
- `MAX_SECONDARIES` defines the maximum number of secondary executor threads the task board will support. The default is 10. It is good practice to set this number below the maximum number of CPU threads are supported by the hardware running the task board.
- `STACK_SIZE` defines the stack size of task board tasks. Default is 57344 bytes. Task stack size cannot be change after task has been initalized, so `STACK_SIZE` must be large enough for all local task board tasks, otherwise stack overflow will occur leading to unpredictable results. Since task space is heap allocated, `STACK_SIZE * MAX_TASKS` should not exceed the maximum amount of heap storage defined in `ulimits` of the running environment.
- `REINSERT_PRIORITY_AT_HEAD`will dictate whether a yielding priority task will be inserted at the head or tail of the primary task ready queue.

## Compiling

Compile using `make` with provided makefile. To create application that uses task board, simply include `tboard.h` and link all task board objects in `/src/` generated by `make`.

## Dependencies

- Task board depends on POSIX Threads, therefore task board has `libpthread` as a dependency. Compiling therefore requires `-pthread` compiler flag. Undefined behavior may occur on non-POSIX compliant OS's
- Task board tests depend on math library. Compiling tests therefore requires `-lm` compiler flag.

## API Cheat Sheet
#### Task Board Functions
```c
typedef struct tboard_t {
	pthread_t primary, secondary[]; // executor threads
	pthread_mutex_t pmutex, smutex[]; // executor mutexes
	pthread_cond_t pcond, scond[]; // executor condition vars
	struct queue pqueue, squeue[]; // executor ready queues
	...
	struct queue msg_sent, msg_recv; // remote task wait queues
	pthread_mutex_t msg_mutex; // remote task mutex
	pthread_cond_t msg_cond; // remote task condition var
	...
	int status; // taskboard status
	int task_count; // number of currently running tasks
}
tboard_t *tboard_create(int sqs); /* create task board with #sqs secondary queues */
void tboard_start(tboard_t *t); /* start task board t */
void tboard_destroy(tboard_t *t); /* join executors, destroy task board t */
void tboard_kill(tboard_t *t); /* kill task board executors */
int tboard_get_concurrent(tboard_t *t); /* query current number of concurrently running tasks */

int tboard_log(char *format, ...); /* log information to same file descriptor across task board */
int tboard_err(char *format, ...); /* report error to same file descriptor across task board */
```
In order to retrieve task board information, you must lock `tboard->tmutex` before initiating shutdown like so
```c
pthread_mutex_lock(&(tboard->tmutex));
tboard_kill(tboard);
history_print_records(tboard, stdout);
pthread_mutex_unlock(&(tboard->tmutex));
```
#### Task Functions
```c
/* task functions must have signature `void func(context_t ctx);` */
typedef void (*tb_task_f)(context_t);
/*** local tasks ***/
typedef  struct  task_t {
	int  id, status, type, cpu_time, yields; /* internal information */
	function_t  fn; /* task function pointer and name */
	context_t  ctx; /* coroutine context */
	context_desc  desc; /* coroutine description, including stack and arguments */
	size_t  data_size; /* size of arguments */
	struct  history_t *hist; /* entry in task execution history hash table */
	struct  task_t *parent; /* link to parent task */
} task_t;
/* Note: obtain function_t fn from TBOARD_FUNC(tb_task_f func) function call */

bool task_create(tboard_t *t, function_t fn, int type, void *args, size_t sizeof_args); /* create local task */
bool blocking_task_create(tboard_t *t, function_t fn, int type, void *args, size_t sizeof_args);  /* create blocking local task */
void task_yield(); /* yield local task */
void *task_get_args(); /* returns args passed in task_create() */

/** remote tasks **/
typedef  struct remote_task_t {
	int  status; /* remote task status */
	char  message[]; /* remote task command */
	void *data; /* data object passed to MQTT to issue response */
	size_t  data_size; /* size of data, non-zero if heap allocated */
	task_t *calling_task; /* link to parent task */
	bool  blocking; /* whether or not remote task is blocking is asynchronous */
} remote_task_t;

/* create remote task */
bool remote_task_create(tboard_t *t, char *message, void *args, size_t sizeof_args, bool blocking); 
```

#### Dummy MQTT
```c
struct MQTT_data {
	int imsg_sent; /* controller-to-worker messages sent by controller */
	int imsg_recv; /* controller-to-worker messages received by worker */
	int omsg_sent; /* worker-to-controller messages sent by worker */
	int omsg_recv; /* worker-to-controller messages responded to by controller */
}
void MQTT_init(tboard_t *t); /* initialize MQTT adapter */
void MQTT_kill(struct MQTT_data *data); /* kill MQTT, record statistics to data */
void MQTT_destroy(); /* destroy MQTT */
void MQTT_send(char *message); /* send controller message to worker MQTT */
void MQTT_recv(tboard_t *t); /* worker MQTT receives controller message */
void MQTT_issue_remote_task(tboard_t *t, remote_task_t *rtask); /* worker MQTT send controller message */
void *MQTT_othread(void *args); /* thread that handles worker-to-controller communications */
void *MQTT_ithread(void *args); /* thread that handles controller-to-worker communications */
```