#include "tboard.h"
#include "queue/queue.h"
#include <pthread.h>
#include <stdlib.h>
#include <minicoro.h>


// TODO: consider adding function to add task_t task so we dont have to do this both here and task_create
bool msg_processor(tboard_t *t, msg_t *msg)
{ // when a message is received, it interprets message and adds to respective queue
    switch (msg->type) {
        case TASK_EXEC:
            for (int i=0; i<MAX_TASKS; i++) {
                if (t->task_list[i].status == 0) { // found empty spot
                    pthread_mutex_lock(&(t->tmutex));
                    if(t->task_list[i].status != 0){
                        pthread_mutex_unlock(&(t->tmutex));
                        continue;
                    }
                    memcpy(&(t->task_list[i]), msg->data, sizeof(task_t));
                    free(msg->data);
                    t->task_list[i].status = 1;
                    t->task_list[i].id = i;
                    if(msg->has_side_effects)
                        t->task_list[i].type = PRIMARY_EXEC;
                    else
                        t->task_list[i].type = SECONDARY_EXEC;
                    t->task_list[i].desc = mco_desc_init(t->task_list[i].fn, 0);
                    t->task_list[i].desc.user_data = msg->user_data;
                    // free(msg); // TODO: this doesnt seem to actually free anything?!
                    mco_create(&(t->task_list[i].ctx), &(t->task_list[i].desc));
                    pthread_mutex_unlock(&(t->tmutex));

                    task_add(t, &(t->task_list[i]));

                    
                    
                    return true;
                }
            }
            tboard_err("msg_processor: We have reached maximum number of concurrent tasks (%d)\n",MAX_TASKS);
            return false;
            break;
        
        case TASK_SCHEDULE:
            if (msg->subtype == PRIMARY_EXEC) {
                return bid_processing(t, (bid_t *)(msg->data));
            } else {
                tboard_err("msg_processor: Secondary scheduler unimplemented.\n");
                return false;
            }
    }
}

bool data_processor(tboard_t *t, msg_t *msg)
{ // when data is received, it interprets message and proceeds accordingly (missing requiremnts)
    tboard_err("data_processor: Data Processor unimplemented.\n");
}

bool bid_processing(tboard_t *t, bid_t *bid)
{ // missing requirements
    tboard_err("msg_processor: Primary scheduler unimplemented.\n");
    return false;
}