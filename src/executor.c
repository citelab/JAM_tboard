/* This controls the primary executor and secondary executor */


#include "tboard.h"
#include "queue/queue.h"
#include <pthread.h>
#include <assert.h>



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
            //task->yields++;
            //task->hist->yields++;
            int status = mco_status(task->ctx);
            
            if (status == MCO_SUSPENDED) {
                task->yields++;
                task->hist->yields++;
                int task_type = task->type;
                struct queue_entry *e = NULL;
                if (mco_get_bytes_stored(task->ctx) == sizeof(task_t)) {
                    // indicative of blocking task creation, so we must retrieve it
                    task_t *subtask = calloc(1, sizeof(task_t)); // freed on termination
                    assert(mco_pop(task->ctx, subtask, sizeof(task_t)) == MCO_SUCCESS);
                    subtask->parent = task;
                    task_type = subtask->type;
                    //e = queue_new_node(subtask);
                    task_place(tboard, subtask);
                } else if (mco_get_bytes_stored(task->ctx) == sizeof(remote_task_t)) {
                    // indicative of remote task creation, so we must retrieve it
                    remote_task_t *rtask = calloc(1, sizeof(remote_task_t)); // freed on retrieval
                    assert(mco_pop(task->ctx, rtask, sizeof(remote_task_t)) == MCO_SUCCESS);
                    rtask->calling_task = task;
                    remote_task_place(tboard, rtask, RTASK_SEND);
                    if (!rtask->blocking)
                        e = queue_new_node(task);
                } else {
                    e = queue_new_node(task);
                }

                if (e != NULL){
                    pthread_mutex_lock(mutex);
                    if (REINSERT_PRIORITY_AT_HEAD == 1 && task_type == PRIORITY_EXEC)
                        queue_insert_head(q, e);
                    else
                        queue_insert_tail(q, e);
                    if(type == PRIMARY_EXEC) pthread_cond_signal(cond); // we wish to wake secondary executors if they are asleep
                    pthread_mutex_unlock(mutex);
                }
            } else if (status == MCO_DEAD) {
                task->status = TASK_COMPLETED;
                history_record_exec(tboard, task, &(task->hist));
                if (task->parent != NULL) { // blocking task just terminated, we wish to return parent to queue
                    assert(mco_push(task->parent->ctx, task, sizeof(task_t)) == MCO_SUCCESS);
                    task_place(tboard, task->parent); // place parent back in appropriate queue
                    /*struct queue_entry *e = queue_new_node(task->parent);
                    pthread_mutex_lock(mutex);
                    queue_insert_tail(q, e);
                    if(type == PRIMARY_EXEC) pthread_cond_signal(cond); // we wish to wake secondary executors
                    pthread_mutex_unlock(mutex);*/
                } else {
                    // we only want to deincrement concurrent count for parent tasks ending
                    // since only one blocking task can be created at a time, and blocked task
                    // essentially takes the place of the parent. Of course, nesting blocked tasks
                    // should be done with caution as there is essentially no upward bound, meaning
                    // large levels of nested blocked tasks could exhaust memory
                    tboard_deinc_concurrent(tboard);
                }
                if (task->data_size > 0 && task->desc.user_data != NULL)
                    free(task->desc.user_data);
                mco_destroy(task->ctx);
                free(task);
                
                
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