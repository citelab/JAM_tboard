#include "tboard.h"

#include "history.h"
#include <uthash.h>
#include <string.h>




void history_record_exec(tboard_t *t, task_t *task, history_t **hist)
{
    // check if function exists in hash table
    pthread_mutex_lock(&(t->hmutex));
    HASH_FIND_STR(t->exec_hist, task->fn.fn_name, *hist);

    if (*hist == NULL) { // does not exist
        *hist = calloc(1, sizeof(history_t));
        (*hist)->fn_name = calloc(strlen(task->fn.fn_name)+1, sizeof(char));
        strcpy((*hist)->fn_name, task->fn.fn_name);
        (*hist)->mean_t = 0;
        (*hist)->mean_yield = 0;
        (*hist)->executions = 0;
        (*hist)->completions = 0;
        HASH_ADD_KEYPTR(hh, t->exec_hist, (*hist)->fn_name, strlen((*hist)->fn_name), *hist);
    }

    
    if(task->status == TASK_COMPLETED){
        (*hist)->mean_t     = (((*hist)->mean_t)*((*hist)->completions) + task->cpu_time) / (((*hist)->completions) + 1);
        (*hist)->mean_yield = (((*hist)->mean_yield)*((*hist)->completions) + task->yields) / (((*hist)->completions) + 1);
        (*hist)->completions += 1;
    }
    // (*hist)->executions += 1; done on task creation!
    pthread_mutex_unlock(&(t->hmutex));
}


void history_fetch_exec(tboard_t *t, function_t *func, history_t **hist)
{
    pthread_mutex_lock(&(t->hmutex));
    HASH_FIND_STR(t->exec_hist, func->fn_name, *hist);
    pthread_mutex_unlock(&(t->hmutex));
}

void history_destroy(tboard_t *t)
{
    history_t *entry, *temp;
    pthread_mutex_lock(&(t->hmutex));
    HASH_ITER(hh, t->exec_hist, entry, temp) {
        HASH_DEL(t->exec_hist, entry);
        free(entry->fn_name);
        free(entry);
    }
    pthread_mutex_unlock(&(t->hmutex));
}


void history_save_to_disk(tboard_t *t, FILE *fptr);
/**
 * history_save_to_disk() - Saves task board history to disk
 * 
 * TODO: implement
 * 
 */

void history_load_from_disk(tboard_t *t, FILE *fptr);
/**
 * history_load_from_disk()
 * 
 * TODO: implement
 */

void history_print_records(tboard_t *t, FILE *fptr)
{
    history_t *entry, *temp;
    pthread_mutex_lock(&(t->hmutex));
    HASH_ITER(hh, t->exec_hist, entry, temp) {
        fprintf(fptr, "History: task '%s' completed %d/%d times, yielding %.0f times (average %f) with mean execution CPU time of %.7f s\n", 
            entry->fn_name, entry->completions, entry->executions, entry->yields, entry->mean_yield, entry->mean_t / CLOCKS_PER_SEC);
    }
    pthread_mutex_unlock(&(t->hmutex));
}