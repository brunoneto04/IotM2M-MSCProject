#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "mqtt_types.h"
#include <stdbool.h>

// Functions required for sending MQTT notifications
MQTTConnection *mqtt_create_dynamic_connection(const char *address, int port, bool use_tls);

bool mqtt_publish2(MQTTConnection* conn, const char* topic, const char* payload);

bool mqtt_send_notification2(MQTTConnection* conn, const char* subscriber_id,const char* content);

MQTTConnection* get_mqtt_connection(void);

void set_mqtt_connection(MQTTConnection* conn);

bool mqtt_connect2(MQTTConnection *conn);

void mqtt_cleanup2(MQTTConnection *conn);

#endif