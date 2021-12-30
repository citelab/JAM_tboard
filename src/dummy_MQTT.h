#include "tboard.h"
#include <minicoro.h>
#include "queue/queue.h"
#include <pthread.h>
#include <time.h>

struct queue MQTT_Message_Pool; // contains incoming messages to be parsed in string format
struct queue MQTT_Message_Queue; // contains msg_t after recieving to be added to task board

pthread_mutex_t MQTT_Mutex;
pthread_cond_t MQTT_Cond;
pthread_mutex_t MQTT_Msg_Mutex;
pthread_cond_t MQTT_Msg_Cond;
pthread_t MQTT_iPthread, MQTT_oPthread;
pthread_mutex_t MQTT_Count_Mutex;

int MQTT_n;

struct arithmetic_s {
    double a;
    double b;
    char operator;
};

struct rarithmetic_s {
    double a;
    double b;
    char operator;
    double ans;
};

struct MQTT_data {
    int imsg_sent;
    int imsg_recv;
    int omsg_sent;
    int omsg_recv;
};

struct timespec MQTT_sleep_ts;

void MQTT_init(tboard_t *t);
/**
 * MQTT_init() - Initialize dummy MQTT adapter for task board @t
 * @t: tboard_t pointer to task board
 * 
 * Initializes dummy MQTT, and creates 2 threads; One for sending messages, one for receiving.
 */

void MQTT_kill(struct MQTT_data *data);
/**
 * MQTT_kill() - Kills MQTT
 * @data: pointer to store execution information
 * 
 * Function stores execution information and cancels incoming/outgoing MQTT threads
 */

void MQTT_destroy();
/**
 * MQTT_destroy() - Destroys MQTT
 * 
 * Joins MQTT threads, destroys MQTT mutexs and condition variables, empties
 * MQTT message pool and queues of unfulfilled requests and frees all allocated
 * data
 */


void MQTT_send(char *message);
/**
 * MQTT_send() - Simulates controller sending message to worker
 * @message: message from controller
 * 
 * Adds message to MQTT message pool, simulating controller sending message
 */

void MQTT_recv(tboard_t *t);
/**
 * MQTT_recv() - Receive and parse message from controller
 * @t: Task board to send message to
 * 
 * Function parses message from controller if present in MQTT message pool. it then
 * generates a msg_t object to send to task board, and inserts it into message queue.
 */

void MQTT_issue_remote_task(tboard_t *t, remote_task_t *rtask);
/**
 * MQTT_issue_remote_task() - Send worker-to-controller task to controller response 
 *                            and simulates waiting/parsing response from controller
 * @t:     task board that receives response from controller
 * @rtask: remote task to send to controller
 * 
 * Given a remote task, it will simulate issuing the remote task to the controller and
 * generate a response. After response is received it will place update task status and
 * @rtask back in the appropriate tboard message queue via remote_task_place() function
 * call.
 */


void *MQTT_othread(void *args);
/**
 * MQTT_othread() - Thread function that handles worker-to-controller communication
 * @args: pointer to task board object
 * 
 * Function will wait for worker-to-controller messages in @tboard->msg_sent queue,
 * sleeping on @tboard->msg_cond if there are no remote tasks present
 */

void *MQTT_ithread(void *args);
/**
 * MQTT_ithread() - Thread function that handles controller-to-worker communication
 * @args: pointer to task board object
 * 
 * At each iteration function will check for pending messages in MQTT message queue. If
 * a message is found, it will act accordingly by sending it to task board. Otherwise, it
 * will check if any new messages are in the message pool, at which point it will parse it
 * and place it in the MQTT message queue. If no new messages are found, it will sleep on
 * condition variable MQTT_Cond
 */


/**
 * The following are local tasks that can be issued to worker by controller
 */
void MQTT_Spawned_Task(context_t ctx);
void MQTT_Print_Message(context_t ctx);
void MQTT_Do_Math(context_t ctx);
void MQTT_Spawn_Task(context_t ctx);

void MQTT_Increment(int *value);
/**
 * MQTT_Increment() - utility function to safely increment execution variable
 * @value: pointer to value to increment
 * 
 * Locks MQTT_Count_Mutex and then increments value
 */