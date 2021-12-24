#include "tboard.h"
#include "queue/queue.h"


// the following function removes a specific queue entry recursively and returns it
struct queue_entry *remove_queue_entry_by_id(struct queue *q, int id)
{
    struct queue_entry *head = queue_pop_head(q); // pop head
    if (head == NULL) { // if NULL, then we reached end of queue without finding it
        return NULL;
    }
    if (((task_t *)(head->data))->id == id) { // if head matches id, return it without inserting it back
        return head;
    } else { // if head does not match id
        struct queue_entry *ret = remove_queue_entry_by_id(q, id); // recursively run function to get matching entry
        queue_insert_head(q, head); // reinsert non-matching head back into queue at the head
        return ret; // return matching entry
    }
}

// the following function removes a specific queue entry recursively and returns it
struct queue_entry *remove_queue_entry_by_type(struct queue *q, int type)
{
    struct queue_entry *head = queue_pop_head(q); // pop head
    if (head == NULL) { // if NULL, then we reached end of queue without finding it
        return NULL;
    }
    if (((task_t *)(head->data))->type == type) { // if head matches id, return it without inserting it back
        return head;
    } else { // if head does not match id
        struct queue_entry *ret = remove_queue_entry_by_type(q, type); // recursively run function to get matching entry
        queue_insert_head(q, head); // reinsert non-matching head back into queue at the head
        return ret; // return matching entry
    }
}







void task_sequencer(tboard_t *tboard)
{
    (void)tboard; // we have not implemented this yet
}