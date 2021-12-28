#include "tboard.h"
#include "dummy_MQTT.h"
#include <minicoro.h>
#include "queue/queue.h"
#include <pthread.h>
#include <string.h>
#include <time.h>
// TODO: Test that this actually works, finish making test

int imsg_sent = 0;
int imsg_recv = 0;

int omsg_sent = 0;
int omsg_recv = 0;

void MQTT_Increment(int *value)
{
    pthread_mutex_lock(&MQTT_Count_Mutex);
    *value = *value + 1;
    pthread_mutex_unlock(&MQTT_Count_Mutex);
}
struct timespec MQTT_sleep_ts = {
    .tv_sec = 0,
    .tv_nsec = 10000000,
};

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
    MQTT_Increment(&imsg_recv);
}

void MQTT_Spawn_Task(void *args)
{
    tboard_t *t = (tboard_t *)(task_get_args());

    printf("MQTT Was instructed to spawn a task.\n");

    task_create(t, TBOARD_FUNC(MQTT_Spawned_Task), SECONDARY_EXEC, NULL, 0);
    MQTT_Increment(&imsg_recv);
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
            return;
    }
    printf("MQTT did math, got %f %c %f = %f.\n",op->a, op->operator, op->b,ans);
    MQTT_Increment(&imsg_recv);
}


void MQTT_init(tboard_t *t)
{
    MQTT_Message_Queue = queue_create();
    MQTT_Message_Pool = queue_create();
    queue_init(&MQTT_Message_Queue);
    queue_init(&MQTT_Message_Pool);

    pthread_mutex_init(&MQTT_Count_Mutex, NULL);
    pthread_mutex_init(&MQTT_Mutex, NULL);
    pthread_cond_init(&MQTT_Cond, NULL);

    pthread_mutex_init(&MQTT_Msg_Mutex, NULL);
    pthread_cond_init(&MQTT_Msg_Cond, NULL);

    pthread_create(&MQTT_oPthread, NULL, MQTT_othread, t);
    pthread_create(&MQTT_iPthread, NULL, MQTT_ithread, t);
}

void MQTT_destroy()
{
    //pthread_mutex_lock(&MQTT_Mutex);
    pthread_cond_signal(&MQTT_Cond);
    //pthread_mutex_unlock(&MQTT_Mutex);

    printf("Joining\n");
    pthread_join(MQTT_iPthread, NULL);
    pthread_join(MQTT_oPthread, NULL);
    printf("End Joined.\n");
    pthread_mutex_destroy(&MQTT_Mutex);
    pthread_cond_destroy(&MQTT_Cond);

    pthread_mutex_destroy(&MQTT_Count_Mutex);
    pthread_mutex_destroy(&MQTT_Msg_Mutex);
    pthread_cond_destroy(&MQTT_Msg_Cond);
}

void MQTT_kill(struct MQTT_data *outp)
{
    if (outp != NULL) {
        pthread_mutex_lock(&MQTT_Count_Mutex);
        outp->imsg_recv = imsg_recv;
        outp->imsg_sent = imsg_sent;
        outp->omsg_recv = omsg_recv;
        outp->omsg_sent = omsg_sent;
        pthread_mutex_unlock(&MQTT_Count_Mutex);
    }

    pthread_cancel(MQTT_iPthread);
    pthread_cancel(MQTT_oPthread);
}

void MQTT_send(char *message)
{
    struct queue_entry *entry = queue_new_node(message);
    pthread_mutex_lock(&MQTT_Msg_Mutex);
    queue_insert_head(&MQTT_Message_Pool, entry);
    pthread_mutex_unlock(&MQTT_Msg_Mutex);


    pthread_cond_signal(&MQTT_Cond); // incase thread is waiting for new message

}

void MQTT_issue_remote_task(tboard_t *t, remote_task_t *rtask)
{
    if (t == NULL || rtask == NULL)
        return;
    
    char message[MAX_MSG_LENGTH+1] = {0};
    strcpy(message, rtask->message);

    char *tok = strtok(message, " ");

    if (strcmp(tok, "print") == 0) {
        printf("Remote MQTT recieved the following message: %s\n",(char *)rtask->data);
        rtask->status = TASK_COMPLETED;
        MQTT_Increment(&omsg_sent);
    } else if(strcmp(tok, "math") == 0) {
        struct rarithmetic_s *data = (struct rarithmetic_s *)rtask->data;
        switch (data->operator) {
            case '+':
                ; data->ans = data->a + data->b;
                break;
            case '-':
                ; data->ans = data->a - data->b;
                break;
            case '*':
                ; data->ans = data->a * data->b;
                break;
            case '/':
                ; data->ans = data->a / data->b;
                break;
        }
        rtask->status = TASK_COMPLETED;
        MQTT_Increment(&omsg_sent);
    } else {
        tboard_err("MQTT_issue_remote_task: Invalid remote task issued by task board.\n");
    }
    remote_task_place(t, rtask, RTASK_RECV);
}

void MQTT_recv(tboard_t *t)
{
    pthread_mutex_lock(&MQTT_Msg_Mutex);
    struct queue_entry *entry = queue_peek_front(&MQTT_Message_Pool);
    if (entry != NULL)
        queue_pop_head(&MQTT_Message_Pool);
    pthread_mutex_unlock(&MQTT_Msg_Mutex);
    if (entry == NULL)
        return;
    char *orig_message = (char *)(entry->data);
    char message[MAX_MSG_LENGTH+1] = {0};
    strcpy(message, orig_message);
    char *tok = strtok(message, " ");
    struct queue *mentry;
    msg_t m = {0}; // in place of calloc since freeing it in msg_processor doesnt seem to work (different threads?)
    if(strcmp(tok, "print") == 0){
        tok = strtok(NULL, ""); // can I do this to get the rest of the string?
        msg_t *msg = &m; //calloc(1, sizeof(msg_t));
        msg->type = TASK_EXEC;
        msg->user_data = calloc(strlen(orig_message)-5, sizeof(char)); // free'd when MQTT_Print_Message() terminates
        if(strlen(orig_message)-5 <= 0)
            msg->ud_allocd = 1;
        else
            msg->ud_allocd = strlen(orig_message)-5;
        memcpy(msg->user_data, orig_message+6, strlen(orig_message)-6);
        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Print_Message);
        msg->has_side_effects = false;
        
        mentry = queue_new_node(msg);

    }else if(strcmp(tok, "spawn") == 0){
        
        msg_t *msg = &m; //calloc(1, sizeof(msg_t));
        msg->type = TASK_EXEC;
        msg->user_data = t;
        msg->ud_allocd = 0; // we do not wish to free taskboard!!
        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Spawn_Task);
        msg->has_side_effects = true;
        
        mentry = queue_new_node(msg);
    }else if(strcmp(tok, "math") == 0){
        char *a_str = strtok(NULL, " ");
        char *op_str = strtok(NULL, " ");
        if(strlen(op_str) != 1){
            tboard_err("MQTT_Recv: Arithmetic function has incorrect operation value %s.\n", op_str);
            free(entry);
            return;
        }
        char op = *op_str;
        char *b_str = strtok(NULL, " ");
        double a = atof(a_str);
        double b = atof(b_str);
        msg_t *msg = &m; //calloc(1, sizeof(msg_t));
        msg->type = TASK_EXEC;
        msg->user_data = (struct arithmetic_s *)calloc(1, sizeof(struct arithmetic_s)); // free'd when MQTT_Do_Math() terminates
        msg->ud_allocd = sizeof(struct arithmetic_s);
        ((struct arithmetic_s *)(msg->user_data))->a = a;
        ((struct arithmetic_s *)(msg->user_data))->b = b;
        ((struct arithmetic_s *)(msg->user_data))->operator = op;

        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Do_Math);
        msg->has_side_effects = false;
        
        mentry = queue_new_node(msg);
    }else{
        tboard_err("MQTT_Recv: Invalid messaged received: %s\n",message);
        free(entry);
    }
    // dont need to lock mutex as this should be run exclusively by MQTT_Thread, where mutex
    // is already locked before
    MQTT_Increment(&imsg_sent);
    queue_insert_tail(&MQTT_Message_Queue, mentry);
    free(entry);
}

void MQTT_othread(void *args)
{
    tboard_t *t = (tboard_t *)args;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    while (true){
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        struct queue_entry *ohead = queue_peek_front(&(t->msg_sent));
        if (ohead == NULL) {
            //pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            pthread_mutex_lock(&(t->msg_mutex));
            pthread_cond_wait(&(t->msg_cond), &(t->msg_mutex));//, &(MQTT_sleep_ts));
            pthread_mutex_unlock(&(t->msg_mutex));
            //pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        } else {
            pthread_mutex_lock(&(t->msg_mutex));
            ohead = queue_peek_front(&(t->msg_sent));
            if(ohead != NULL) queue_pop_head(&(t->msg_sent));
            pthread_mutex_unlock(&(t->msg_mutex));
            if(ohead == NULL) continue;

            MQTT_Increment(&omsg_recv);
            MQTT_issue_remote_task(t, (remote_task_t *)(ohead->data));
            free(ohead);
        }
    }
}

void MQTT_ithread(void *args)
{
    tboard_t *t = (tboard_t *)args;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    while (true){
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        
        struct queue_entry *ihead = queue_peek_front(&MQTT_Message_Queue);
        if (ihead == NULL){ // nothing in message pool, check to see if we recieved a message
            ihead = queue_peek_front(&MQTT_Message_Pool);
            if (ihead == NULL) { // no new message in pool either, wait until this changes
                //pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
                pthread_mutex_lock(&MQTT_Mutex);
                pthread_cond_wait(&MQTT_Cond, &MQTT_Mutex);//, &MQTT_sleep_ts);
                pthread_mutex_unlock(&MQTT_Mutex);
                //pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            } else {
                MQTT_recv(t);
            }
        } else {
            pthread_mutex_lock(&MQTT_Mutex);
            queue_pop_head(&MQTT_Message_Queue);
            msg_t msg;
            memcpy(&msg, (msg_t *)(ihead->data), sizeof(msg_t)); // no segfaults cause we sent copy, removed from stack automatically
            if(!msg_processor(t, &msg) && msg.ud_allocd > 0 && msg.user_data != NULL)
                free(msg.user_data); // adding failed, delete user_data if allocated
            free(msg.data);
            free(ihead);
            pthread_mutex_unlock(&MQTT_Mutex);
        }
    }
}