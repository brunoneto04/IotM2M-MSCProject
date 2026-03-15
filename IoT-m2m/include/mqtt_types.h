#ifndef MQTT_TYPES_H
#define MQTT_TYPES_H

#include <MQTTClient.h>
#include <stdbool.h>

#define MQTT_TOPIC_PREFIX "/oneM2M"
#define MQTT_REQ_TOPIC "/req"
#define MQTT_RESP_TOPIC "/resp"
#define MQTT_REG_TOPIC "/reg_req"
#define MAX_CREDENTIALS 5
#define INPUT_BUFFER_SIZE 256

#define ONEM2M_TOPIC_TEMPLATE "/oneM2M/%s/%s/%s/%s"  // prefix/type/originator/receiver
#define ONEM2M_REQUEST "req"
#define ONEM2M_RESPONSE "resp"
#define ONEM2M_REG_REQUEST "reg_req"
#define ONEM2M_REG_RESPONSE "reg_resp"
#define ONEM2M_PAYLOAD_TYPE "json"

#define MQTT_PROTOCOL_VERSION 4  // MQTT 3.1.1
#define MQTT_QOS_LEVEL 1        // QoS 1 as required by oneM2M
#define SSL_VERSION_STR "tlsv1.2"

#define MQTT_TOPIC_FORMAT "/oneM2M/%s/%s/%s"  // prefix/type/originator/receiver

// Tipos de mensagens
#define MQTT_MSG_TYPE_REQ "req"
#define MQTT_MSG_TYPE_RESP "resp"
#define MQTT_MSG_TYPE_REG "reg"

typedef enum {
    INPUT_TYPE_STRING,
    INPUT_TYPE_NUMBER,
    INPUT_TYPE_PASSWORD
} InputType;

typedef struct {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts;
    MQTTClient_SSLOptions ssl_opts;
    char client_id[128];
    bool is_connected;
    void* user_context;
    struct {
        void (*on_connect)(void*);
        void (*on_message)(void*, const char*, const char*);
        void (*on_disconnect)(void*);
        void (*on_error)(void*, int);
    } callbacks;
} MQTTConnection;

typedef struct {
    //Configurações do Broker
    char address[256];
    bool use_mqtt; 
    int port;
    int keepalive;
    int timeout;
    bool enable;
    
    //Configurações de Tópicos
    char topic_prefix[128];
    char request_topic[256];
    char response_topic[256];
    char registration_topic[256];
    
    // Segurança
    bool use_tls;
    bool verify_cert;
    char ca_file[256];
    char cert_file[256];
    char key_file[256];
    char username[64];
    char password[64];
    char allowed_credentials[MAX_CREDENTIALS][64];
    int allowed_cred_count;
    
    //Identificação do Cliente
    char client_id[128];
    char entity_type;       // 'A' for AE, 'C' for CSE
    char entity_id[256];    // AE-ID or CSE-ID
    int protocol_version;   // MQTT protocol version (should be 4 for 3.1.1)
} MQTTConfig;

// Estrutura para mensagem oneM2M
typedef struct {
    char op[32];     // Operação (CREATE, RETRIEVE, etc)
    char fr[256];    // From (Originator)
    char to[256];    // To (Receiver)
    char rqi[64];    // Request Identifier
    char ty[32];     // Resource Type
    char* pc;        // Payload Content
} OneM2MMessage;

#endif