#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <mosquitto.h>
#include <pthread.h>
#include "mqtt_adapter.h"

void mqtt_message_callback(struct mosquitto *mosq, void *udata, const struct mosquitto_message *msg) 
{
    // TODO: hookup the task board here
    if (msg->payloadlen) {
        printf("%s %s\n", msg->topic, (char *)msg->payload);
    }else{
        printf("%s\n", msg->topic);
    }
}

void mqtt_connect_callback(struct mosquitto *mosq, void *udata, int res) 
{
    struct mqtt_adapter_t *ma = (struct mqtt_adapter_t *)udata;
    if (!res) 
        mqtt_do_subscribe(ma);
    else
        fprintf(stderr, "Connect failed on interface: %s\n", ma->desc);
}

void mqtt_subscribe_callback(struct mosquitto *mosq, void *udata, int mid, int qcnt, const int *qgv) 
{
    for(int i = 1; i < qcnt; i++)
        printf("QoS-given: %d\n", qgv[i]);
}

void mqtt_log_callback(struct mosquitto *mosq, void *udata, int level, const char *str) 
{
    //printf("%s\n", str);
}

void mqtt_publish_callback(struct mosquitto *mosq, void *udata, int mid) 
{
    struct mqtt_adapter_t *ma = (struct mqtt_adapter_t *)udata;
    struct pub_msg_entry_t *p = NULL;
    HASH_FIND_INT(ma->pmsgs, &mid, p);
    if (p != NULL) {
        pthread_mutex_lock(&(ma->hlock));
        HASH_DEL(ma->pmsgs, p);
        pthread_mutex_unlock(&(ma->hlock));           
	//        free(p->ptr);
	        free(p);
    }
}

struct pub_msg_entry_t *create_pub_msg_entry(int id, void *msg) 
{
    struct pub_msg_entry_t *p = (struct pub_msg_entry_t *)calloc(1, sizeof(struct pub_msg_entry_t));
    assert(p != NULL);

    p->id = id;
    p->ptr = msg;
    return p;
}

void mqtt_do_subscribe(struct mqtt_adapter_t *ma) 
{
    char **p;
    p = NULL;
    while ((p = (char**)utarray_next(ma->topics, p))) 
        mosquitto_subscribe(ma->mosq, NULL, *p, 0);
}

struct mqtt_adapter_t *create_mqtt_adapter(char *desc)
{
    struct mqtt_adapter_t *ma = (struct mqtt_adapter_t *)calloc(1, sizeof(struct mqtt_adapter_t));
    struct mosquitto *mosq;
    mosq = mosquitto_new(NULL, true, ma);
    ma->mosq = mosq;

    if (!mosq) 
        mosquitto_lib_cleanup();
    assert (mosq != NULL);
    ma->pmsgs = NULL; 
    ma->mid = 0;
    utarray_new(ma->topics, &ut_str_icd);
    pthread_mutex_init(&(ma->hlock), NULL);

    return ma;
}

bool connect_mqtt_adapter(struct mqtt_adapter_t *ma, struct broker_info_t *bi)
{
    if (mosquitto_connect(ma->mosq, bi->host, bi->port, bi->keep_alive)) {
        fprintf(stderr, "Connection error: %s\n", ma->desc);
        return false;
    }
    mosquitto_loop_start(ma->mosq);
    return true;
}

void destroy_mqtt_adapter(struct mqtt_adapter_t *ma) 
{
    mosquitto_destroy(ma->mosq);
    utarray_free(ma->topics);
    free(ma);
    mosquitto_lib_cleanup();
}

void disconnect_mqtt_adapter(struct mqtt_adapter_t *ma) 
{
    mosquitto_disconnect(ma->mosq);
}

void mqtt_publish(struct mqtt_adapter_t *ma, char *topic, void *msg, int msglen, int qos)
{
    ma->mid++;
    mosquitto_publish(ma->mosq, &(ma->mid), topic, msglen, msg, qos, 0);
    struct pub_msg_entry_t *pentry = create_pub_msg_entry(ma->mid, msg);
    pthread_mutex_lock(&(ma->hlock));
    HASH_ADD_INT(ma->pmsgs, id, pentry);
    pthread_mutex_unlock(&(ma->hlock));    
}

void mqtt_post_subscription(struct mqtt_adapter_t *ma, char *topic) 
{
    utarray_push_back(ma->topics, &topic);
}

