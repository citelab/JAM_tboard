/**
 * Test 5: Milestone 2, controller to worker (MQTT to tboard) exclusively
 * 
 * We create a thread that simulates MQTT receiving remote messages. This thread,
 * with function generate_MQTT_message(), generates remote messages and sends them
 * to the dummy MQTT adapter
 * 
 * Remote tasks:
 * * MQTT_Print_Message() - Prints message and terminates
 * * MQTT_Do_Math() - Does arithmetic, prints result and terminates
 * * MQTT_Spawn_Task() - Spawns local task, which prints and terminates
 * 
 */

#include "tests.h"
#ifdef TEST_5

#include "../tboard.h"
#include "../dummy_MQTT.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stdbool.h>


pthread_t message_generator;
long kill_time, test_time;
int messages_sent = 0;
struct MQTT_data mqtt_data;

void *generate_MQTT_message(void *args);




int main()
{
    printf("Working");
    test_time = clock();
    init_tests();


    destroy_tests();
    test_time = clock() - test_time;

    printf("\n=================== TEST STATISTICS ================\n");
    printf("Test took %ld CPU cycles to complete, killing taskboard took %ld CPU cycles to complete.\n",test_time, kill_time);
    
    tboard_exit();
}

void init_tests()
{
    tboard = tboard_create(SECONDARY_EXECUTORS);

    tboard_start(tboard);
    MQTT_init(tboard);

    pthread_create(&message_generator, NULL, generate_MQTT_message, tboard);
    pthread_create(&tb_killer, NULL, kill_tboard, tboard);

    printf("Taskboard created, all threads initialized.\n");
}

void destroy_tests()
{
    pthread_join(message_generator, NULL);
    tboard_destroy(tboard);
    pthread_join(tb_killer, NULL);
    MQTT_destroy();
}

void *generate_MQTT_message(void *args)
{
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
        if(RAPID_GENERATION == 1) fsleep(0.0003);
        else fsleep(rand() % 4);
    }
    return NULL;
}

void *kill_tboard (void *args)
{
    tboard_t *t = (tboard_t *)args;
    fsleep(RAPID_GENERATION ? 2 : MAX_RUN_TIME);
    pthread_mutex_lock(&(t->tmutex));
    pthread_cancel(message_generator);
    
    kill_time = clock();
    MQTT_kill(&mqtt_data);
    tboard_kill(t);
    kill_time = clock() - kill_time;
    
    printf("=================== TASK STATISTICS ================\n");
    history_print_records(t, stdout);
    pthread_mutex_unlock(&(t->tmutex));
    return NULL;
}

void *check_completion(void *args)
{
    tboard_t *t = (tboard_t *)args;
    (void)t;
    return NULL;
}


#endif