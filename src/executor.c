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
    long start_time, end_time;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // disable premature cancellation by tboard_kill()
    while (true) {
        // create single cancellation point
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

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
            
            task->status = TASK_RUNNING;

            start_time = clock();
            mco_resume(task->ctx);
            end_time = clock();

            task->cpu_time += (end_time - start_time);
            task->yields++;
            task->hist->yields++;
            int status = mco_status(task->ctx);
            
            if (status == MCO_SUSPENDED) {

                pthread_mutex_lock(mutex);
                struct queue_entry *e = queue_new_node(task);
                queue_insert_tail(q, e);

                if(type == PRIMARY_EXEC) pthread_cond_signal(cond);
                pthread_mutex_unlock(mutex);
            } else if (status == MCO_DEAD) {
                task->status = TASK_COMPLETED;
                history_record_exec(tboard, task, &(task->hist));
                if (task->data_size > 0)
                    free(task->desc.user_data);
                mco_destroy(task->ctx);
                free(task);
                tboard_deinc_concurrent(tboard);
                
            } else {
                printf("Unexpected status received: %d, will lose task.\n",status);
            }


            free(next);
        } else { // empty queue, we wait until signal
            if (type == PRIMARY_EXEC) { // nothing left in queue, so if we init shutdown then we wait for rest of threads to finish as well (they should all be at this point if we found nothing to do)
                pthread_mutex_lock(&(tboard->pmutex));
                pthread_cond_wait(&(tboard->pcond), &(tboard->pmutex));
                pthread_mutex_unlock(&(tboard->pmutex));
            } else {
                pthread_mutex_lock(&(tboard->smutex[num]));
                pthread_cond_wait(&(tboard->scond[num]), &(tboard->smutex[num]));
                pthread_mutex_unlock(&(tboard->smutex[num]));
            }
        }
    }
     
}