#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include "mqtt_types.h"

typedef struct {
    void (*onConnect)(MQTTConnection* connection);
    void (*onDisconnect)(MQTTConnection* connection);
    void (*onSubscribed)(MQTTConnection* connection, const char* topic);
    void (*onUnsubscribed)(MQTTConnection* connection, const char* topic);
    void (*onError)(MQTTConnection* connection, int rc);
    void (*onMessage)(MQTTConnection* connection, const char* topic, const char* payload);
} MQTTHandler;

#endif