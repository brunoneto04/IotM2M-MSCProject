/*
AUTHORSHIP -------------------------------------------------

 * File:    mqtt_client.c
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
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static MQTTConnection *g_mqtt_connection = NULL;

MQTTConnection *get_mqtt_connection(void)
{
    return g_mqtt_connection;
}

void set_mqtt_connection(MQTTConnection *conn)
{
    g_mqtt_connection = conn;
}

bool mqtt_send_notification2(MQTTConnection *conn,const char *subscriber_id,const char *content)
{
    if (!conn || !subscriber_id || !content)
    {
        return false;
    }

    char topic[512];
    snprintf(topic, sizeof(topic), "/oneM2M/resp/%s/%s/json",
             conn->client_id,
             subscriber_id);

    return mqtt_publish2(conn, topic, content);
}

bool mqtt_publish2(MQTTConnection *conn, const char *topic, const char *payload)
{
    if (!conn || !conn->is_connected)
    {
        printf("[ERROR] Invalid MQTT connection\n");
        return false;
    }

    printf(" Publishing to topic: %s\n", topic);
    printf("Payload: %s\n", payload);

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = strlen(payload);
    pubmsg.qos = 1; // QoS 1
    pubmsg.retained = 0;

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(conn->client, topic, &pubmsg, &token);

    if (rc != MQTTCLIENT_SUCCESS)
    {
        printf("[ERROR] Publish failed: %s (code: %d)\n", MQTTClient_strerror(rc), rc);
        return false;
    }

    // Wait for delivery confirmation
    rc = MQTTClient_waitForCompletion(conn->client, token, 5000); // 5 seconds
    if (rc != MQTTCLIENT_SUCCESS)
    {
        printf("[ERROR] Delivery failed: %s (code: %d)\n", MQTTClient_strerror(rc), rc);
        return false;
    }

    printf("[OK] Message published successfully to topic: %s\n", topic);
    return true;
}

MQTTConnection *mqtt_create_dynamic_connection(const char *address, int port, bool use_tls)
{
    MQTTConnection *conn = calloc(1, sizeof(MQTTConnection));
    if (!conn)
    {
        printf("[ERROR] Failed to allocate memory for MQTT connection\n");
        return NULL;
    }

    // Construir URI
    char uri[256];
    snprintf(uri, sizeof(uri), "%s://%s:%d",
            use_tls ? "ssl" : "tcp", address, port);

    printf("  Creating MQTT client with URI: %s\n", uri);

    // Criar cliente
    int rc = MQTTClient_create(&conn->client, uri, "iotm2m-notifier",
                    MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("[ERROR] Failed to create MQTT client: %s (code: %d)\n", MQTTClient_strerror(rc), rc);
        free(conn);
        return NULL;
    }

    // Initialise connection struct
    conn->conn_opts = (MQTTClient_connectOptions)MQTTClient_connectOptions_initializer;
    
    // Basic settings
    conn->conn_opts.keepAliveInterval = 60;
    conn->conn_opts.cleansession = 1;
    conn->conn_opts.connectTimeout = 30;

    // Configure TLS if needed
    if (use_tls)
    {
        
        // Inicializar SSL options corretamente
        static MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
        
        ssl_opts.trustStore = "certs/ca.crt";
        ssl_opts.keyStore = "certs/client.crt";
        ssl_opts.privateKey = "certs/client.key";
        ssl_opts.enableServerCertAuth = 1;
        ssl_opts.sslVersion = MQTT_SSL_VERSION_TLS_1_2;
        ssl_opts.verify = 0;
        
        conn->conn_opts.ssl = &ssl_opts;
        memcpy(&conn->ssl_opts, &ssl_opts, sizeof(MQTTClient_SSLOptions));
    }
    else
    {
        conn->conn_opts.ssl = NULL;
    }

    conn->is_connected = false;
    strncpy(conn->client_id, "iotm2m-notifier", sizeof(conn->client_id) - 1);

    return conn;
}

static void mqtt_connection_lost(void *context, char *cause)
{
    MQTTConnection *conn = (MQTTConnection *)context;
    if (conn && conn->callbacks.on_disconnect)
    {
        conn->callbacks.on_disconnect(conn->user_context);
    }
}

static int mqtt_message_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    MQTTConnection *conn = (MQTTConnection *)context;
    if (conn && conn->callbacks.on_message)
    {
        char *payload = message->payload;
        conn->callbacks.on_message(conn->user_context, topicName, payload);
    }
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}


bool mqtt_connect2(MQTTConnection *conn)
{
    if (!conn || !conn->client)
    {
        printf("[ERROR] Invalid MQTT connection\n");
        return false;
    }

    printf("Connecting to MQTT broker...\n");

    int rc = MQTTClient_connect(conn->client, &conn->conn_opts);
    
    if (rc != MQTTCLIENT_SUCCESS)
    {
        printf("[ERROR] MQTT connection failed: %s (code: %d)\n", MQTTClient_strerror(rc), rc);
        
        // Additional diagnostics
        switch(rc)
        {
            case MQTTCLIENT_FAILURE:
                printf("[ERROR] General connection failure\n");
                break;
            case MQTTCLIENT_DISCONNECTED:
                printf("[ERROR] Client disconnected\n");
                break;
            case MQTTCLIENT_MAX_MESSAGES_INFLIGHT:
                printf("[ERROR] Too many in-flight messages\n");
                break;
            case MQTTCLIENT_BAD_UTF8_STRING:
                printf("[ERROR] Invalid UTF-8 string\n");
                break;
            case MQTTCLIENT_NULL_PARAMETER:
                printf("[ERROR] Null parameter\n");
                break;
            case MQTTCLIENT_TOPICNAME_TRUNCATED:
                printf("[ERROR] Topic name error\n");
                break;
            case MQTTCLIENT_BAD_STRUCTURE:
                printf("[ERROR] Invalid structure\n");
                break;
            case MQTTCLIENT_BAD_QOS:
                printf("[ERROR] Invalid QoS\n");
                break;
            case MQTTCLIENT_SSL_NOT_SUPPORTED:
                printf("[ERROR] SSL not supported\n");
                break;
            default:
                printf("[ERROR] Unknown error code: %d\n", rc);
                break;
        }
        
        conn->is_connected = false;
        return false;
    }

    conn->is_connected = true;
    printf("[OK] Connected to MQTT broker\n");
    return true;
}

void mqtt_cleanup2(MQTTConnection *conn)
{
    if (!conn)
        return;

    if (conn->client && conn->is_connected)
    {
        printf("Disconnecting from MQTT broker...\n");
        MQTTClient_disconnect(conn->client, 1000);
    }

    if (conn->client)
    {
        MQTTClient_destroy(&conn->client);
    }

    // Free SSL strings if dynamically allocated
    if (conn->conn_opts.ssl)
    {
     
    }

    free(conn);
    printf("MQTT connection cleaned up\n");
}


