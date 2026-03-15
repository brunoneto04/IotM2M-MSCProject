/*
AUTHORSHIP -------------------------------------------------

 * File:    subscription.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions necessary for the Subscription to work.

COMMANDS TO RUN THE PROGRAM ----------------------------------------

Directory: /home/ubuntu/Desktop/iotm2m

gcc -o iotm2m main.c handlers/cseBase.c handlers/ae.c handlers/container.c handlers/contentInstance.c handlers/subscription.c handlers/common.c -I include -l sqlite3 -l json-c

./iotm2m
valgrind ./iotm2m
valgrind --leak-check=yes ./iotm2m
*/
#include "subscription.h"
#include "mqtt_client.h"

#include <sys/time.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include <time.h>

#include <stdbool.h>
#include <curl/curl.h>

#include <pthread.h>

typedef struct {
    char *url;
    char *payload;
} http_args_t;

//variáveis estáticas para armazenar o conteúdo e URI da última notificação, evitando duplicações de notificações
static char last_notification_content[1024] = "";
static char last_notification_uri[512] = "";
static time_t last_notification_time = 0;

static void extract_root_cse_uri(const char *uri, char *root_uri, size_t size)
{
    const char *start = uri;

    // Ignora barras iniciais
    while (*start == '/' || *start == '\\')
        start++;

    // Encontra a primeira barra ou fim da string
    const char *end = strpbrk(start, "/\\");

    size_t len = end ? (end - start) : strlen(start);
    strncpy(root_uri, start, len);
    root_uri[len] = '\0';
}

bool get_cse_id_from_parent(const char *parent_uri, char *cse_id, size_t size)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    bool found = false;
    char root_rn[256] = {0};

    extract_root_cse_uri(parent_uri, root_rn, sizeof(root_rn));//extrai o nome do recurso (RN) do URI do CSE Base

    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK)
        return false;

    //Procura o CSI da CSE Base usando o RN (resource name)
    const char *sql =
        "SELECT c.csi FROM cse_bases c "
        "INNER JOIN resources r ON c.ri = r.ri "
        "WHERE r.rn = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
    {
        fprintf(stderr, "Erro SQL: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, root_rn, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *csi = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(cse_id, csi, size - 1);
        found = true;
    }
    else
    {
        fprintf(stderr, "CSE não encontrada para RN: %s\n", root_rn);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

bool create_subscription(const Subscription *sub)
{
    if (!sub)
        return false;

    sqlite3 *db;
    sqlite3_stmt *stmt;

    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
        return false;

        const char *create_table =
        "CREATE TABLE IF NOT EXISTS subscriptions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "ri TEXT NOT NULL UNIQUE,"
        "resource_name TEXT NOT NULL UNIQUE,"
        "resource_uri TEXT NOT NULL,"
        "notification_uris TEXT NOT NULL,"
        "notification_type INTEGER,"
        "event_type TEXT,"
        "content_type TEXT,"
        "originator TEXT,"
        "creation_time TEXT,"
        "last_modified_time TEXT"
        ");";

    rc = sqlite3_exec(db, create_table, 0, 0, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Erro ao criar tabela: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    const char *sql =
    "INSERT INTO subscriptions ("
    "ri, resource_name, resource_uri, notification_uris, notification_type, "
    "event_type, content_type, originator, creation_time, last_modified_time"
    ") VALUES (?,?,?,?,?,?,?,?,?,?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, sub->resource_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, sub->resource_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, sub->resource_uri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, sub->notification_uris, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, sub->notification_type);
    sqlite3_bind_text(stmt, 6, sub->event_type_str, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, sub->content_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, sub->originator, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, sub->creation_time, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, sub->last_modified_time, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "Erro ao inserir subscription: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return rc == SQLITE_DONE;
}

//Estrutura para gerar Request Identifier único
static char* generate_request_id() {
    static char rqi[32];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long timestamp = tv.tv_sec * 1000000LL + tv.tv_usec;
    snprintf(rqi, sizeof(rqi), "%lld", timestamp);
    return rqi;
}

//Função para gerar timestamp oneM2M
static void generate_onem2m_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* utc_tm = gmtime(&now);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    snprintf(buffer, size, "%04d%02d%02dT%02d%02d%02d,%06ld",
            utc_tm->tm_year + 1900, utc_tm->tm_mon + 1, utc_tm->tm_mday,
            utc_tm->tm_hour, utc_tm->tm_min, utc_tm->tm_sec,
            tv.tv_usec);
}


//Função auxiliar para calcular tamanho do conteúdo em bytes
static int calculate_content_size(const char *content) {
    if (!content) return 0;
    return strlen(content);
}

//Função auxiliar para obter dados atualizados do recurso da BD
static json_object* get_resource_data_from_db(const char *resource_uri, int event_type, sqlite3 *db) {
    json_object *resource_data = NULL;
    
    //Extrair componentes da URI
    char uri_copy[512];
    strncpy(uri_copy, resource_uri, sizeof(uri_copy) - 1);
    uri_copy[sizeof(uri_copy) - 1] = '\0';
    
    //Remove a barra inicial se existir
    char *uri_work = uri_copy;
    if (uri_work[0] == '/') uri_work++;
    
    char *csebase = strtok(uri_work, "/");
    char *ae = strtok(NULL, "/");
    char *container = strtok(NULL, "/");
    char *cin_name = strtok(NULL, "/");
    
    if (!csebase || !ae || !container) return NULL;
    
    sqlite3_stmt *stmt = NULL;
    
    if (cin_name) {
        // É uma ContentInstance
        const char *sql =
            "SELECT "
            "  r.ri, r.rn, r.pi, r.ct, r.lt, "
            "  ci.con, ci.cs, ci.et, ci.st "
            "FROM resources r "
            "JOIN content_instances ci ON r.ri = ci.ri "
            "WHERE (r.rn = ? OR r.ri = ?) AND r.pi IN ("
            "  SELECT c_res.ri FROM resources c_res WHERE c_res.rn = ? AND c_res.pi IN ("
            "    SELECT ae_res.ri FROM resources ae_res WHERE ae_res.rn = ? AND ae_res.pi IN ("
            "      SELECT cb_res.ri FROM resources cb_res WHERE cb_res.rn = ?"
            "    )"
            "  )"
            ")";
            
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cin_name, -1, SQLITE_STATIC); // r.rn
            sqlite3_bind_text(stmt, 2, cin_name, -1, SQLITE_STATIC); // r.ri
            sqlite3_bind_text(stmt, 3, container, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, ae, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, csebase, -1, SQLITE_STATIC);
            
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                json_object *cin_obj = json_object_new_object();
            
                const char *ri = (const char *)sqlite3_column_text(stmt, 0);
                const char *rn = (const char *)sqlite3_column_text(stmt, 1);
                const char *pi = (const char *)sqlite3_column_text(stmt, 2);
                const char *ct = (const char *)sqlite3_column_text(stmt, 3);
                const char *lt = (const char *)sqlite3_column_text(stmt, 4);
                const char *con = (const char *)sqlite3_column_text(stmt, 5);
                int cs = sqlite3_column_int(stmt, 6);
                const char *et = (const char *)sqlite3_column_text(stmt, 7);
                int st = sqlite3_column_int(stmt, 8);
            
                if (con) json_object_object_add(cin_obj, "con", json_object_new_string(con));
                if (ri) json_object_object_add(cin_obj, "ri", json_object_new_string(ri));
                if (pi) json_object_object_add(cin_obj, "pi", json_object_new_string(pi));
                if (rn) json_object_object_add(cin_obj, "rn", json_object_new_string(rn));
                if (ct) json_object_object_add(cin_obj, "ct", json_object_new_string(ct));
                if (lt) json_object_object_add(cin_obj, "lt", json_object_new_string(lt));
                json_object_object_add(cin_obj, "ty", json_object_new_int(4));
                json_object_object_add(cin_obj, "cs", json_object_new_int(cs));
                json_object_object_add(cin_obj, "st", json_object_new_int(st));
                if (et) json_object_object_add(cin_obj, "et", json_object_new_string(et));
            
                resource_data = json_object_new_object();
                json_object_object_add(resource_data, "m2m:cin", cin_obj);
            }
        }
    } else {
        const char *sql = 
            "SELECT c.ri, c.et, c.st, r.rn, r.pi, r.ct, r.lt "
            "FROM containers c "
            "JOIN resources r ON c.ri = r.ri "
            "WHERE r.rn = ? AND r.pi IN ("
            "  SELECT ae_res.ri FROM resources ae_res WHERE ae_res.rn = ? AND ae_res.pi IN ("
            "    SELECT cb_res.ri FROM resources cb_res WHERE cb_res.rn = ?"
            "  )"
            ")";
            
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, container, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, ae, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, csebase, -1, SQLITE_STATIC);
            
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                json_object *cnt_obj = json_object_new_object();
                
                const char *ri = (const char *)sqlite3_column_text(stmt, 0);
                const char *et = (const char *)sqlite3_column_text(stmt, 1);
                int st = sqlite3_column_int(stmt, 2);
                const char *rn = (const char *)sqlite3_column_text(stmt, 3);
                const char *pi = (const char *)sqlite3_column_text(stmt, 4);
                const char *ct = (const char *)sqlite3_column_text(stmt, 5);
                const char *lt = (const char *)sqlite3_column_text(stmt, 6);
                
                json_object_object_add(cnt_obj, "ri", json_object_new_string(ri ? ri : ""));
                json_object_object_add(cnt_obj, "pi", json_object_new_string(pi ? pi : ""));
                json_object_object_add(cnt_obj, "rn", json_object_new_string(rn ? rn : ""));
                json_object_object_add(cnt_obj, "ct", json_object_new_string(ct ? ct : ""));
                json_object_object_add(cnt_obj, "lt", json_object_new_string(lt ? lt : ""));
                json_object_object_add(cnt_obj, "ty", json_object_new_int(3)); // Container type
                json_object_object_add(cnt_obj, "st", json_object_new_int(st));
                
                if (et) {
                    json_object_object_add(cnt_obj, "et", json_object_new_string(et));
                }
                
                resource_data = json_object_new_object();
                json_object_object_add(resource_data, "m2m:cnt", cnt_obj);
            }
        }
    }
    
    if (stmt) sqlite3_finalize(stmt);
    return resource_data;
}

bool get_subscription(const char *resource_name, Subscription *sub)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;

    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
        return false;

    const char *sql = "SELECT * FROM subscriptions WHERE resource_name = ?";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, resource_name, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        strncpy(sub->resource_id, (const char *)sqlite3_column_text(stmt, 1), sizeof(sub->resource_id) - 1);
        strncpy(sub->resource_name, (const char *)sqlite3_column_text(stmt, 2), sizeof(sub->resource_name) - 1);
        strncpy(sub->resource_uri, (const char *)sqlite3_column_text(stmt, 3), sizeof(sub->resource_uri) - 1);
        strncpy(sub->notification_uris, (const char *)sqlite3_column_text(stmt, 4), sizeof(sub->notification_uris) - 1);
        sub->notification_type = sqlite3_column_int(stmt, 5);
        sub->event_type = sqlite3_column_int(stmt, 6);
        strncpy(sub->content_type, (const char *)sqlite3_column_text(stmt, 7), sizeof(sub->content_type) - 1);
        strncpy(sub->originator, (const char *)sqlite3_column_text(stmt, 8), sizeof(sub->originator) - 1);
        strncpy(sub->creation_time, (const char *)sqlite3_column_text(stmt, 9), sizeof(sub->creation_time) - 1);
        strncpy(sub->last_modified_time, (const char *)sqlite3_column_text(stmt, 10), sizeof(sub->last_modified_time) - 1);
        rc = true;
    }
    else
    {
        rc = false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rc;
}

static void extract_subscriber_id(const char *uri, char *buffer, size_t buffer_size, bool *use_tls)
{
    *use_tls = false;
    const char *prefix_mqtt = "mqtt://";
    const char *prefix_mqtts = "mqtts://";

    //Verificar primeiro o MQTTS (mais específico do que quando se tem apenas MQTT)
    if (strncmp(uri, prefix_mqtts, strlen(prefix_mqtts)) == 0)
    {
        uri += strlen(prefix_mqtts);
        *use_tls = true;
    }
    else if (strncmp(uri, prefix_mqtt, strlen(prefix_mqtt)) == 0)
    {
        uri += strlen(prefix_mqtt);
        *use_tls = false;
    }
    else
    {
        printf("[MQTT] Nenhum prefixo detetado, assumindo MQTT sem TLS\n");
        // Se não tem prefixo, assumir MQTT sem TLS
        *use_tls = false;
    }

    // Remover barras extras
    while (*uri == '/')
        uri++;

    // Encontrar o fim do subscriber_id (antes da próxima barra ou fim da string)
    const char *end = strchr(uri, '/');
    size_t len = end ? (end - uri) : strlen(uri);

    if (len >= buffer_size)
        len = buffer_size - 1;
    
    strncpy(buffer, uri, len);
    buffer[len] = '\0';

    printf("[MQTT] TLS habilitado: %s\n", *use_tls ? "SIM" : "NÃO");
}

static bool should_notify_for_event(const char *subscription_event_types, int actual_event_type, const char *resource_uri, const char *subscription_uri)
{

    // Verificar se o evento atual está na lista de eventos subscritos
    char *event_list = strdup(subscription_event_types);
    char *token = strtok(event_list, ",");
    bool event_match = false;

    while (token != NULL)
    {
        int subscribed_event = atoi(token);
        if (subscribed_event == actual_event_type)
        {
            event_match = true;
            break;
        }
        token = strtok(NULL, ",");
    }

    free(event_list);

    if (!event_match)
    {
        return false;
    }

    // Normalizar ambas as URIs antes da comparação
    char normalized_resource[512];
    char normalized_subscription[512];

    normalize_uri(resource_uri, normalized_resource, sizeof(normalized_resource));
    normalize_uri(subscription_uri, normalized_subscription, sizeof(normalized_subscription));

    switch (actual_event_type)
    {
    case 1: // Update_of_Resource
            // Notificar se o recurso atualizado é exatamente o subscrito
        return (strcmp(normalized_resource, normalized_subscription) == 0);

    case 2: // Delete_of_Resource
        // Notificar se o recurso deletado é exatamente o subscrito
        bool match = (strcmp(normalized_resource, normalized_subscription) == 0);
        return match;

    case 3: // Create_of_Direct_Child_Resource
        // Notificar se o recurso criado é filho direto do subscrito
        return is_direct_child(normalized_resource, normalized_subscription);

    //case 4: // Delete_of_Direct_Child_Resource
        // Notificar se o recurso deletado é filho direto do subscrito
        //return is_direct_child(normalized_resource, normalized_subscription);

    default:
        return false;
    }
}

static char* get_ri_from_resource_uri(const char *resource_uri, sqlite3 *db) {
    static char ri[128] = "";
    if (!resource_uri || !db) return NULL;

    // Extrair último segmento da URI (pode ser rn ou ri)
    const char *last_slash = strrchr(resource_uri, '/');
    const char *name = last_slash ? last_slash + 1 : resource_uri;

    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT ri FROM resources WHERE rn = ? OR ri = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            strncpy(ri, (const char *)sqlite3_column_text(stmt, 0), sizeof(ri) - 1);
            ri[sizeof(ri) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
        return ri;
    }
    return NULL;
}

static bool is_direct_child(const char *child_uri, const char *parent_uri) 
{
    printf("A Verificar se '%s' é filho direto de '%s'\n", child_uri, parent_uri);
    
    if (!child_uri || !parent_uri) {
        return false;
    }
    
    char clean_parent[512], clean_child[512];
    strncpy(clean_parent, parent_uri, sizeof(clean_parent) - 1);
    strncpy(clean_child, child_uri, sizeof(clean_child) - 1);
    clean_parent[sizeof(clean_parent) - 1] = '\0';
    clean_child[sizeof(clean_child) - 1] = '\0';
    
    //Remove trailing slashes
    int len = strlen(clean_parent);
    if (len > 0 && clean_parent[len-1] == '/') {
        clean_parent[len-1] = '\0';
    }
    
    len = strlen(clean_child);
    if (len > 0 && clean_child[len-1] == '/') {
        clean_child[len-1] = '\0';
    }
    
    // Verificar se child começa com parent + "/"
    char expected_prefix[612];
    snprintf(expected_prefix, sizeof(expected_prefix), "%s/", clean_parent);
    
    if (strncmp(clean_child, expected_prefix, strlen(expected_prefix)) != 0) {
        return false;
    }
    
    // Verificar se é filho DIRETO (não tem mais "/" depois do parent)
    const char *remaining = clean_child + strlen(expected_prefix);
    bool is_direct = strchr(remaining, '/') == NULL && strlen(remaining) > 0;
    return is_direct;
}

static void normalize_uri(const char *uri, char *normalized, size_t size) 
{
    if (!uri || !normalized || size == 0) return;
    
    // Copiar URI removendo barras duplas e normalizando
    strncpy(normalized, uri, size - 1);
    normalized[size - 1] = '\0';
    
    // Remover barra final se existir (exceto se for apenas "/")
    size_t len = strlen(normalized);
    if (len > 1 && normalized[len - 1] == '/') {
        normalized[len - 1] = '\0';
    }
    
    // Garantir que começa com "/"
    if (normalized[0] != '/') {
        char temp[512];
        snprintf(temp, sizeof(temp), "/%s", normalized);
        strncpy(normalized, temp, size - 1);
        normalized[size - 1] = '\0';
    }
}

void handle_mqtt_notification(const char *resource_uri, const char *content, int event_type)
{
    // Verificar se a notificação é duplicada (manter como está)
    time_t current_time = time(NULL);
    if (strcmp(content, last_notification_content) == 0 &&
        strcmp(resource_uri, last_notification_uri) == 0 &&
        (current_time - last_notification_time) < 2)
    {
        return;
    }
    
    // Atualizar cache (manter como está)
    strncpy(last_notification_content, content, sizeof(last_notification_content) - 1);
    last_notification_content[sizeof(last_notification_content) - 1] = '\0';
    strncpy(last_notification_uri, resource_uri, sizeof(last_notification_uri) - 1);
    last_notification_uri[sizeof(last_notification_uri) - 1] = '\0';
    last_notification_time = current_time;

    // Código de abertura DB e loop (manter como está até o ponto onde constrói o payload)
    sqlite3 *db;
    sqlite3_stmt *stmt;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK)
    {
        printf("[ERRO] Falha ao abrir a base de dados\n");
        return;
    }

    const char *sql = "SELECT * FROM subscriptions";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
    {
        printf("[ERRO] Falha ao executar a query SQL: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    int subscription_count = 0;
    int notifications_sent = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        subscription_count++;
        Subscription sub = {0};
        
        // Preencher estrutura
        strncpy(sub.resource_id,   (const char *)sqlite3_column_text(stmt, 1), sizeof(sub.resource_id)   - 1); // ri
        strncpy(sub.resource_name, (const char *)sqlite3_column_text(stmt, 2), sizeof(sub.resource_name) - 1); // resource_name
        strncpy(sub.resource_uri, (const char *)sqlite3_column_text(stmt, 3), sizeof(sub.resource_uri) - 1);
        strncpy(sub.notification_uris, (const char *)sqlite3_column_text(stmt, 4), sizeof(sub.notification_uris) - 1);
        sub.notification_type = sqlite3_column_int(stmt, 5);
        const char *stored_events = (const char *)sqlite3_column_text(stmt, 6);
        if (stored_events)
        {
            strncpy(sub.event_type_str, stored_events, sizeof(sub.event_type_str) - 1);
            sub.event_type_str[sizeof(sub.event_type_str) - 1] = '\0';
        }
        else
        {
            sub.event_type_str[0] = '\0';
        }
        strncpy(sub.content_type, (const char *)sqlite3_column_text(stmt, 6), sizeof(sub.content_type) - 1);
        strncpy(sub.originator, (const char *)sqlite3_column_text(stmt, 7), sizeof(sub.originator) - 1);
        strncpy(sub.creation_time, (const char *)sqlite3_column_text(stmt, 8), sizeof(sub.creation_time) - 1);
        strncpy(sub.last_modified_time, (const char *)sqlite3_column_text(stmt, 9), sizeof(sub.last_modified_time) - 1);

        // Verificar se deve notificar (manter como está)
        if (!should_notify_for_event(sub.event_type_str, event_type, resource_uri, sub.resource_uri)) {
            printf("[SUB] A Subscription %s NÃO deve ser notificada para este tipo de evento\n", sub.resource_name);
            continue;
        }

        printf("[SUB] A Enviar NOTIFICAÇÃO para a subscription %s \n", sub.resource_name);
        notifications_sent++;

        char cse_id[64];
        if (!get_cse_id_from_parent(sub.resource_uri, cse_id, sizeof(cse_id))) {
            continue;
        }

        // Extrair subscriber info (manter como está)
        char subscriber_id[256];
        bool use_tls = false;
        MQTTURIInfo uri_info;
        extract_subscriber_id_enhanced(sub.notification_uris, subscriber_id, sizeof(subscriber_id), &use_tls, &uri_info);

        char formatted_cse_id[64];
        strncpy(formatted_cse_id, cse_id, sizeof(formatted_cse_id) - 1);
        formatted_cse_id[sizeof(formatted_cse_id) - 1] = '\0';
        
        // Formatar CSE ID (manter como está)
        for (char *p = formatted_cse_id; *p; p++)
        {
            if (*p == '\\') *p = '/';
        }
        if (formatted_cse_id[0] == '/')
        {
            memmove(formatted_cse_id, formatted_cse_id + 1, strlen(formatted_cse_id));
        }
        
        // 1. Criar estrutura principal da notificação oneM2M
        json_object *notification = json_object_new_object();
        
        // 2. Campos obrigatórios do cabeçalho oneM2M - **CORRIGIDO: adicionar barra inicial**
        char fr_with_slash[128];
        snprintf(fr_with_slash, sizeof(fr_with_slash), "/%s", formatted_cse_id);
        json_object_object_add(notification, "fr", json_object_new_string(fr_with_slash));
        json_object_object_add(notification, "to", json_object_new_string(subscriber_id));
        
        // 3. Gerar timestamp oneM2M
        char timestamp[64];
        generate_onem2m_timestamp(timestamp, sizeof(timestamp));
        json_object_object_add(notification, "ot", json_object_new_string(timestamp));
        
        // 4. Campos obrigatórios
        json_object_object_add(notification, "op", json_object_new_int(5)); // NOTIFY operation
        json_object_object_add(notification, "rqi", json_object_new_string(generate_request_id()));
        json_object_object_add(notification, "rvi", json_object_new_string("3")); // oneM2M release version 3
        json_object_object_add(notification, "drt", json_object_new_int(1));
        
        // 5. Construir Primitive Content (pc)
        json_object *pc = json_object_new_object();
        json_object *sgn = json_object_new_object();
        json_object *nev = json_object_new_object();
        json_object *rep = json_object_new_object();

        // 6. Obter dados atualizados do recurso da BD
        json_object *fresh_resource_data = get_resource_data_from_db(resource_uri, event_type, db);
        
        int effective_nct = sub.notification_type;

        // 7. Regras específicas por tipo de evento
        switch (event_type)
        {
        case 2: // Delete - sempre nct=3
            effective_nct = 3;
            break;
        case 1: // Update - pode ser nct=1 ou nct=2
            // Manter o nct original (1 ou 2)
            break;
        case 3: // Create - pode ser nct=2 ou nct=3
            break;
        }

        if (effective_nct == 1) // All Attributes
        {
            bool added = false;
            if (fresh_resource_data) {
                json_object_object_foreach(fresh_resource_data, key, val) {
                    if (val && json_object_get_type(val) != json_type_null) {
                        json_object_object_add(rep, key, json_object_get(val));
                        added = true;
                    }
                    break;
                }
            }
            if (!added) {
                // Fallback: se for update de container, envia m2m:cnt null
                if (event_type == 1 && strstr(resource_uri, "/") && strrchr(resource_uri, '/') == strchr(resource_uri, '/')) {
                    // Provavelmente é um container
                    json_object_object_add(rep, "m2m:cnt", NULL);
                } else {
                    // Fallback para contentInstance
                    json_object *content_obj = json_tokener_parse(content);
                    if (content_obj && json_object_is_type(content_obj, json_type_object)) {
                        json_object *cin_obj = NULL;
                        if (json_object_object_get_ex(content_obj, "m2m:cin", &cin_obj) && cin_obj && json_object_is_type(cin_obj, json_type_object)) {
                            json_object_object_add(rep, "m2m:cin", json_object_get(cin_obj));
                            added = true;
                        } else {
                            json_object_object_add(rep, "m2m:cin", json_object_get(content_obj));
                            added = true;
                        }
                    }
                    if (content_obj) json_object_put(content_obj);
                }
            }
            if (!added) {
                json_object_object_add(rep, "m2m:cnt", NULL);
            }
        }
        else if (effective_nct == 2) // Modified Attributes
        {
            bool added = false;
            if (event_type == 1 && content && strlen(content) > 0 && fresh_resource_data) {
                json_object *content_obj = json_tokener_parse(content);
                if (content_obj && json_object_is_type(content_obj, json_type_object)) {
                    json_object *cnt_update = NULL;
                    if (json_object_object_get_ex(content_obj, "m2m:cnt", &cnt_update) && cnt_update) {
                        json_object *cnt_bd = NULL;
                        if (json_object_object_get_ex(fresh_resource_data, "m2m:cnt", &cnt_bd) && cnt_bd) {
                            json_object *cnt_mod = json_object_new_object();
                            json_object_object_foreach(cnt_update, mod_key, mod_val) {
                                json_object *updated_val = NULL;
                                if (json_object_object_get_ex(cnt_bd, mod_key, &updated_val) && updated_val) {
                                    json_object_object_add(cnt_mod, mod_key, json_object_get(updated_val));
                                }
                            }
                            json_object_object_add(rep, "m2m:cnt", cnt_mod);
                            added = true;
                        }
                    }
                }
                if (content_obj) json_object_put(content_obj);
            }
            if (!added) {
                // Se não há atributos modificados, envia sempre a notificação com null
                json_object_object_add(rep, "m2m:cnt", NULL);
            }
        }
        else if (effective_nct == 3) // Resource ID
        {
            char *ri = get_ri_from_resource_uri(resource_uri, db);
            if (ri && strlen(ri) > 0) {
                json_object_object_add(rep, "m2m:uri", json_object_new_string(ri));
            } else {
                // fallback para o nome, se não encontrar
                const char *last_slash = strrchr(resource_uri, '/');
                const char *resource_id = last_slash ? last_slash + 1 : resource_uri;
                json_object_object_add(rep, "m2m:uri", json_object_new_string(resource_id));
            }
        }

        // 9. Montar estrutura nev
        json_object_object_add(nev, "net", json_object_new_int(event_type));
        json_object_object_add(nev, "rep", rep);

        // 10. Montar estrutura sgn  
        json_object_object_add(sgn, "nev", nev);
        
        // 11.Construir subscription reference com barra inicial
        char subscription_ref[1024];
        snprintf(subscription_ref, sizeof(subscription_ref), "/%s/%s", formatted_cse_id, sub.resource_id);
        json_object_object_add(sgn, "sur", json_object_new_string(subscription_ref));

        // 12. Finalizar estrutura
        json_object_object_add(pc, "m2m:sgn", sgn);
        json_object_object_add(notification, "pc", pc);

        // 13. Gerar payload final
        const char *payload = json_object_to_json_string_ext(notification, JSON_C_TO_STRING_PRETTY);
        printf("\n=== PAYLOAD oneM2M Criado (nct=%d, net=%d, TLS=%s) ===\n%s\n==============\n", effective_nct, event_type, use_tls ? "YES" : "NO", payload);

        // Limpar dados do recurso se foram obtidos da BD
        if (fresh_resource_data) {
            json_object_put(fresh_resource_data);
        }


        // Check if the notification is via http
        if (strncmp(sub.notification_uris, "http://", 7) == 0) {
            http_args_t *args = malloc(sizeof(http_args_t));
            args->url = strdup(sub.notification_uris);
            args->payload = strdup(payload);  // Ensure payload stays valid
        
            pthread_t tid;
            if (pthread_create(&tid, NULL, send_http_notification_thread, args) != 0) {
                printf("[ERRO] Falha ao criar thread para notificação HTTP\n");
                free(args->url);
                free(args->payload);
                free(args);
            } else {
                pthread_detach(tid); // No need to join later
            }
        
            json_object_put(notification);  // Clean up JSON object
            continue; // Skip MQTT
        }

        // O resto do código MQTT permanece igual...
        MQTTConnection *notification_conn = NULL;
        
        if (uri_info.is_full_uri) {
            notification_conn = mqtt_create_dynamic_connection(uri_info.host, uri_info.port, uri_info.use_tls);
        } else {
            notification_conn = mqtt_create_dynamic_connection("localhost", use_tls ? 8883 : 1883, use_tls);
        }
        
        if (!notification_conn) {
            printf("[ERRO] Falha ao criar conexão MQTT%s para %s:%d\n", 
                   use_tls ? "S" : "", 
                   uri_info.is_full_uri ? uri_info.host : "localhost",
                   uri_info.is_full_uri ? uri_info.port : (use_tls ? 8883 : 1883));
            json_object_put(notification);
            continue;
        }
        
        if (!mqtt_connect2(notification_conn)) {
            printf("[ERRO] Falha na conexão MQTT%s para %s:%d\n", 
                   use_tls ? "S" : "",
                   uri_info.is_full_uri ? uri_info.host : "localhost",
                   uri_info.is_full_uri ? uri_info.port : (use_tls ? 8883 : 1883));
            mqtt_cleanup2(notification_conn);
            json_object_put(notification);
            continue;
        }
        
        char full_topic[1024];
        if (uri_info.is_full_uri) {
            strncpy(full_topic, uri_info.topic, sizeof(full_topic) - 1);
            full_topic[sizeof(full_topic) - 1] = '\0';
        } else {
            int result = snprintf(full_topic, sizeof(full_topic), "/oneM2M/req/%s/%s/json", 
                                formatted_cse_id, uri_info.topic);
            if (result >= sizeof(full_topic)) {
                printf("[ERRO] Tópico MQTT não suportado: %s\n", full_topic);
                json_object_put(notification);
                mqtt_cleanup2(notification_conn);
                continue;
            }
        }
        
        printf("[MQTT] Tópico MQTT construído: %s\n", full_topic);
        printf("[MQTT] A estabelecer conexão a %s:%d (TLS: %s)\n", 
               uri_info.is_full_uri ? uri_info.host : "localhost",
               uri_info.is_full_uri ? uri_info.port : (use_tls ? 8883 : 1883),
               uri_info.use_tls ? "sim" : "não");
        
        bool success = mqtt_publish2(notification_conn, full_topic, payload);
        mqtt_cleanup2(notification_conn);
        
        if (success) {
            printf("[MQTT] Notificação #%d enviada com sucesso via %s para %s:%d! ***\n", 
                   notifications_sent, uri_info.use_tls ? "MQTTS" : "MQTT", 
                   uri_info.is_full_uri ? uri_info.host : "localhost",
                   uri_info.is_full_uri ? uri_info.port : (use_tls ? 8883 : 1883));
        } else {
            printf("[MQTT] Falha ao enviar notificação via %s para %s:%d!\n", 
                   uri_info.use_tls ? "MQTTS" : "MQTT", 
                   uri_info.is_full_uri ? uri_info.host : "localhost",
                   uri_info.is_full_uri ? uri_info.port : (use_tls ? 8883 : 1883));
        }
        
        json_object_put(notification);
    }
    
    printf("[MQTT] Total de subscriptions verificadas: %d, notificações enviadas: %d\n", 
           subscription_count, notifications_sent);
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// Função para extrair conteúdo real de um JSON
static json_object *extract_content_from_json(const char *content)
{
    if (!content) {
        return json_object_new_string("");
    }

    // Tenta parsear o conteúdo como JSON
    json_object *parsed = json_tokener_parse(content);
    if (!parsed)
    {
        // Se não é JSON válido, retorna como string simples
        return json_object_new_string(content);
    }

    // Se é um wrapper m2m:cin, extrai o conteúdo interno
    json_object *cin_obj;
    if (json_object_object_get_ex(parsed, "m2m:cin", &cin_obj))
    {
        json_object *con_obj;
        if (json_object_object_get_ex(cin_obj, "con", &con_obj))
        {
            // Incrementa referência para não ser liberado quando parsed for liberado
            json_object_get(con_obj);
            json_object_put(parsed);
            return con_obj;
        }
        // Se não tem "con", retorna o objeto cin completo
        json_object_get(cin_obj);
        json_object_put(parsed);
        return cin_obj;
    }

    // Se não é um wrapper, retorna o objeto completo
    return parsed;
}

static void get_ri_from_resource_uri_aux(const char *resource_uri, sqlite3 *db, char *out_ri, size_t out_ri_size) {
    sqlite3 *local_db = db;
    if (!local_db) {
        if (sqlite3_open(DB_PATH, &local_db) != SQLITE_OK) return;
    }
    const char *last_slash = strrchr(resource_uri, '/');
    const char *name = last_slash ? last_slash + 1 : resource_uri;
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT ri FROM resources WHERE rn = ? OR ri = ?";
    if (sqlite3_prepare_v2(local_db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            strncpy(out_ri, (const char *)sqlite3_column_text(stmt, 0), out_ri_size - 1);
            out_ri[out_ri_size - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }
    if (!db) sqlite3_close(local_db);
}

// 1. Handler para GET subscription
bool handle_subscription_request(const char *request_json, const char *resource_uri, const char *originator, char *response, size_t response_size, int client_socket)
{
    json_object *request = json_tokener_parse(request_json);
    if (!request)
        return false;

    Subscription sub = {0};
    json_object *sub_obj;

    // Parse m2m:sub object
    if (!json_object_object_get_ex(request, "m2m:sub", &sub_obj))
    {
        json_object_put(request);
        return false;
    }

    // Get resource name
    json_object *rn;
    if (!json_object_object_get_ex(sub_obj, "rn", &rn))
    {
        json_object_put(request);
        return false;
    }
    strncpy(sub.resource_name, json_object_get_string(rn), sizeof(sub.resource_name) - 1);
    
    const char *resource_name = json_object_get_string(rn);

    if (get_subscription(resource_name, &sub))
    {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 403 Already exists\r\n\r\n");
        
        write(client_socket, response, strlen(response));
        return false;
    }

    // Gera um ri único para a subscription
    snprintf(sub.resource_id, sizeof(sub.resource_id), "sub%lld", (long long)time(NULL) * 1000000LL + rand() % 1000000);

    // Set parent ID from resource URI
    // Antes de criar a subscription:
    char parent_ri[128] = "";
    get_ri_from_resource_uri_aux(resource_uri, NULL, parent_ri, sizeof(parent_ri)); // Nova função, ver abaixo
    strncpy(sub.parent_id, parent_ri, sizeof(sub.parent_id) - 1);
    strncpy(sub.resource_uri, resource_uri, sizeof(sub.resource_uri) - 1);

    // Get notification URI (nu)
    json_object *nu;
    if (json_object_object_get_ex(sub_obj, "nu", &nu) &&
        json_object_is_type(nu, json_type_array))
    {
        const char *uri = json_object_get_string(json_object_array_get_idx(nu, 0));

        // IMPORTANTE: Armazenar a URI COMPLETA (com protocolo)
        strncpy(sub.notification_uris, uri, sizeof(sub.notification_uris) - 1);
        sub.notification_uris[sizeof(sub.notification_uris) - 1] = '\0';
    }
    else
    {
        json_object_put(request);
        return false;
    }

    // Get notification content type (nct) - PADRÃO 1 se não especificado
    json_object *nct;
    if (json_object_object_get_ex(sub_obj, "nct", &nct))
    {
        sub.notification_type = json_object_get_int(nct);
    }
    else
    {
        sub.notification_type = 1; // PADRÃO: All Attributes
        printf("[SUB] NCT não especificado, utilizando o padrão: 1 (All Attributes)\n");
    }

    // Get event notification criteria (enc)
    json_object *enc;
    if (json_object_object_get_ex(sub_obj, "enc", &enc))
    {
        json_object *net;
        if (json_object_object_get_ex(enc, "net", &net) &&
            json_object_is_type(net, json_type_array))
        {
            char event_types[256] = {0};
            int array_len = json_object_array_length(net);

            // VALIDAÇÃO: Verificar combinações incompatíveis nct/net
            for (int i = 0; i < array_len; i++)
            {
                int event_val = json_object_get_int(json_object_array_get_idx(net, i));

                // Validar usando a função corrigida
                if (!is_valid_nct_net_combination(sub.notification_type, event_val))
                {
                    snprintf(response, response_size,
                             "HTTP/1.1 422 Unprocessable Entity"
                             "\r\nContent-Type: application/json"
                             "\r\n\r\n"
                             "{\"error\":\"Invalid combination: nct=%d with net=%d\"}",
                             sub.notification_type, event_val);
                    json_object_put(request);
                    return false;
                }

                char val_str[16];
                snprintf(val_str, sizeof(val_str), "%s%d", (i > 0 ? "," : ""), event_val);
                strncat(event_types, val_str, sizeof(event_types) - strlen(event_types) - 1);
            }
            strncpy(sub.event_type_str, event_types, sizeof(sub.event_type_str) - 1);
        }
    }

    // Set timestamps
    time_t now = time(NULL);
    strftime(sub.creation_time, sizeof(sub.creation_time), "%Y-%m-%d %H:%M:%S", localtime(&now));
    strncpy(sub.last_modified_time, sub.creation_time, sizeof(sub.last_modified_time));

    // Set resource type and originator
    sub.resource_type = 23; // subscription
    strncpy(sub.originator, originator, sizeof(sub.originator) - 1);

    bool success = create_subscription(&sub);
    if (success)
    {
        // Format response according to oneM2M standard
        json_object *response_obj = json_object_new_object();
        json_object *sub_resp = json_object_new_object();

        json_object_object_add(sub_resp, "rn", json_object_new_string(sub.resource_name));
        json_object_object_add(sub_resp, "ri", json_object_new_string(sub.resource_id));
        json_object_object_add(sub_resp, "pi", json_object_new_string(sub.parent_id));
        json_object_object_add(sub_resp, "ty", json_object_new_int(sub.resource_type));
        json_object_object_add(sub_resp, "ct", json_object_new_string(sub.creation_time));
        json_object_object_add(sub_resp, "lt", json_object_new_string(sub.last_modified_time));

        json_object *nu_array = json_object_new_array();
        json_object_array_add(nu_array, json_object_new_string(sub.notification_uris));
        json_object_object_add(sub_resp, "nu", nu_array);

        json_object_object_add(sub_resp, "nct", json_object_new_int(sub.notification_type));

        json_object_object_add(response_obj, "m2m:sub", sub_resp);

        const char *json_response = json_object_to_json_string(response_obj);
        strncpy(response, json_response, response_size - 1);
        json_object_put(response_obj);
    }

    json_object_put(request);
    return success;
}

static bool parse_mqtt_uri_enhanced(const char *uri, MQTTURIInfo *info) {
    if (!uri || !info) {
        return false;
    }
    
    memset(info, 0, sizeof(MQTTURIInfo));
    
    // Verificar se é formato completo mqtt://host:port/topic ou mqtts://host:port/topic
    if (strncmp(uri, "mqtts://", 8) == 0) {
        info->use_tls = true;
        info->is_full_uri = true;
        uri += 8; // Pula "mqtts://"
    } else if (strncmp(uri, "mqtt://", 7) == 0) {
        const char *after_prefix = uri + 7;
        
        // CORREÇÃO: Verificar se realmente tem host:port ou apenas tópico
        // Se tem ':' seguido por números E '/', é formato completo
        const char *colon = strchr(after_prefix, ':');
        const char *slash = strchr(after_prefix, '/');
        
        if (colon && slash && colon < slash) {
            // Verificar se entre ':' e '/' são apenas dígitos (porta)
            bool is_port = true;
            for (const char *p = colon + 1; p < slash; p++) {
                if (!isdigit(*p)) {
                    is_port = false;
                    break;
                }
            }
            
            if (is_port) {
                // É formato completo: mqtt://host:port/topic
                info->use_tls = false;
                info->is_full_uri = true;
                uri += 7; // Pula "mqtt://"
            } else {
                // É formato simples com ':' no tópico
                info->is_full_uri = false;
                info->use_tls = false;
                strcpy(info->host, "localhost");
                info->port = 1883;
                strncpy(info->topic, after_prefix, sizeof(info->topic) - 1);
                info->topic[sizeof(info->topic) - 1] = '\0';
                printf("[MQTT] Formato simples com ':' no tópico - Host: %s, Port: %d, Topic: %s\n", 
                       info->host, info->port, info->topic);
                return true;
            }
        } else if (slash && !colon) {
            // mqtt://host/topic (sem porta)
            info->use_tls = false;
            info->is_full_uri = true;
            uri += 7; // Pula "mqtt://"
        } else {
            // Formato simples - apenas tópico
            info->is_full_uri = false;
            info->use_tls = false;
            strcpy(info->host, "localhost");
            info->port = 1883;
            strncpy(info->topic, after_prefix, sizeof(info->topic) - 1);
            info->topic[sizeof(info->topic) - 1] = '\0';
            return true;
        }
    } else if (strncmp(uri, "http://", 7) == 0) {
        info->use_tls = false;
        info->is_full_uri = true;
        uri += 7; // Salta o "http://"
    } else {
        // Sem prefixo - assumir formato simples
        info->is_full_uri = false;
        info->use_tls = false;
        strcpy(info->host, "localhost");
        info->port = 1883;
        strncpy(info->topic, uri, sizeof(info->topic) - 1);
        info->topic[sizeof(info->topic) - 1] = '\0';
        
        return true;
    }
    
    // Parsear formato completo: host:port/topic
    const char *slash = strchr(uri, '/');
    if (!slash) {
        return false;
    }
    
    // Extrair tópico (tudo após a primeira barra)
    strncpy(info->topic, slash, sizeof(info->topic) - 1);
    info->topic[sizeof(info->topic) - 1] = '\0';
    
    // Parsear host:port
    const char *colon = strchr(uri, ':');
    if (colon && colon < slash) {
        // Tem porta especificada
        size_t host_len = colon - uri;
        if (host_len >= sizeof(info->host)) {
            host_len = sizeof(info->host) - 1;
        }
        strncpy(info->host, uri, host_len);
        info->host[host_len] = '\0';
        
        // Extrair porta
        char port_str[16];
        size_t port_len = slash - colon - 1;
        if (port_len >= sizeof(port_str)) {
            port_len = sizeof(port_str) - 1;
        }
        strncpy(port_str, colon + 1, port_len);
        port_str[port_len] = '\0';
        info->port = atoi(port_str);
        
        // Validar porta
        if (info->port <= 0 || info->port > 65535) {
            printf("[ERRO] Porta inválida: %d\n", info->port);
            return false;
        }
    } else {
        // Sem porta especificada, usar host completo e porta padrão
        size_t host_len = slash - uri;
        if (host_len >= sizeof(info->host)) {
            host_len = sizeof(info->host) - 1;
        }
        strncpy(info->host, uri, host_len);
        info->host[host_len] = '\0';
        
        // Porta padrão baseada no protocolo
        info->port = info->use_tls ? 8883 : 1883;
    }
    
    return true;
}

// Função aprimorada para parsear URIs MQTT completas ou simples
static bool parse_mqtt_uri(const char *uri, MQTTURIInfo *info)
{
    if (!uri || !info) {
        return false;
    }
    
    memset(info, 0, sizeof(MQTTURIInfo));
    
    // Verificar se é formato completo mqtt://host:port/topic ou mqtts://host:port/topic
    if (strncmp(uri, "mqtts://", 8) == 0) {
        info->use_tls = true;
        info->is_full_uri = true;
        uri += 8; // Pula "mqtts://"
    } else if (strncmp(uri, "mqtt://", 7) == 0) {
        info->use_tls = false;
        info->is_full_uri = true;
        uri += 7; // Pula "mqtt://"
    } else {
        // Formato simples - apenas tópico (comportamento atual)
        info->is_full_uri = false;
        info->use_tls = false;
        strcpy(info->host, "localhost"); // Default
        info->port = 1883; // Default
        strncpy(info->topic, uri, sizeof(info->topic) - 1);
        info->topic[sizeof(info->topic) - 1] = '\0';

        return true;
    }
    
    // Parsear formato completo: host:port/topic
    const char *slash = strchr(uri, '/');
    if (!slash) {
        printf("[MQTT] URI MQTT inválida - falta tópico: %s\n", uri);
        return false;
    }
    
    // Extrair tópico (tudo após a primeira barra)
    strncpy(info->topic, slash, sizeof(info->topic) - 1);
    info->topic[sizeof(info->topic) - 1] = '\0';
    
    // Parsear host:port
    const char *colon = strchr(uri, ':');
    if (colon && colon < slash) {
        // Tem porta especificada
        size_t host_len = colon - uri;
        if (host_len >= sizeof(info->host)) {
            host_len = sizeof(info->host) - 1;
        }
        strncpy(info->host, uri, host_len);
        info->host[host_len] = '\0';
        
        // Extrair porta
        char port_str[16];
        size_t port_len = slash - colon - 1;
        if (port_len >= sizeof(port_str)) {
            port_len = sizeof(port_str) - 1;
        }
        strncpy(port_str, colon + 1, port_len);
        port_str[port_len] = '\0';
        info->port = atoi(port_str);
    } else {
        // Sem porta especificada, usar host completo e porta padrão
        size_t host_len = slash - uri;
        if (host_len >= sizeof(info->host)) {
            host_len = sizeof(info->host) - 1;
        }
        strncpy(info->host, uri, host_len);
        info->host[host_len] = '\0';
        
        // Porta padrão baseada no protocolo
        info->port = info->use_tls ? 8883 : 1883;
    }
    
    printf("[MQTT] URI completa - Host: %s, Port: %d, TLS: %s, Topic: %s\n",info->host, info->port, info->use_tls ? "sim" : "não", info->topic);
    
    return true;
}

// Função para validar combinações nct/net
static bool is_valid_nct_net_combination(int nct, int net) {

    // nct=2 (Modified Attributes) só faz sentido em Updates (net=1)
    if (nct == 2 && net != 1) {
        return false;
    }
    // nct=1 (All Attributes) não é permitido para net=2 (Delete)
    if (nct == 1 && net == 2) {
        return false;
    }
    // nct=3 (ResourceID) é o único permitido para net=2 (Delete)
    if (net == 2 && nct != 3) {
        return false;
    }
    // nct=3 (ResourceID) só em casos reconhecidos (net=1,2,3)
    if (nct == 3 && net != 1 && net != 2 && net != 3) {
        return false;
    }
    // nct=1 (All Attributes) só em casos reconhecidos (net=1,3)
    if (nct == 1 && net != 1 && net != 3) {
        return false;
    }
    return true;
}

static void extract_subscriber_id_enhanced(const char *uri, char *buffer, size_t buffer_size, bool *use_tls, MQTTURIInfo *uri_info)
{
    if (!uri || !buffer || !use_tls || !uri_info) {
        return;
    }
    
    // Inicializar estrutura
    memset(uri_info, 0, sizeof(MQTTURIInfo));
    
    // Verificar se é mqtts://subscriber_id ou mqtt://subscriber_id (formato simples)
    if (strncmp(uri, "mqtts://", 8) == 0) {
        const char *subscriber_part = uri + 8;
        
        // Verificar se tem formato host:port/topic ou apenas subscriber_id
        if (!strchr(subscriber_part, '/') && !strchr(subscriber_part, ':')) {
            // Formato simples: mqtts://subscriber_id
            *use_tls = true;
            uri_info->use_tls = true;
            uri_info->is_full_uri = false;
            strcpy(uri_info->host, "localhost");
            uri_info->port = 8883;
            
            strncpy(buffer, subscriber_part, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            strncpy(uri_info->topic, subscriber_part, sizeof(uri_info->topic) - 1);

            return;
        }
    } else if (strncmp(uri, "mqtt://", 7) == 0) {
        const char *subscriber_part = uri + 7;
        
        // Verificar se tem formato host:port/topic ou apenas subscriber_id
        if (!strchr(subscriber_part, '/') && !strchr(subscriber_part, ':')) {
            // Formato simples: mqtt://subscriber_id
            *use_tls = false;
            uri_info->use_tls = false;
            uri_info->is_full_uri = false;
            strcpy(uri_info->host, "localhost");
            uri_info->port = 1883;
            
            strncpy(buffer, subscriber_part, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            strncpy(uri_info->topic, subscriber_part, sizeof(uri_info->topic) - 1);

            return;
        }
    } else if (strncmp(uri, "http://", 7) == 0) {
        const char *subscriber_part = uri + 7;

        *use_tls = false;
        uri_info->use_tls = false;
        uri_info->is_full_uri = true;
        strcpy(uri_info->host, "localhost");
        uri_info->port = 8080;
        
        strncpy(buffer, subscriber_part, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        strncpy(uri_info->topic, subscriber_part, sizeof(uri_info->topic) - 1);
        
        return;
    }
    
    // Se chegou aqui, tenta efetuar o parse como URI completa ou fallback
    if (!parse_mqtt_uri_enhanced(uri, uri_info)) {
        // Fallback para comportamento original
        extract_subscriber_id(uri, buffer, buffer_size, use_tls);
        
        // Preencher uri_info com defaults
        uri_info->use_tls = *use_tls;
        uri_info->is_full_uri = false;
        strcpy(uri_info->host, "localhost");
        uri_info->port = *use_tls ? 8883 : 1883;
        strncpy(uri_info->topic, buffer, sizeof(uri_info->topic) - 1);
        return;
    }
    
    // Definir use_tls baseado no parsing
    *use_tls = uri_info->use_tls;
    
    if (uri_info->is_full_uri) {
        // Para URIs completas, o subscriber ID é extraído do tópico
        const char *topic = uri_info->topic;
        if (topic[0] == '/') {
            topic++;
        }
        
        strncpy(buffer, topic, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';

    } else {
        strncpy(buffer, uri_info->topic, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';

    }
}

// 1. Handler para GET subscription
bool handle_get_subscription(int client_socket, const char *resource_name, const char *resource_uri/*, char *response, size_t response_size*/)
{
    char response[BUFFER_SIZE] = {0};
    Subscription sub = {0};

    if (!get_subscription(resource_name, &sub))
    {   
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        
        write(client_socket, response, strlen(response));
        return false;
    }

    // Set parent ID from resource URI
    char parent_ri[128] = "";
    get_ri_from_resource_uri_aux(resource_uri, NULL, parent_ri, sizeof(parent_ri)); // Nova função, ver abaixo
    strncpy(sub.parent_id, parent_ri, sizeof(sub.parent_id) - 1);
    strncpy(sub.resource_uri, resource_uri, sizeof(sub.resource_uri) - 1);

    // Construir resposta JSON
    json_object *response_obj = json_object_new_object();
    json_object *sub_obj = json_object_new_object();

    json_object_object_add(sub_obj, "rn", json_object_new_string(sub.resource_name));
    json_object_object_add(sub_obj, "ri", json_object_new_string(sub.resource_id));
    json_object_object_add(sub_obj, "pi", json_object_new_string(sub.parent_id));
    json_object_object_add(sub_obj, "ty", json_object_new_int(23)); // subscription type
    json_object_object_add(sub_obj, "ct", json_object_new_string(sub.creation_time));
    json_object_object_add(sub_obj, "lt", json_object_new_string(sub.last_modified_time));

    // Notification URIs
    json_object *nu_array = json_object_new_array();
    json_object_array_add(nu_array, json_object_new_string(sub.notification_uris));
    json_object_object_add(sub_obj, "nu", nu_array);

    json_object_object_add(sub_obj, "nct", json_object_new_int(sub.notification_type));

    json_object_object_add(response_obj, "m2m:sub", sub_obj);

    const char *json_response = json_object_to_json_string(response_obj);


    snprintf(response, BUFFER_SIZE,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s",
            strlen(json_response), json_response);

    write(client_socket, response, strlen(response));


    json_object_put(response_obj);
    return true;
}

// 2. Handler para DELETE subscription
bool handle_delete_subscription(int client_socket, const char *resource_name)
{
    char response[BUFFER_SIZE] = {0};
    Subscription sub = {0};


    if (!delete_subscription(resource_name))
    {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        printf("[INFO] Not Found subscription\n");
        write(client_socket, response, strlen(response));
        return false;
    }

    printf("[INFO] Subscription Deleted\n");

    snprintf(response, BUFFER_SIZE,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen("Subscription deleted successfully"), "Subscription deleted successfully");
    

    write(client_socket, response, strlen(response));

    return true;
}

bool delete_subscription(const char *resource_name)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    bool result = false;

    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(db));
        return false;
    }

    const char *sql = "DELETE FROM subscriptions WHERE resource_name = ?";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    rc = sqlite3_bind_text(stmt, 1, resource_name, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_DONE)
    {
        // Check if a row was actually deleted
        int changes = sqlite3_changes(db);
        result = (changes > 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return result;
}


// Function to send notification via http
bool send_http_notification(const char *url, const char *json_payload) {
    CURL *curl;
    CURLcode res;
    bool success = false;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        // Set the URL
        curl_easy_setopt(curl, CURLOPT_URL, url);

        // Set the POST data
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);

        // Set headers: Content-Type: application/json
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set timeout (optional)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // Perform the request
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if (response_code >= 200 && response_code < 300) {
                success = true;
            } else {
                printf("[SUB] Resposta da notificação HTTP %ld do servidor\n", response_code);
            }
        } else {
            printf("[SUB] Falha ao enviar notificação HTTP: %s\n", curl_easy_strerror(res));
        }

        // Cleanup
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    } else {
        printf("[ERRO] Falha ao inicializar a biblioteca libcurl\n");
    }

    curl_global_cleanup();
    return success;
}


// Thread to send notification and not block code execution
void* send_http_notification_thread(void *arg) {
    http_args_t *args = (http_args_t*)arg;

    bool success = send_http_notification(args->url, args->payload);

    if (success) {
        printf("[HTTP] Notificação enviada via HTTP para %s\n", args->url);
    } else {
        printf("[HTTP] Falha ao enviar notificação HTTP para %s\n", args->url);
    }

    free(args->url);
    free(args->payload);
    free(args);
    return NULL;
}