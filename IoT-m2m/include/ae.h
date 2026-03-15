#ifndef AE_H
#define AE_H

#include <json-c/json.h>
#include "../include/cseBase.h"
#include "../include/container.h"
#include "response_params.h"

// Function prototype for handling ae requests
char *handle_request_ae_get(struct response_params *params, char *csebase_name, char *ae_name, char *request_type);
void handle_request_ae_post(struct response_params *params, char *csebase_name, char *request, char *body);
void handle_request_ae_put(struct response_params *params, char *csebase_name, char *ae_name, char *request, char *body);
void handle_request_ae_delete(struct response_params *params, char *csebase_name, char *ae_name, char *request_type, sqlite3 *db);

#endif
