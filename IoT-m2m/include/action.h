/*
 * File:    action.h
*/
#ifndef IOTM2M_ACTIONS_H
#define IOTM2M_ACTIONS_H

#include "common.h"
#include "response_params.h"
#include <sqlite3.h>

#define ACTION_RESOURCE_TYPE 65
#define MAX_DEPENDENCIES 10

typedef enum
{
    EVAL_MODE_OFF = 0,
    EVAL_MODE_ONCE=1,
    EVAL_MODE_PERIODIC=2,
    EVAL_MODE_CONTINUOUS=3
}EvalMode;
typedef enum {
    EVAL_OP_EQUALS        = 0,
    EVAL_OP_NOT_EQUALS    = 1,
    EVAL_OP_GREATER_THAN  = 2,
    EVAL_OP_LESS_THAN     = 3,
    EVAL_OP_GREATER_EQUAL = 4,
    EVAL_OP_LESS_EQUAL    = 5,
    EVAL_OP_STRING_MATCH  = 6
} EvalOperator;

typedef struct {
    char        subject[128];
    EvalOperator op;           /* int no SQLite, enum em memória */
    char        threshold[256];
} EvalCriteria;

typedef struct {
    /* Atributos comuns */
    char ri[64]; // Resource ID
    char rn[64]; // Resource Name
    char pi[64]; // Parent ID
    char ct[32]; // Creation Time
    char lt[32]; // LastModifiedTime
    char et[32]; // expirationTime

    /* Atributos específicos */
    int          action_priority;           /* -1 = não configurado */
    char         subject_resource_id[256];  /* vazio = usa recurso pai */
    EvalCriteria eval_criteria;
    int          eval_mode;                 /* EvalMode */
    int          eval_control_param;        /* 0 = não configurado */
    char         dependencies[MAX_DEPENDENCIES][64]; /* RIs dos <dependency> filhos */
    int          dependency_count;
    char         object_resource_id[256];
    char         action_primitive[1024];
    char         input[512];
    char         action_result[1024];       /* RO — preenchido pelo Hosting CSE */
} Action;

/* POST /{csebase}/{ae}/{container}
 * Content-Type: application/json;ty=65
 * Body exemplo:
 * {
 *     "m2m:act": {
 *         "rn": "action1",
 *         "sri": "/mn-name/ae_sensor/cont_temp",
 *         "evc": { "sus": "con", "optr": 2, "thr": "30" },
 *         "evm": 3,
 *         "ori": "/mn-name/ae_sensor/cont_actuator",
 *         "acp": "CREATE",
 *         "inp": "ON"
 *     }
 * }
 * Resposta: 201 Created */
void handle_request_action_post(struct response_params *params,
                                const char *csebase,
                                const char *ae,
                                const char *container,
                                const char *body);

/* GET /{csebase}/{ae}/{container}/{action_name}?ty=65
 * Resposta: 200 OK com atributos do <action>
 * Devolve char* com JSON alocado — caller liberta */
char *handle_request_action_get(struct response_params *params,
                                const char *csebase,
                                const char *ae,
                                const char *container,
                                const char *action_name);

/* PUT /{csebase}/{ae}/{container}/{action_name}
 * Apenas atributos RW podem ser atualizados:
 * evc, evm, evcp, ori, acp, inp, acp, sri
 * Resposta: 200 OK com atributos atualizados */
void handle_request_action_put(struct response_params *params,
                               const char *csebase,
                               const char *ae,
                               const char *container,
                               const char *action_name,
                               const char *body);

/* DELETE /{csebase}/{ae}/{container}/{action_name}
 * Para a thread de monitorização associada.
 * Resposta: 200 OK */
void handle_request_action_delete(struct response_params *params,
                                  const char *csebase,
                                  const char *ae,
                                  const char *container,
                                  const char *action_name);

bool handle_action_request(const char *request_json,
                           const char *resource_uri,
                           const char *originator,
                           char *response,
                           size_t response_size,
                           int client_socket);

bool handle_get_action(int client_socket,
                       const char *resource_name,
                       const char *resource_uri);

bool handle_delete_action(int client_socket,
                          const char *resource_name);

/* Schedule -> Action bridge. Strong override of the weak stub in schedule.c:
 * the scheduler thread calls this when a <schedule> matches the current minute,
 * and it runs the actions living in that schedule's AE (see action.c). */
void check_and_trigger_actions(void);

#endif //IOTM2M_ACTIONS_H