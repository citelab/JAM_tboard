/* This controls the primary executor and secondary executor */


#include "tboard.h"
#include "queue/queue.h"
#include <pthread.h>




void *executor(void *arg)
{
    exec_t args = *((exec_t *)arg);
    // TODO: consider freeing arg right here, and not saving exec_t in tboard_t object
    tboard_t *tboard = args.tboard;
    int type = args.type;
    int num = args.num;

    while (true) {
        pthread_testcancel(); // we can end thread safely here
        task_sequencer(tboard); // run sequencer
        struct queue_entry *next = NULL;
        struct queue *q = NULL;
        pthread_mutex_t *mutex = NULL;
        pthread_cond_t *cond = NULL;
        ////// Fetch next process to run (TODO: add scheduler support) ////////
        if (type == PRIMARY_EXEC) {
            pthread_mutex_lock(&(tboard->pmutex));
            mutex = &(tboard->pmutex);
            cond = &(tboard->pcond);
            q = &(tboard->pqueue);
            next = queue_peek_front(q);
            if (next) { 
                queue_pop_head(q);
            } else {
                mutex = NULL;
                for(int i=0; i<tboard->sqs; i++){
                    pthread_mutex_lock(&(tboard->smutex[i]));
                    q = &(tboard->squeue[i]);
                    next = queue_peek_front(q);
                    if(next){
                        queue_pop_head(q);
                        mutex = &(tboard->smutex[i]);
                        cond = &(tboard->scond[i]);
                        pthread_mutex_unlock(&(tboard->smutex[i]));
                        break;
                    }
                    pthread_mutex_unlock(&(tboard->smutex[i]));
                }
            }
            pthread_mutex_unlock(&(tboard->pmutex));
        } else {
            pthread_mutex_lock(&(tboard->smutex[num]));
            mutex = &(tboard->smutex[num]);
            cond = &(tboard->scond[num]);
            q = &(tboard->squeue[num]);
            next = queue_peek_front(q);
            if(next) queue_pop_head(q);
            pthread_mutex_unlock(&(tboard->smutex[num]));
        }
        if (next) {
            ////////// Get queue data, and swap context to function until yielded ///////////

            task_t *task = ((task_t *)(next->data));
            //mco_resume(task->ctx);
            mco_resume(tboard->task_list[task->id].ctx);

            int status = mco_status(task->ctx);
            if (status == MCO_SUSPENDED) {
                pthread_mutex_lock(mutex);
                struct queue_entry *e = queue_new_node(&(tboard->task_list[task->id]));
                queue_insert_tail(q, e);

                if(type == PRIMARY_EXEC) pthread_cond_signal(cond);
                pthread_mutex_unlock(mutex);

            } else if (status == MCO_DEAD) {
                mco_destroy(task->ctx);
                task_t empty_task = {0};
                memcpy(&(tboard->task_list[task->id]), &empty_task, sizeof(task_t));
            } else {
                printf("Unexpected status received: %d, will lose task.\n",status);
            }


            free(next);
        } else { // empty queue, we wait until signal
            if (type == PRIMARY_EXEC) { // nothing left in queue, so if we init shutdown then we wait for rest of threads to finish as well (they should all be at this point if we found nothing to do)
                /*tboard->init_shutdown = 1;
                for(int i=0; i<tboard->sqs; i++){
                    pthread_mutex_lock(&(tboard->smutex[i]));
                    pthread_cond_signal(&(tboard->scond[i]));//, &(tboard->smutex[i]));
                    pthread_mutex_unlock(&(tboard->smutex[i]));
                }
                break;*/
                pthread_mutex_lock(&(tboard->pmutex));
                pthread_cond_wait(&(tboard->pcond), &(tboard->pmutex));
                pthread_mutex_unlock(&(tboard->pmutex));
            } else {
                pthread_mutex_lock(&(tboard->smutex[num]));
                //printf("Sleeping scond %d.\n\n",num);
                pthread_cond_wait(&(tboard->scond[num]), &(tboard->smutex[num]));
                //printf("Resuming scond %d.\n\n",num);
                pthread_mutex_unlock(&(tboard->smutex[num]));
                //if(tboard->init_shutdown == 1){
                //    break;
                //}
            }
            
        }
    }
     
}