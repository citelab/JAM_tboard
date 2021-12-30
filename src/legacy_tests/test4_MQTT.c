#include "legacy_tests.h"
#ifdef LTEST_4

#include "../tboard.h"

#include "../dummy_MQTT.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

struct timespec ts = {
	.tv_sec = 0,
	.tv_nsec = 300000
};


tboard_t *tboard = NULL;
pthread_t message_generator;
pthread_t tboard_killer;
int messages_sent = 0;
struct MQTT_data mqtt_data;

#define SECONDARY_EXECUTORS 2
#define RAPID_GENERATION 0

void *generate_MQTT_message(void *args){
    (void)args;
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
                char message[50] = {0}; // char *message = calloc(50, sizeof(char)); //
                sprintf(message, "math %d %c %d",rand()%500, op, rand()%500);
                MQTT_send(message);
                break;
            case 2:
                MQTT_send("spawn");
                break;
        }
        messages_sent++;
        if(RAPID_GENERATION == 1) nanosleep(&ts, NULL);
        else sleep(rand() % 4);
    }
    return NULL;
}

void *kill_tboard(void *args){
    (void)args;
    sleep(RAPID_GENERATION ? 2 : 30);
    pthread_mutex_lock(&(tboard->tmutex));
    pthread_cancel(message_generator);
    MQTT_kill(&mqtt_data);
    tboard_kill(tboard);
    history_print_records(tboard, stdout);
    pthread_mutex_unlock(&(tboard->tmutex));
    printf("tboard killed.\n");
    return NULL;
}

int main(){
    
    tboard = tboard_create(SECONDARY_EXEC);
    tboard_start(tboard);
    
    MQTT_init(tboard);

    pthread_create(&message_generator, NULL, generate_MQTT_message, NULL);
    pthread_create(&tboard_killer, NULL, kill_tboard, NULL);

    printf("Taskboard created, all threads initialized.\n");
    
    pthread_join(message_generator, NULL);
    printf("joined msg gen.\n");
    tboard_destroy(tboard);
    printf("destroyed tboard.\n");
    pthread_join(tboard_killer, NULL);
    printf("joined tboard killer.\n");
    printf("Sent %d/%d messages to MQTT, processed %d and sent to task board.\n",messages_sent,mqtt_data.imsg_sent,mqtt_data.imsg_recv);
    MQTT_destroy();
    tboard_exit();

}

#endif
// */