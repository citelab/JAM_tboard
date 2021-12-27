#include "tests.h"

#ifdef TEST_5

#include "../tboard.h"
#include "../dummy_MQTT.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#define SECONDARY_EXECUTORS 10
#define SMALL_TASK_ITERATIONS 10000
#define MIN_PRIMARY_TASKS 4
#define ISSUE_PRIORITY_TASK 1
#define MAX_CREATION_ATTEMPTS 30
#define MAX_PRIORITY_SLEEP 5 // max number of seconds between priority tasks being issued
#define MAX_MQTT_SLEEP 0.5 // max number of seconds between MQTT messages
#define CONTINUE_CREATING_PRIMARY_TASKS_UNTIL_KILL 1
#define KILLSWITCH_PROBABILITY 50 // P(kill) = 1/KILLSWITCH_PROBABILITY

bool print_tasks_msgs = true;

tboard_t *tboard = NULL;
pthread_t msg_gen, tb_kill, priority_t_gen;

pthread_mutex_t count_mutex;


double yield_count = 0;

int priority_count = 0, priority_tasks = 0;
int primary_count = 0, primary_tasks = 0;
int secondary_count = 0, secondary_tasks = 0;
int msg_count = 0, msg_sent = 0;
int max_tasks_reached = 0;

const struct timespec killswitch_timeout = {
    .tv_sec = 1,
	.tv_nsec = 500000000L,
};

///////////// Helper Functions ////////////

void increment_count(int *count);
int read_count(int *count);
int total_tasks();
int total_count();
void fsleep(float max_second);


///////////// Task Functions ///////////////

void priority_task(void *args);
void primary_task(void *args);
void secondary_task(void *args);


///////////// Thread Functions //////////////

void tboard_killer(void *args);
void generate_MQTT_message(void *args);
void generate_priority_task(void *args);

///////////// Main Function ////////////////

int main(int argc)
{
    if(argc > 1) print_tasks_msgs = false;
    pthread_mutex_init(&count_mutex, NULL);

    // initialize tboard
    tboard = tboard_create(SECONDARY_EXECUTORS);
    tboard_start(tboard);

    // initialize dummy MQTT
    MQTT_init(tboard);

    // initiate main pthreads
    pthread_create(&priority_t_gen, NULL, &generate_priority_task, tboard);
    pthread_create(&msg_gen, NULL, &generate_MQTT_message, tboard);

    // initiate tasks:
    int n[MIN_PRIMARY_TASKS] = {0};
    for (int i=0; i<MIN_PRIMARY_TASKS; i++) {
        n[i] = i;
        task_create(tboard, TBOARD_FUNC(primary_task), 1, &n[i]);
        increment_count(&primary_tasks);
    }

    // create tboard killer pthread
    pthread_create(&tb_kill, NULL, &tboard_killer, tboard);
    
    // destroy tboard
    tboard_destroy(tboard);

    // kill MQTT
    MQTT_kill(&msg_count);

    // join pthreads
    printf("Waiting for priorty\n");
    pthread_join(priority_t_gen, NULL);
    printf("Waiting for msg_gen\n");
    pthread_join(msg_gen, NULL);
    printf("Waiting for tbkill\n");
    pthread_join(tb_kill, NULL);

    

    // destroy MQTT
    MQTT_destroy();

    printf("========================= Results ===========================\n");
    printf("Completed %d/%d total tasks with %.0f yields:\n", total_count(), total_tasks(), yield_count);
    printf("\t%d/%d priority tasks\n",priority_count, priority_tasks);
    printf("\t%d/%d primary tasks\n",primary_count, primary_tasks);
    printf("\t%d/%d secondary tasks\n",secondary_count, secondary_tasks);
    printf("\t%d/%d MQTT tasks\n",msg_count, msg_sent);
    printf("\tMax tasks reached %d times for local tasks.\n",max_tasks_reached);
    printf("====================================================================\n");


    // exit tboard
    tboard_exit();
}

///////////// Thread Implementation ///////////////

void tboard_killer(void *args)
{
    tboard_t *t = (tboard_t *)args;
    while (true) {
        nanosleep(&killswitch_timeout, NULL);
        if (rand() % KILLSWITCH_PROBABILITY == 0) {
            if (print_tasks_msgs)
                tboard_log("tboard_killer: random number hit, killing task board.\n");

            // lock tboard->tmutex in order to capture tboard data before collected and destroyed
            pthread_mutex_lock(&(t->tmutex));
            // cancel other threads
            pthread_cancel(priority_t_gen);
            pthread_cancel(msg_gen);
            // kill tboard
            tboard_kill(tboard);
            // print history records to show manipulation can be done before exiting
            history_print_records(t, stdout);
            // unlock tboard->tmutex, allowing tboard_destroy() to continue and return
            pthread_mutex_unlock(&(t->tmutex));
            // break out of loop, effectively terminating tb_kill thread
            break;
        } else if (CONTINUE_CREATING_PRIMARY_TASKS_UNTIL_KILL == 1) {
            // add another primary task if we are out already
            if (read_count(&secondary_count) == read_count(&secondary_tasks) && read_count(&priority_count) == read_count(&priority_tasks)) {
                int n = read_count(&primary_tasks);
                task_create(tboard, TBOARD_FUNC(primary_task), 1, &n);
                increment_count(&primary_tasks);
            }
        }
    }
}
void generate_MQTT_message(void *args)
{
    char *commands[] = {"print", "math", "spawn"};
    char operator[] = "+-/*";
    while (true) {
        int cmd = rand() % 3;
        switch (cmd) {
            case 0:
                MQTT_send("print 'hello world!'\n");
                break;
            case 1:
                ;
                int opn = rand() % 4;
                char op = operator[opn];
                char message[50] = {0};
                sprintf(message, "math %d %c %d",rand()%500, op, rand()%500);
                MQTT_send(message);
                break;
            case 2:
                MQTT_send("spawn");
                break;
        }
        increment_count(&msg_sent);
        fsleep(MAX_MQTT_SLEEP);
    }
}


void generate_priority_task(void *args)
{
    if (ISSUE_PRIORITY_TASK == 0)
        return;
    
    while(true){
        fsleep(MAX_PRIORITY_SLEEP);
        int task_num = read_count(&priority_tasks);
        if (print_tasks_msgs)
            tboard_log("priority_gen: issued priority task %d at CPU time %d\n",task_num ,clock());
        bool res = task_create(tboard, TBOARD_FUNC(priority_task), PRIORITY_EXEC, &task_num);
        if (res)
            increment_count(&priority_tasks);
        else
            tboard_err("priority_gen: unable to create task (max concurrent tasks reached), trying again later.\n");
    }
}

///////////// Task Implementation /////////////////
void priority_task(void *args)
{
    int pcount = *((int *)task_get_args());
    if(print_tasks_msgs)
        tboard_log("priority: priority task %d executed at CPU time %ld.\n", pcount, clock());

    increment_count(&priority_count);
}

void primary_task(void *args)
{
    int i = 0;
    int num = *((int *)task_get_args());
    if (print_tasks_msgs)
        tboard_log("primary #%d: Creating %d many different tasks to task 3x+1.\n", num, SMALL_TASK_ITERATIONS);
    int actually_gend = 0;
    bool gen_failed = false;
    for (; i<SMALL_TASK_ITERATIONS; i++) {
        int attempt_count = 0;
        int iarg = i;
        while (false == task_create(tboard, TBOARD_FUNC(secondary_task), SECONDARY_EXEC, &iarg)) {
            if (attempt_count > MAX_CREATION_ATTEMPTS) {
                tboard_err("primary: Unable to create task after %d attempts.\n", MAX_CREATION_ATTEMPTS);
                increment_count(&primary_count);
                gen_failed = true;
            }
            fsleep(1.0 / 10000);
            increment_count(&max_tasks_reached);
            task_yield(); yield_count++;
            attempt_count++;
        }
        if (gen_failed) break;
        actually_gend++;
        increment_count(&secondary_tasks);
        task_yield(); yield_count++;
    }
    if (print_tasks_msgs)
        tboard_log("primary #%d: Created %d/%d secondary tasks.\n", num, actually_gend, SMALL_TASK_ITERATIONS);
    
    increment_count(&primary_count);
}

void secondary_task(void *args)
{
    int x = *((int *)(task_get_args()));
    int i = 0;
    if (x <= 1) {
        if (x >= 0) increment_count(&secondary_count);
        else        tboard_err("secondary: Invalid value of x encountered in secondary task: %d\n", x);
        return;
    }
    while (x != 1) {
        if(x % 2 == 0) x /= 2;
        else           x = 3*x + 1;
        i++;
        task_yield(); yield_count++;
    }

    increment_count(&secondary_count);
}

/////////////////////// Utility Function Implementation ///////////////////
void increment_count(int *count)
{
	pthread_mutex_lock(&count_mutex);
	(*count)++;
	pthread_mutex_unlock(&count_mutex);
}

int read_count(int *count)
{
	pthread_mutex_lock(&count_mutex);
	int ret = *count;
	pthread_mutex_unlock(&count_mutex);
    return ret;
}

int total_tasks()
{
    pthread_mutex_lock(&count_mutex);
    int ret = priority_tasks + primary_tasks + secondary_tasks + msg_sent;
    pthread_mutex_unlock(&count_mutex);
    return ret;
}

int total_count()
{
    pthread_mutex_lock(&count_mutex);
    int ret = priority_count + primary_count + secondary_count + msg_count;
    pthread_mutex_unlock(&count_mutex);
    return ret;
}

void fsleep(float max_second)
{
    float seconds = (float)rand() / (float)(RAND_MAX/max_second);
    int s = (int)seconds;
    long ns = (long)(1000000000 * (seconds - s));
    struct timespec ts = {
        .tv_sec = s,
        .tv_nsec = ns,
    };
    nanosleep(&ts, NULL);
}

#endif