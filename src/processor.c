#include "tboard.h"
#include "processor.h"
#include "queue/queue.h"
#include <pthread.h>
#include <stdlib.h>
#include <minicoro.h>


// TODO: consider adding function to add task_t task so we dont have to do this both here and task_create
bool msg_processor(tboard_t *t, msg_t *msg)
{ // when a message is received, it interprets message and adds to respective queue
    switch (msg->type) {
        case TASK_EXEC:
            ;
            task_t *task = calloc(1, sizeof(task_t));

            memcpy(task, msg->data, sizeof(task_t)); // free expected by executor
            task->status = 1;
            task->id = TASK_ID_REMOTE_ISSUED;
            task->parent = NULL;
            task->cpu_time = 0;
            if(msg->has_side_effects)
                task->type = PRIMARY_EXEC;
            else
                task->type = SECONDARY_EXEC;
            task->desc = mco_desc_init((task->fn.fn), 0);
            task->desc.user_data = msg->user_data;
            task->data_size = msg->ud_allocd;
            mco_create(&(task->ctx), &(task->desc));
            // pthread_mutex_unlock(&(t->tmutex)); this shouldnt be locked!
            if (task_add(t, task) == true) {
                return true;
            } else {
                // unsuccessful, so we must deallocate allocate space
                tboard_err("msg_processor: We have reached maximum number of concurrent tasks (%d)\n",MAX_TASKS);
                mco_destroy(task->ctx);
                free(task);
                return false;
            }
        
        case TASK_SCHEDULE:
            if (msg->subtype == PRIMARY_EXEC) {
                return bid_processing(t, (bid_t *)(msg->data));
            } else {
                tboard_err("msg_processor: Secondary scheduler unimplemented.\n");
                return false;
            }
        default:
            tboard_err("msg_processor: Invalid message type encountered: %d\n", msg->type);
            return false;
    }
}

bool data_processor(tboard_t *t, msg_t *msg)
{ // when data is received, it interprets message and proceeds accordingly (missing requiremnts)
    (void)t; (void)msg;
    tboard_err("data_processor: Data Processor unimplemented.\n");
    return false;
}

bool bid_processing(tboard_t *t, bid_t *bid)
{ // missing requirements
    (void)t; (void)bid;
    tboard_err("msg_processor: Primary scheduler unimplemented.\n");
    return false;
}