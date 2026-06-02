#include "../include/schedule.h"
#include "../include/logger.h"
#include <common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <unistd.h>

/**
 * @brief Initializes the 'schedules' table in the SQLite database.
 * 
 * This function creates the table if it does not exist. 'ri' is the primary key,
 * and 'rn' is marked as UNIQUE to prevent duplicates under the same root
 * (although ideally in oneM2M it should be unique by 'pi' and 'rn', we keep 
 * global UNIQUE simplified according to the requirements).
 */
void init_schedule_table(void) {
    sqlite3 *db;
    char *err_msg = 0;
    
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        LOG_ERR("Error: Could not open DB to initialize Schedules: %s", sqlite3_errmsg(db));
        return;
    }

    const char *sql = 
        "CREATE TABLE IF NOT EXISTS schedules ("
        "ri TEXT PRIMARY KEY, "
        "rn TEXT UNIQUE NOT NULL, "
        "pi TEXT NOT NULL, "
        "ct TEXT NOT NULL, "
        "lt TEXT NOT NULL, "
        "et TEXT, "
        "sce TEXT NOT NULL"
        ");";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERR("SQL Error creating schedules table: %s", err_msg);
        sqlite3_free(err_msg);
    } else {
        LOG("[DB] 'schedules' table verified/initialized successfully.");
    }

    sqlite3_close(db);
}

// --- CRUD Routes (Phase 7.4) ---

/**
 * @brief Basic validation for a cron-like schedule element (sce).
 * Validates if the string has at least 4 spaces separating 5 time fields.
 */
static bool is_valid_cron_format(const char *sce) {
    if (!sce) return false;
    int spaces = 0;
    for (int i = 0; sce[i]; i++) {
        if (sce[i] == ' ') spaces++;
    }
    return spaces >= 4;
}

void handle_schedule_create(struct response_params *params, const char *pi, const char *body) {
    char response[BUFFER_SIZE] = {0};
    json_object *root = json_tokener_parse(body);
    
    if (!root) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"Invalid JSON\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    json_object *sch_obj, *rn_obj, *sce_obj;
    if (!json_object_object_get_ex(root, "m2m:sch", &sch_obj) ||
        !json_object_object_get_ex(sch_obj, "rn", &rn_obj) ||
        !json_object_object_get_ex(sch_obj, "sce", &sce_obj)) {
        
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"Missing mandatory fields: m2m:sch, rn or sce\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(root);
        return;
    }

    const char *rn = json_object_get_string(rn_obj);
    const char *sce = json_object_get_string(sce_obj);

    if (!is_valid_cron_format(sce)) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"Invalid sce cron format. Expected '* * * * *'\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(root);
        return;
    }

    char *ri = generate_random_sequence(); // From common.h
    
    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        free(ri);
        json_object_put(root);
        return;
    }

    const char *sql = "INSERT INTO schedules (ri, rn, pi, ct, lt, sce) VALUES (?, ?, ?, datetime('now', 'localtime'), datetime('now', 'localtime'), ?)";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, rn, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, pi ? pi : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, sce, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 403 Forbidden\r\n\r\n{\"error\":\"Schedule Name already exists under this parent\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
    } else {
        LOG("[Schedule] Created successfully with RI: %s", ri);
        snprintf(response, BUFFER_SIZE, 
            "HTTP/1.1 201 Created\r\n"
            "Content-Type: application/json\r\n\r\n"
            "{\"m2m:sch\":{\"ri\":\"%s\",\"rn\":\"%s\",\"pi\":\"%s\",\"sce\":\"%s\"}}", 
            ri, rn, pi ? pi : "", sce);
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    free(ri);
    json_object_put(root);
}

void handle_schedule_retrieve(struct response_params *params, const char *identifier) {
    char response[BUFFER_SIZE] = {0};
    sqlite3 *db;
    
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    const char *sql = "SELECT ri, rn, pi, ct, lt, et, sce FROM schedules WHERE ri = ? OR rn = ?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, identifier, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, identifier, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *ri = (const char *)sqlite3_column_text(stmt, 0);
        const char *rn = (const char *)sqlite3_column_text(stmt, 1);
        const char *pi = (const char *)sqlite3_column_text(stmt, 2);
        const char *ct = (const char *)sqlite3_column_text(stmt, 3);
        const char *lt = (const char *)sqlite3_column_text(stmt, 4);
        const char *et = (const char *)sqlite3_column_text(stmt, 5);
        const char *sce = (const char *)sqlite3_column_text(stmt, 6);

        json_object *resp_obj = json_object_new_object();
        json_object *sch_obj = json_object_new_object();
        
        json_object_object_add(sch_obj, "ri", json_object_new_string(ri));
        json_object_object_add(sch_obj, "rn", json_object_new_string(rn));
        json_object_object_add(sch_obj, "pi", json_object_new_string(pi));
        json_object_object_add(sch_obj, "ct", json_object_new_string(ct));
        json_object_object_add(sch_obj, "lt", json_object_new_string(lt));
        if (et) json_object_object_add(sch_obj, "et", json_object_new_string(et));
        json_object_object_add(sch_obj, "sce", json_object_new_string(sce));
        
        json_object_object_add(resp_obj, "m2m:sch", sch_obj);

        const char *json_str = json_object_to_json_string(resp_obj);
        snprintf(response, BUFFER_SIZE, 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n\r\n"
            "%s", json_str);
            
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(resp_obj);
    } else {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void handle_schedule_update(struct response_params *params, const char *identifier) {
    char response[BUFFER_SIZE] = {0};
    sqlite3 *db;
    
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    // Currently handling the required PUT logic: Update 'lt' to reflect changes
    const char *sql = "UPDATE schedules SET lt = datetime('now', 'localtime') WHERE ri = ? OR rn = ?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, identifier, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, identifier, -1, SQLITE_STATIC);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (changes > 0) {
        LOG("[Schedule] Updated successfully: %s", identifier);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"message\":\"Updated lt successfully\"}");
    } else {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
    }
    
    if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
}

void handle_schedule_delete(struct response_params *params, const char *identifier) {
    char response[BUFFER_SIZE] = {0};
    sqlite3 *db;
    
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    const char *sql = "DELETE FROM schedules WHERE ri = ? OR rn = ?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, identifier, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, identifier, -1, SQLITE_STATIC);

    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (changes > 0) {
        LOG("[Schedule] Deleted successfully: %s", identifier);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"message\":\"Deleted successfully\"}");
    } else {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
    }
    
    if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
}

// --- Skeleton for Background Scheduling logic (Phase 7.5) ---