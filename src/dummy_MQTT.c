#include "tboard.h"
#include "dummy_MQTT.h"
#include <minicoro.h>
#include "queue/queue.h"
#include <pthread.h>
#include <string.h>
#include <time.h>
// TODO: Test that this actually works, finish making test

#define MQTT_ADD_BACK_TO_QUEUE_ON_FAILURE 1

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

void MQTT_Spawned_Task(context_t ctx)
{
    (void)ctx;
    int n = ++MQTT_n;
    printf("\tMQTT Spawned Task %d started.\n", n);
    task_yield();
    printf("\tMQTT Spawned Task %d ended.\n", n);
}


void MQTT_Print_Message(context_t ctx)
{
    (void)ctx;
    char *message = (char *)(task_get_args());
    printf("MQTT Received the following message: %s",message);
    MQTT_Increment(&imsg_sent);
}

void MQTT_Spawn_Task(context_t ctx)
{
    (void)ctx;
    tboard_t *t = (tboard_t *)(task_get_args());

    printf("MQTT Was instructed to spawn a task.\n");

    task_create(t, TBOARD_FUNC(MQTT_Spawned_Task), SECONDARY_EXEC, NULL, 0);
    MQTT_Increment(&imsg_sent);
}

void MQTT_Do_Math(context_t ctx)
{
    (void)ctx;
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
    MQTT_Increment(&imsg_sent);
}


void MQTT_init(tboard_t *t)
{
    // initialize MQTT message queues
    MQTT_Message_Queue = queue_create();
    MQTT_Message_Pool = queue_create();
    queue_init(&MQTT_Message_Queue);
    queue_init(&MQTT_Message_Pool);

    // initialize MQTT mutexes and condition variables
    pthread_mutex_init(&MQTT_Count_Mutex, NULL);
    pthread_mutex_init(&MQTT_Mutex, NULL);
    pthread_cond_init(&MQTT_Cond, NULL);

    pthread_mutex_init(&MQTT_Msg_Mutex, NULL);
    pthread_cond_init(&MQTT_Msg_Cond, NULL);

    // create MQTT incoming and outgoing threads
    pthread_create(&MQTT_oPthread, NULL, MQTT_othread, t);
    pthread_create(&MQTT_iPthread, NULL, MQTT_ithread, t);
}

void MQTT_destroy()
{
    // signal cond variable to wake a sleeping thread
    pthread_cond_signal(&MQTT_Cond);

    // join MQTT incoming and outgoing threads
    pthread_join(MQTT_iPthread, NULL);
    pthread_join(MQTT_oPthread, NULL);

    // destroy mutexes and condition variables
    pthread_mutex_destroy(&MQTT_Mutex);
    pthread_cond_destroy(&MQTT_Cond);

    pthread_mutex_destroy(&MQTT_Count_Mutex);
    pthread_mutex_destroy(&MQTT_Msg_Mutex);
    pthread_cond_destroy(&MQTT_Msg_Cond);
    
    // empty message queues of unfulfilled requests and deallocate heap data
    struct queue_entry *head;
    while ((head = queue_peek_front(&MQTT_Message_Queue)) != NULL){
        queue_pop_head(&MQTT_Message_Queue);
        if (((msg_t *)(head->data))->ud_allocd > 0 && ((msg_t *)(head->data))->user_data != NULL) {
            free(((msg_t *)(head->data))->user_data); // adding failed, delete user_data if allocated
        }
        free(((msg_t *)(head->data))->data);
        free(head->data);
        free(head);
    }
    while ((head = queue_peek_front(&MQTT_Message_Pool)) != NULL){
        queue_pop_head(&MQTT_Message_Pool);
        free(head);
    }
}

void MQTT_kill(struct MQTT_data *outp)
{
    // if outp is provided, write execution information to it
    if (outp != NULL) {
        pthread_mutex_lock(&MQTT_Count_Mutex);
        outp->imsg_recv = imsg_recv;
        outp->imsg_sent = imsg_sent;
        outp->omsg_recv = omsg_recv;
        outp->omsg_sent = omsg_sent;
        pthread_mutex_unlock(&MQTT_Count_Mutex);
    }
    // queue thread cancellation of incoming and outgoing threads
    pthread_cancel(MQTT_iPthread);
    pthread_cancel(MQTT_oPthread);
}

void MQTT_send(char *message)
{
    // sends message to dummy MQTT (simulating controller to worker message)

    // insert message from controller into message pool
    struct queue_entry *entry = queue_new_node(message);
    pthread_mutex_lock(&MQTT_Msg_Mutex);
    queue_insert_head(&MQTT_Message_Pool, entry);
    pthread_mutex_unlock(&MQTT_Msg_Mutex);

    // signal that new message has been sent incase thread is sleeping
    pthread_cond_signal(&MQTT_Cond); // incase thread is waiting for new message

}

void MQTT_issue_remote_task(tboard_t *t, remote_task_t *rtask)
{
    // issue remote task to controller

    if (t == NULL || rtask == NULL)
        return;
    
    // copy message to send
    char message[MAX_MSG_LENGTH+1] = {0};
    strcpy(message, rtask->message);


    // simulate worker response by parsing message
    char *tok = strtok(message, " ");
    if (strcmp(tok, "print") == 0) { // asking controller to print, non-blocking
        printf("Remote MQTT recieved the following message: %s\n",(char *)rtask->data);
        rtask->status = TASK_COMPLETED;
        MQTT_Increment(&omsg_sent);
    } else if(strcmp(tok, "math") == 0) { // asking controller to do math, blocking
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
    // place response back into task board via remote_task_place() function call

    remote_task_place(t, rtask, RTASK_RECV);
    pthread_mutex_lock(&(t->pmutex));
    pthread_cond_signal(&(t->pcond));
    pthread_mutex_unlock(&(t->pmutex));

}

void MQTT_recv(tboard_t *t)
{
    // recieve message from controller
    pthread_mutex_lock(&MQTT_Msg_Mutex);
    struct queue_entry *entry = queue_peek_front(&MQTT_Message_Pool);
    if (entry == NULL) { // no messages from controller in message pool, return
        pthread_mutex_unlock(&MQTT_Msg_Mutex);
        return;
    }
    queue_pop_head(&MQTT_Message_Pool);
    // parse message worker received
    char *orig_message = (char *)(entry->data);
    char message[MAX_MSG_LENGTH+1] = {0};
    strcpy(message, orig_message);
    char *tok = strtok(message, " ");
    struct queue_entry *mentry;
    // parse message. I will fully annotate only one as the rest follow the same structure
    if(strcmp(tok, "print") == 0){ // controller wishes to print
        tok = strtok(NULL, ""); // can I do this to get the rest of the string?
        msg_t *msg = calloc(1, sizeof(msg_t)); // create message to send to task board
        msg->type = TASK_EXEC;
        // copy data sent from controller
        msg->user_data = calloc(strlen(orig_message)-5, sizeof(char)); // free'd when MQTT_Print_Message() terminates
        // indicate that memory was allocated to be free'd later by task board
        if(strlen(orig_message)-5 <= 0)
            msg->ud_allocd = 1;
        else
            msg->ud_allocd = strlen(orig_message)-5;
        // copy data from controller to be passed to task board
        memcpy(msg->user_data, orig_message+6, strlen(orig_message)-6);
        // create task_t to pass to tboard containing pointer to local function task board will run
        msg->data = (task_t *)calloc(1, sizeof(task_t)); // free'd in MQTT_Thread()
        ((task_t *)(msg->data))->fn = TBOARD_FUNC(MQTT_Print_Message);

        msg->has_side_effects = false; // we are printing so no major side effects
        // create queue entry
        mentry = queue_new_node(msg);

    }else if(strcmp(tok, "spawn") == 0){
        
        msg_t *msg = calloc(1, sizeof(msg_t));
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
            pthread_mutex_unlock(&MQTT_Msg_Mutex);
            free(entry);
            return;
        }
        char op = *op_str;
        char *b_str = strtok(NULL, " ");
        double a = atof(a_str);
        double b = atof(b_str);
        msg_t *msg = calloc(1, sizeof(msg_t));
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
    }
    // Insert message into message queue to be sent to task board in MQTT_ithread()
    // We dont need to lock mutex as this should be run exclusively by MQTT_Thread, where mutex
    // is already locked before
    queue_insert_tail(&MQTT_Message_Queue, mentry);
    pthread_mutex_unlock(&MQTT_Msg_Mutex);
    MQTT_Increment(&imsg_recv);
    free(entry); // free message pool queue entry
}

void *MQTT_othread(void *args)
{
    // thread that handles worker-to-controller communication
    tboard_t *t = (tboard_t *)args;
    // set cancel state so thread cannot prematurely exit (ensures graceful termination)
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    while (true){
        // create the only cancellation point in the entire thread
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        // see if task board has sent any messages
        struct queue_entry *ohead = queue_peek_front(&(t->msg_sent));
        if (ohead == NULL) { // no messages, so we sleep on tboard->msg_cond
            pthread_mutex_lock(&(t->msg_mutex));
            pthread_cond_wait(&(t->msg_cond), &(t->msg_mutex));
            pthread_mutex_unlock(&(t->msg_mutex));
        } else { // we received a message, so we will sent it to controller
            pthread_mutex_lock(&(t->msg_mutex));
            // get message
            ohead = queue_peek_front(&(t->msg_sent));
            if(ohead != NULL) queue_pop_head(&(t->msg_sent));
            pthread_mutex_unlock(&(t->msg_mutex));
            if(ohead == NULL) continue;

            // send message to controller
            MQTT_Increment(&omsg_recv);
            MQTT_issue_remote_task(t, (remote_task_t *)(ohead->data));

            // free queue entry
            free(ohead);
        }
    }
}

void *MQTT_ithread(void *args)
{
    tboard_t *t = (tboard_t *)args;
    // set cancel state so thread cannot prematurely exit (ensures graceful termination)
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    while (true){
        // create the only cancellation point in the entire thread
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        // see if MQTT has any messages from controller to fulfill
        struct queue_entry *ihead = queue_peek_front(&MQTT_Message_Queue);
        if (ihead == NULL){ // nothing in message queue, so we check to see if we recieved a new message to process
            ihead = queue_peek_front(&MQTT_Message_Pool);
            if (ihead == NULL) { // no new message in pool either, wait until this changes
                pthread_mutex_lock(&MQTT_Mutex);
                pthread_cond_wait(&MQTT_Cond, &MQTT_Mutex);
                pthread_mutex_unlock(&MQTT_Mutex);
            } else { // new message in message pool, so we must parse it
                MQTT_recv(t);
            }
        } else { // we have a message in message queue we must fulfil
            pthread_mutex_lock(&MQTT_Mutex);
            // get msg_t from message queue
            queue_pop_head(&MQTT_Message_Queue);
            msg_t *msg = (msg_t *)(ihead->data);
            // sent message to task board via msg_processor() call
            bool res = msg_processor(t, msg);
            // we could not add message to task board, so if indicated we return message to queue to be added later
            if(!res && MQTT_ADD_BACK_TO_QUEUE_ON_FAILURE == 1) {
                queue_insert_tail(&MQTT_Message_Queue, ihead);
                nanosleep(&MQTT_sleep_ts, NULL);
            } else { // perform proper garbage collection
                // if message was not successful but we are not supposed to return message to queue, we must free msg->user_data
                // if we allocated memory to store it
                if(!res && msg->ud_allocd > 0 && msg->user_data != NULL) {
                    free(msg->user_data); // adding failed, delete user_data if allocated
                }
                // free task_t in msg->data 
                free(msg->data);
                // free msg
                free(msg);
                // free queue entry
                free(ihead);
            }
            // unlock mutex
            pthread_mutex_unlock(&MQTT_Mutex);
        }
    }
}