#include "tests.h"
#ifdef TEST_4

#include "../tboard.h"

#include "../dummy_MQTT.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>


tboard_t *tboard = NULL;
pthread_t message_generator;
pthread_t tboard_killer;
int messages_sent = 0;

#define SECONDARY_EXECUTORS 2

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
        messages_sent++;
        sleep(rand() % 4);
    }
}

void kill_tboard(void *args){
    sleep(100);
    pthread_cancel(message_generator);
    tboard_kill(tboard);
    printf("tboard killed.\n");
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
    int msgs_sent;
    MQTT_kill(&msgs_sent);
    printf("Sent %d messages to MQTT, processed %d and sent to task board.\n",messages_sent,msgs_sent);
    MQTT_destroy();
    tboard_exit();

}

#endif
// */