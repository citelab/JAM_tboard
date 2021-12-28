#include "tests.h"
#ifdef TEST_7

#include "../tboard.h"

#include "../dummy_MQTT.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

#define SECONDARY_EXECUTORS 2

#define CONTINUOUSLY_ISSUE 0

#define NUM_TASKS 100

tboard_t *tboard = NULL;
pthread_t message_generator;
pthread_t tboard_killer;
int imessages_sent = 0;
int omessages_sent = 0;
int omessages_recv = 0;
struct MQTT_data mqtt_data = {0};

int completion_count = 0;
int task_count = 0;
pthread_mutex_t count_mutex;

bool task_gen_complete = false;
int max_task_reached = 0;

void increment_completion_count();
void increment_msg_count(int *msg);
int read_completion_count();

double rand_double(double min, double max);

void fsleep(float max_second);

void generate_MQTT_message(void *args){
    char *commands[] = {"print", "math", "spawn"};
    char operators[] = "+-/*";
    while(true)
    {
        int cmd = rand() % 3;
        switch(cmd){
            case 0:
                MQTT_send("print 'hello world!'\n");
                break;
            case 1:
                ;
                int opn = rand() % 4;
                char op = operators[opn];
                char message[50] = {0};
                sprintf(message, "math %d %c %d",rand()%500, op, rand()%500);
                MQTT_send(message);
                break;
            case 2:
                MQTT_send("spawn");
                break;
        }
        increment_msg_count(&imessages_sent);
        fsleep(0.5);
    }
}

void remote_task(void *args){
    increment_msg_count(&omessages_sent);
    if (rand() % 2 == 0) {
        remote_task_t rtask = {0};
        strcpy(rtask.message, "print");
        char *pmessage = calloc(20, sizeof(char));
        strcpy(pmessage, "Hello World!");
        bool res = remote_task_create(tboard, "print", pmessage, strlen(pmessage), TASK_ID_NONBLOCKING);
        if (!res) {
            tboard_err("Could not create remote task 'print Hello World!'\n");
        } else {
            increment_completion_count();
        }
    } else {
        struct rarithmetic_s mathing = {0};
        mathing.a = rand_double(1.0, 10.0);
        mathing.b = rand_double(1.0, 10.0);
        char ops[] = "+-/*";
        mathing.operator = ops[rand() % 4];

        bool res = remote_task_create(tboard, "math", &mathing, 0, TASK_ID_BLOCKING);
        if (res) {
            printf("Remote task computed %f %c %f = %f\n", mathing.a, mathing.operator, mathing.b, mathing.ans);
            increment_completion_count();
        } else {
             tboard_err("Could not create remote task 'math %f %c %f'\n", mathing.a, mathing.operator, mathing.b);
        }
    }
    increment_msg_count(&omessages_recv);
}


void remote_task_gen(void *args){
    for (int i=0; i<NUM_TASKS; i++) {
        int unable_to_create_task_count = 0; // bad name i know
        int *n = calloc(1, sizeof(int));
        *n = i;
        while(false == task_create(tboard, TBOARD_FUNC(remote_task), PRIMARY_EXEC, n, sizeof(int))) {
            if (unable_to_create_task_count > 30) {
                tboard_log("remote_task_gen: Was unable to create the same task after 30 attempts. Ending at %d tasks created.\n",i);
                task_gen_complete = true;
                return;
            }
            max_task_reached++;
            usleep(300);
            task_yield();
            unable_to_create_task_count++;
        }
        task_count++;
        task_yield();
    }
    task_gen_complete = true;
    tboard_log("remote_task_gen: Finished creating %d remote tasks.\n",task_count);
}
void kill_tboard(void *args){
    if(CONTINUOUSLY_ISSUE == 1) {
        fsleep(100);
        printf("Random time hit, killing task board.\n");
    } else {
        while (true) {
            if (task_gen_complete && read_completion_count() == NUM_TASKS) {
                break;
            } else {
                usleep(300);
            }
        }
    }
    MQTT_kill(&mqtt_data);
    pthread_cancel(message_generator);
    pthread_mutex_lock(&(tboard->tmutex));
    tboard_kill(tboard);
    history_print_records(tboard, stdout);
    pthread_mutex_unlock(&(tboard->tmutex));
}

int main(){
    
    tboard = tboard_create(SECONDARY_EXEC);
    tboard_start(tboard);
    
    MQTT_init(tboard);

    pthread_create(&message_generator, NULL, generate_MQTT_message, NULL);
    task_create(tboard, TBOARD_FUNC(remote_task_gen), PRIMARY_EXEC, NULL, 0);
    pthread_create(&tboard_killer, NULL, kill_tboard, NULL);

    printf("Taskboard created, all threads initialized.\n");
    
    pthread_join(message_generator, NULL);
    printf("joined msg gen.\n");
    tboard_destroy(tboard);
    printf("destroyed tboard.\n");
    pthread_join(tboard_killer, NULL);
    printf("joined tboard killer.\n");
    
    printf("MQTT Statistics:\n");
    printf("\tSent %d/%d remote tasks to MQTT, %d were received, %d were responded to.\n",omessages_recv, omessages_sent, mqtt_data.omsg_recv, mqtt_data.omsg_sent);
    printf("\tIssued %d local tasks to MQTT, %d were received, %d were completed.\n", imessages_sent, mqtt_data.imsg_recv, mqtt_data.imsg_sent);
    MQTT_destroy();
    tboard_exit();

}

double rand_double(double min, double max)
{
    double scale = (double)(rand()) / (double)RAND_MAX;
    return min + scale*max;
}

void increment_completion_count()
{
    pthread_mutex_lock(&count_mutex);
	completion_count++;
	pthread_mutex_unlock(&count_mutex);
}

int read_completion_count()
{
	pthread_mutex_lock(&count_mutex);
	int ret = completion_count;
	pthread_mutex_unlock(&count_mutex);
    return ret;
}
void increment_msg_count(int *msg)
{
    pthread_mutex_lock(&count_mutex);
	*msg = *msg + 1;
	pthread_mutex_unlock(&count_mutex);
}

void fsleep(float max_second)
{
    float seconds = (float)rand() / (float)(RAND_MAX/max_second);
    int s = (int)seconds;
    long ns = (long)(1000000000 * (seconds - s));
    struct timespec ts = {
        .tv_sec = s,
        .tv_nsec = ns,
    };
    nanosleep(&ts, NULL);
}

#endif
// */