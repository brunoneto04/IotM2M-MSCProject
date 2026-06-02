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
#ifdef ENABLE_COAP
#include <coap3/coap.h>
#endif

#define PORT 8080
#define COAP_PORT 5683

void handle_shutdown(int signal);
void *check_and_delete_expired_resources(void *arg);
char *get_json_key_from_request(const char *request);
char *extract_json_key(const char *json);

#ifdef ENABLE_HTTP
void *start_web_server(void *arg);
void handle_request(int client_socket);
#endif

#ifdef ENABLE_COAP
void *start_coap_server(void *arg);
void handle_coap_request(coap_resource_t *resource, coap_session_t *session, const coap_pdu_t *request, const coap_string_t *query, coap_pdu_t *response);
#endif

#endif
