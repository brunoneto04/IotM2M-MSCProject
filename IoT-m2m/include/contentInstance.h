#ifndef CONTENTINSTANCE_H
#define CONTENTINSTANCE_H

#include "../include/container.h"
#include "response_params.h"

// Function prototype for handling contentInstance requests
char *handle_request_cin_get(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *cin_name, char *request_type);
void handle_request_cin_post(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request, char *body);
void handle_request_cin_delete(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *cin_name, char *request_type, sqlite3 *db);

#endif
