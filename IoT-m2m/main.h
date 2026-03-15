#ifndef MAIN_H
#define MAIN_H

#include "include/common.h"
#include "include/cseBase.h"
#include "include/ae.h"
#include "include/container.h"
#include "include/contentInstance.h"
#include "include/subscription.h"
#include <pthread.h>
#include <signal.h>
#include <sys/select.h>
#include <coap3/coap.h> 

#define PORT 8080
#define COAP_PORT 5683

void start_check_and_delete_expired_resources();
void *start_web_server(void *arg);
void *start_coap_server(void *arg);
char *get_json_key_from_request(const char *request);
char *extract_json_key(const char *json);
void handle_request(int client_socket);
void handle_coap_request(coap_resource_t *resource,coap_session_t *session,const coap_pdu_t *request,const coap_string_t *query,coap_pdu_t *response);
void *check_and_delete_expired_resources(void *arg);



#endif
