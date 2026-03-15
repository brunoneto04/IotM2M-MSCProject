#ifndef CSEBASE_H
#define CSEBASE_H

#include "common.h"
#include "response_params.h"
#include <json-c/json.h>
#include <stdbool.h>

#define DEFAULT_CST 1
#define DEFAULT_RN "mn-name"
#define DEFAULT_LBL "middle_node"
#define DEFAULT_POA "http://192.168.0.1:8080"

void handle_request_csebase_create();
char *handle_request_csebase_get(struct response_params *params, char *csebase_name, char *request_type);
int delete_csebases(sqlite3 *db);
bool csebase_exists(sqlite3 *db);

void add_containers_to_ae(sqlite3 *db, json_object *json_array, const char *csebase_rn, const char *ae_ri, const char *ae_rn);
void add_aes_to_csebase(sqlite3 *db, json_object *json_array, const char *csebase_rn, const char *csebase_ri, const char *app_name);
void get_resource_names_json(struct response_params *params, const char *csebase_name, const char *app_name);

void parse_input(const char *input, char *output[], int *count);

#endif
