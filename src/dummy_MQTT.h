#include "tboard.h"
#include <minicoro.h>
#include "queue/queue.h"
#include <pthread.h>

struct queue MQTT_Message_Pool; // contains incoming messages to be parsed in string format
struct queue MQTT_Message_Queue; // contains msg_t after recieving to be added to task board
pthread_mutex_t MQTT_Mutex;
pthread_cond_t MQTT_Cond;
pthread_mutex_t MQTT_Msg_Mutex;
pthread_cond_t MQTT_Msg_Cond;
pthread_t MQTT_Pthread;
int MQTT_n;

struct arithmetic_s {
    double a;
    double b;
    char operator;
};




void MQTT_Spawned_Task(void *args);


void MQTT_Print_Message(void *args);

void MQTT_Spawn_Task(void *args);

void MQTT_init(tboard_t *t);

void MQTT_destroy();

void MQTT_kill();

void MQTT_send(char *message);

void MQTT_thread(void *args);