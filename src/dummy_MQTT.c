#include "tboard.h"
#include "dummy_MQTT.h"
#include <minicoro.h>
#include "queue/queue.h"
#include <pthread.h>
#include <string.h>
// TODO: Test that this actually works, finish making test

int msg_t_sent = 0;

void MQTT_Spawned_Task(void *args)
{
    int n = ++MQTT_n;
    printf("\tMQTT Spawned Task %d started.\n", n);
    task_yield();
    printf("\tMQTT Spawned Task %d ended.\n", n);
}


void MQTT_Print_Message(void *args)
{
    char *message = (char *)(task_get_args());
    printf("MQTT Received the following message: %s",message);
    free(message);
}

void MQTT_Spawn_Task(void *args)
{
    tboard_t *t = (tboard_t *)(task_get_args());

    printf("MQTT Was instructed to spawn a task.\n");

    task_create(t, TBOARD_FUNC(MQTT_Spawned_Task), SECONDARY_EXEC, NULL);
}

void MQTT_Do_Math(void *args)
{
    struct arithmetic_s *op = (struct arithmetic_s *)(task_get_args());
    double ans = op->a;
    switch(op->operator){
        case '+':
            ans += op->b;
            break;
        case '-':
            ans -= op->b;
            break;
        case '*':
            ans *= op->b;
            break;
        case '/':
            ans /= op->b;
            break;
        default:
            printf("MQTT Could not do arithmetic, unexpected results occured: %f %c %f unsupported.\n",op->a, op->operator, op->b);
            free(op);
            return;
    }
    printf("MQTT did math, got %f %c %f = %f.\n",op->a, op->operator, op->b,ans);
    free(op);
}


void MQTT_init(tboard_t *t)
{
    MQTT_Message_Queue = queue_create();
    MQTT_Message_Pool = queue_create();
    queue_init(&MQTT_Message_Queue);
    queue_init(&MQTT_Message_Pool);

    pthread_mutex_init(&MQTT_Mutex, NULL);
    pthread_cond_init(&MQTT_Cond, NULL);

    pthread_mutex_init(&MQTT_Msg_Mutex, NULL);
    pthread_cond_init(&MQTT_Msg_Cond, NULL);

    pthread_create(&MQTT_Pthread, NULL, MQTT_thread, t);
}

void MQTT_destroy()
{
    pthread_join(MQTT_Pthread, NULL);

    pthread_mutex_destroy(&MQTT_Mutex);
    pthread_cond_destroy(&MQTT_Cond);

    pthread_mutex_destroy(&MQTT_Msg_Mutex);
    pthread_cond_destroy(&MQTT_Msg_Cond);
}

void MQTT_kill(int *msgs_sent)
{
    *msgs_sent = msg_t_sent;
    pthread_cancel(MQTT_Pthread);
}

void MQTT_send(char *message)
{
    struct queue_entry *entry = queue_new_node(message);
    pthread_mutex_lock(&MQTT_Msg_Mutex);
    queue_insert_head(&MQTT_Message_Pool, entry);
    pthread_mutex_unlock(&MQTT_Msg_Mutex);


    pthread_cond_signal(&MQTT_Cond); // incase thread is waiting for new message

}

void MQTT_recv(tboard_t *t)
{
    pthread_mutex_lock(&MQTT_Msg_Mutex);
    struct queue_entry *entry = queue_peek_front(&MQTT_Message_Pool);
    if(entry == NULL)
        return;
    queue_pop_head(&MQTT_Message_Pool);
    pthread_mutex_unlock(&MQTT_Msg_Mutex);

    char *orig_message = (char *)(entry->data);
    char message[256] = {0};
    strcpy(message, orig_message);
    char *tok = strtok(message, " ");
    struct queue *mentry;
    msg_t m = {0}; // in place of calloc since freeing it in msg_processor doesnt seem to work (different threads?)
    if(strcmp(tok, "print") == 0){
        tok = strtok(NULL, ""); // can I do this to get the rest of the string?
        msg_t *msg = &m; //calloc(1, sizeof(msg_t));
        msg->type = TASK_EXEC;
        msg->user_data = calloc(strlen(orig_message)-5, sizeof(char)); // free'd in MQTT_Print_Message()
        msg->ud_allocd = true;
        memcpy(msg->user_data, orig_message+6, strlen(orig_message)-6);
        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Print_Message);
        msg->has_side_effects = false;
        
        mentry = queue_new_node(msg);

    }else if(strcmp(tok, "spawn") == 0){
        
        msg_t *msg = &m; //calloc(1, sizeof(msg_t));
        msg->type = TASK_EXEC;
        msg->user_data = t;
        msg->ud_allocd = false;
        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Spawn_Task);
        msg->has_side_effects = true;
        
        mentry = queue_new_node(msg);
    }else if(strcmp(tok, "math") == 0){
        char *a_str = strtok(NULL, " ");
        char *op_str = strtok(NULL, " ");
        if(strlen(op_str) != 1){
            printf("Arithmetic function has incorrect operation value %s.\n", op_str);
            free(entry);
            return;
        }
        char op = *op_str;
        char *b_str = strtok(NULL, " ");
        double a = atof(a_str);
        double b = atof(b_str);
        msg_t *msg = &m; //calloc(1, sizeof(msg_t));
        msg->type = TASK_EXEC;
        msg->user_data = (struct arithmetic_s *)calloc(1, sizeof(struct arithmetic_s)); // free'd in MQTT_Do_Math()
        msg->ud_allocd = true;
        ((struct arithmetic_s *)(msg->user_data))->a = a;
        ((struct arithmetic_s *)(msg->user_data))->b = b;
        ((struct arithmetic_s *)(msg->user_data))->operator = op;

        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Do_Math);
        msg->has_side_effects = false;
        
        mentry = queue_new_node(msg);
    }else{
        printf("Invalid messaged received: %s\n",message);
        free(entry);
    }
    // dont need to lock mutex as this should be run exclusively by MQTT_Thread, where mutex
    // is already locked before
    msg_t_sent++;
    queue_insert_tail(&MQTT_Message_Queue, mentry);
    free(entry);
}
void MQTT_thread(void *args)
{
    tboard_t *t = (tboard_t *)args;
    while (true){
        pthread_mutex_lock(&MQTT_Mutex);
        struct queue_entry *head = queue_peek_front(&MQTT_Message_Queue);
        if (head == NULL){ // nothing in message pool, check to see if we recieved a message
            head = queue_peek_front(&MQTT_Message_Pool);
            if (head == NULL) // no new message in pool either, wait until this changes
                pthread_cond_wait(&MQTT_Cond, &MQTT_Mutex);
            else
                MQTT_recv(t);
            pthread_mutex_unlock(&MQTT_Mutex);
            continue;
        } else {
            queue_pop_head(&MQTT_Message_Queue);
            msg_t msg;
            memcpy(&msg, (msg_t *)(head->data), sizeof(msg_t)); // no segfaults cause we sent copy, removed from stack automatically
            if(!msg_processor(t, &msg) && msg.ud_allocd && msg.user_data != NULL)
                free(msg.user_data); // adding failed, delete user_data if allocated
            free(msg.data);
        }

        free(head);
        pthread_mutex_unlock(&MQTT_Mutex);
    }
}