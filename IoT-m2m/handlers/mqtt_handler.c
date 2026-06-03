/*
AUTHORSHIP -------------------------------------------------

 * File:    mqtt_handler.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions necessary for the MQTT client to work.

COMMANDS TO RUN THE PROGRAM ----------------------------------------

Directory: /home/ubuntu/Desktop/iotm2m

gcc -o iotm2m main.c handlers/cseBase.c handlers/ae.c handlers/container.c handlers/contentInstance.c handlers/subscription.c handlers/common.c -I include -l sqlite3 -l json-c

./iotm2m
valgrind ./iotm2m
valgrind --leak-check=yes ./iotm2m
*/
#include "mqtt_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"

static void handle_request(const char* topic, const char* payload) {
    // Implement oneM2M request logic
    LOG("Received request on topic: %s", topic);
    LOG("Payload: %s", payload);
}

static void handle_response(const char* topic, const char* payload) {
    // Implement oneM2M response logic
    LOG("Received response on topic: %s", topic);
    LOG("Payload: %s", payload);
}

static int mqtt_message_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    MQTTConnection* conn = (MQTTConnection*)context;
    
    char* payload = (char*)message->payload;
    char* topic = malloc(topicLen + 1);
    strncpy(topic, topicName, topicLen);
    topic[topicLen] = '\0';

    // Check oneM2M message type
    if (strstr(topic, MQTT_REQ_TOPIC) != NULL) {
        handle_request(topic, payload);
    } else if (strstr(topic, MQTT_RESP_TOPIC) != NULL) {
        handle_response(topic, payload);
    }

    free(topic);
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);

    return 1;
}