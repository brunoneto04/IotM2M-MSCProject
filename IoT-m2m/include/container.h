#ifndef CONTAINER_H
#define CONTAINER_H

#include <json-c/json.h>
#include "../include/ae.h"
#include "../include/contentInstance.h"
#include <limits.h>
#include "response_params.h"

#define MBS_DEFAULT 60000000
#define MIA_DEFAULT 1600
#define MNI_DEFAULT 10000

// Function prototype for handling container requests
char *handle_request_container_get(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request_type);
void handle_request_container_post(struct response_params *params, char *csebase_name, char *ae_name, char *request, char *body);
void handle_request_container_put(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request, char *body);
void handle_request_container_delete(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request_type, sqlite3 *db);

#endif
