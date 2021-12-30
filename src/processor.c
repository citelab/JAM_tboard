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
        case TASK_EXEC: // controller wants to create local task
            ;
            // copy task_t 
            task_t *task = calloc(1, sizeof(task_t)); // freed by executor
            memcpy(task, msg->data, sizeof(task_t)); // msg->data free'd by MQTT
            task->status = TASK_INITIALIZED;
            task->id = TASK_ID_REMOTE_ISSUED;
            task->parent = NULL;
            task->cpu_time = 0; // no time has been spent executing
            if(msg->has_side_effects) // as per specs in google doc
                task->type = PRIMARY_EXEC;
            else
                task->type = SECONDARY_EXEC;
            // create task description, and fill it with user data
            task->desc = mco_desc_init((task->fn.fn), 0);
            task->desc.user_data = msg->user_data;
            task->data_size = msg->ud_allocd;
            // create task coroutine
            mco_create(&(task->ctx), &(task->desc));
            // try to add task to task board
            if (task_add(t, task) == true) {
                return true; // task was added successfully, return true
            } else {
                // unsuccessful, destroy allocated values and return false
                tboard_err("msg_processor: We have reached maximum number of concurrent tasks (%d)\n",MAX_TASKS);
                mco_destroy(task->ctx); // destroy context created
                free(task); // free task allocated above
                return false;
            }
        
        case TASK_SCHEDULE: // unimplemented in current milestones
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
{ 
    // when data is received, it interprets message and proceeds accordingly (missing requirements)
    (void)t; (void)msg;
    tboard_err("data_processor: Data Processor unimplemented.\n");
    return false;
}

bool bid_processing(tboard_t *t, bid_t *bid)
{ 
    // missing requirements
    (void)t; (void)bid;
    tboard_err("msg_processor: Primary scheduler unimplemented.\n");
    return false;
}