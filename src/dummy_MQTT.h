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
 * @msgs_sent
 * 
 */
void MQTT_destroy();


void MQTT_send(char *message);
void MQTT_recv(tboard_t *t);

void MQTT_issue_remote_task(tboard_t *t, remote_task_t *rtask);


void MQTT_othread(void *args);
void MQTT_ithread(void *args);



void MQTT_Spawned_Task(void *args);

void MQTT_Print_Message(void *args);
void MQTT_Do_Math(void *args);
void MQTT_Spawn_Task(void *args);

void MQTT_Increment(int *value);