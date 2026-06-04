#ifndef SUBSCRIPTION_H
#define SUBSCRIPTION_H

#include "mqtt_types.h"
#include <json-c/json.h>
#include <stdbool.h>
#include <time.h>
#include <sqlite3.h> 

typedef struct {
    char resource_name[256];      // rn
    char resource_uri[256];       // sur (subscription reference)
    char resource_id[512];        // ri (increased size to avoid truncation)
    char parent_id[256];          // pi
    char notification_uris[512];  // nu (can have multiple URIs)
    char content_type[64];        // cnf
    int notification_type;        // nct
    int event_type;              // net
    char originator[256];        // cr
    char creation_time[32];      // ct
    char last_modified_time[32]; // lt
    int resource_type;           // ty (23 for subscription)
    char event_type_str[256]; // String representation of event types
} Subscription;

// MQTT URI information
typedef struct {
    char host[256];
    int port;
    char topic[512];
    bool use_tls;
    bool is_full_uri;  // true se formato completo, false se formato simples
} MQTTURIInfo;


// Function declarations
bool handle_subscription_request(const char *request_json, const char *resource_uri, const char *originator, char *response, size_t response_size, int client_socket);

bool create_subscription(const Subscription* sub);
bool delete_subscription(const char* resource_name);

void handle_mqtt_notification(const char *resource_uri, const char *content, int event_type);

static json_object* extract_content_from_json(const char* content);

bool handle_get_subscription(int client_socket, const char *resource_name, const char *resource_uri);

bool handle_delete_subscription(int client_socket, const char *resource_name);

static void extract_subscriber_id(const char* uri, char* buffer, size_t buffer_size, bool* use_tls);

static bool is_direct_child(const char *child_uri, const char *parent_uri);

static bool should_notify_for_event(const char *subscription_event_types, int actual_event_type, const char *resource_uri, const char *subscription_uri);

static void normalize_uri(const char *uri, char *normalized, size_t size); 

static bool parse_mqtt_uri_enhanced(const char *uri, MQTTURIInfo *info);

static void extract_subscriber_id_enhanced(const char *uri, char *buffer, size_t buffer_size, bool *use_tls, MQTTURIInfo *uri_info);

static bool is_valid_nct_net_combination(int nct, int net);

static void extract_subscriber_id_enhanced(const char *uri, char *buffer, size_t buffer_size, bool *use_tls, MQTTURIInfo *uri_info);

static bool parse_mqtt_uri(const char *uri, MQTTURIInfo *info);

static void generate_onem2m_timestamp(char* buffer, size_t size);

static char* generate_request_id();

static int calculate_content_size(const char *content);

static json_object* get_resource_data_from_db(const char *resource_uri, int event_type, sqlite3 *db);

static char* get_ri_from_resource_uri(const char *resource_uri, sqlite3 *db);

bool send_http_notification(const char *url, const char *json_payload);

void* send_http_notification_thread(void *arg);

static void get_ri_from_resource_uri_aux(const char *resource_uri, sqlite3 *db, char *out_ri, size_t out_ri_size);

#endif