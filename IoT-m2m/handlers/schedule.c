#include "../include/schedule.h"
#include "../include/logger.h"
#include <common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/* schedules table is created by database/create_tables.sql — no runtime DDL needed. */
void init_schedule_table(void) { }

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

/* Schedule type number in oneM2M (TS-0004 ty=29) */
#define SCHEDULE_RESOURCE_TYPE 29

void handle_schedule_create(struct response_params *params, const char *pi, const char *body) {
    char response[BUFFER_SIZE] = {0};
    json_object *root = json_tokener_parse(body);

    if (!root) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Invalid JSON\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    json_object *sch_obj, *rn_obj, *sce_obj;
    if (!json_object_object_get_ex(root, "m2m:sch", &sch_obj) ||
        !json_object_object_get_ex(sch_obj, "rn", &rn_obj) ||
        !json_object_object_get_ex(sch_obj, "sce", &sce_obj)) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Missing mandatory fields: m2m:sch, rn or sce\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(root);
        return;
    }

    const char *rn  = json_object_get_string(rn_obj);
    const char *sce = json_object_get_string(sce_obj);

    if (!is_valid_cron_format(sce)) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Invalid sce cron format. Expected '* * * * *'\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(root);
        return;
    }

    /* Resolve parent AE name → ri so pi stored in resources is always an ri. */
    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(root);
        return;
    }

    /* Look up the parent resource by rn or ri */
    char pi_ri[64] = {0};
    {
        sqlite3_stmt *s;
        const char *q = "SELECT ri FROM resources WHERE rn = ? OR ri = ? LIMIT 1";
        if (sqlite3_prepare_v2(db, q, -1, &s, NULL) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, pi ? pi : "", -1, SQLITE_STATIC);
            sqlite3_bind_text(s, 2, pi ? pi : "", -1, SQLITE_STATIC);
            if (sqlite3_step(s) == SQLITE_ROW) {
                const char *r = (const char *)sqlite3_column_text(s, 0);
                strncpy(pi_ri, r ? r : "", sizeof(pi_ri) - 1);
            }
            sqlite3_finalize(s);
        }
    }
    if (pi_ri[0] == '\0') {
        sqlite3_close(db);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Parent resource not found\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(root);
        return;
    }

    char *ri = generate_random_sequence();
    if (!ri) {
        sqlite3_close(db);
        json_object_put(root);
        return;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    /* Insert into shared resources table */
    sqlite3_stmt *stmt;
    const char *sql_res =
        "INSERT INTO resources (ty, ri, rn, pi, ct, lt) "
        "VALUES (?, ?, ?, ?, datetime('now','localtime'), datetime('now','localtime'))";
    int rc = SQLITE_ERROR;
    if (sqlite3_prepare_v2(db, sql_res, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int (stmt, 1, SCHEDULE_RESOURCE_TYPE);
        sqlite3_bind_text(stmt, 2, ri,    -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, rn,    -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, pi_ri, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    if (rc != SQLITE_DONE) {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        free(ri);
        json_object_put(root);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Schedule name already exists\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    /* Insert into schedules table (schedule-specific fields only) */
    const char *sql_sch = "INSERT INTO schedules (ri, sce) VALUES (?, ?)";
    rc = SQLITE_ERROR;
    if (sqlite3_prepare_v2(db, sql_sch, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, ri,  -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, sce, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    if (rc != SQLITE_DONE) {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        free(ri);
        json_object_put(root);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
    sqlite3_close(db);

    LOG("[Schedule] Created successfully with RI: %s", ri);
    snprintf(response, BUFFER_SIZE,
        "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\n\r\n"
        "{\"m2m:sch\":{\"ri\":\"%s\",\"rn\":\"%s\",\"pi\":\"%s\",\"sce\":\"%s\"}}",
        ri, rn, pi_ri, sce);
    if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
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

    const char *sql =
        "SELECT r.ri, r.rn, r.pi, r.ct, r.lt, s.et, s.sce "
        "FROM resources r JOIN schedules s ON r.ri = s.ri "
        "WHERE r.ri = ? OR r.rn = ?";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, identifier, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, identifier, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *ri  = (const char *)sqlite3_column_text(stmt, 0);
        const char *rn  = (const char *)sqlite3_column_text(stmt, 1);
        const char *pi  = (const char *)sqlite3_column_text(stmt, 2);
        const char *ct  = (const char *)sqlite3_column_text(stmt, 3);
        const char *lt  = (const char *)sqlite3_column_text(stmt, 4);
        const char *et  = (const char *)sqlite3_column_text(stmt, 5);
        const char *sce = (const char *)sqlite3_column_text(stmt, 6);

        json_object *resp_obj = json_object_new_object();
        json_object *sch_obj  = json_object_new_object();
        json_object_object_add(sch_obj, "ri",  json_object_new_string(ri));
        json_object_object_add(sch_obj, "rn",  json_object_new_string(rn));
        json_object_object_add(sch_obj, "pi",  json_object_new_string(pi));
        json_object_object_add(sch_obj, "ct",  json_object_new_string(ct));
        json_object_object_add(sch_obj, "lt",  json_object_new_string(lt));
        if (et) json_object_object_add(sch_obj, "et", json_object_new_string(et));
        json_object_object_add(sch_obj, "sce", json_object_new_string(sce));
        json_object_object_add(resp_obj, "m2m:sch", sch_obj);

        const char *json_str = json_object_to_json_string(resp_obj);
        snprintf(response, BUFFER_SIZE,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json_str);
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        json_object_put(resp_obj);
    } else {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

void handle_schedule_update(struct response_params *params, const char *identifier, const char *body) {
    char response[BUFFER_SIZE] = {0};

    /* Parse optional new sce from body: {"m2m:sch":{"sce":"..."}} */
    const char *new_sce = NULL;
    json_object *root = body ? json_tokener_parse(body) : NULL;
    if (root) {
        json_object *sch_obj, *sce_obj;
        if (json_object_object_get_ex(root, "m2m:sch", &sch_obj) &&
            json_object_object_get_ex(sch_obj, "sce", &sce_obj)) {
            new_sce = json_object_get_string(sce_obj);
        }
    }

    if (new_sce && !is_valid_cron_format(new_sce)) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Invalid sce cron format. Expected '* * * * *'\"}");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        if (root) json_object_put(root);
        return;
    }

    sqlite3 *db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        if (root) json_object_put(root);
        return;
    }

    /* Resolve identifier → ri (may be rn or ri) */
    char ri[64] = {0};
    {
        sqlite3_stmt *s;
        const char *q = "SELECT r.ri FROM resources r JOIN schedules sch ON r.ri = sch.ri WHERE r.ri = ? OR r.rn = ? LIMIT 1";
        if (sqlite3_prepare_v2(db, q, -1, &s, NULL) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, identifier, -1, SQLITE_STATIC);
            sqlite3_bind_text(s, 2, identifier, -1, SQLITE_STATIC);
            if (sqlite3_step(s) == SQLITE_ROW) {
                const char *r = (const char *)sqlite3_column_text(s, 0);
                strncpy(ri, r ? r : "", sizeof(ri) - 1);
            }
            sqlite3_finalize(s);
        }
    }
    if (ri[0] == '\0') {
        sqlite3_close(db);
        if (root) json_object_put(root);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    /* Always update lt in resources */
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "UPDATE resources SET lt = datetime('now','localtime') WHERE ri = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* Update sce in schedules if provided */
    if (new_sce) {
        sqlite3_prepare_v2(db, "UPDATE schedules SET sce = ? WHERE ri = ?", -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, new_sce, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, ri,      -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
    sqlite3_close(db);

    LOG("[Schedule] Updated successfully: %s", identifier);
    snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"message\":\"Updated successfully\"}");
    if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
    if (root) json_object_put(root);
}

void handle_schedule_delete(struct response_params *params, const char *identifier) {
    char response[BUFFER_SIZE] = {0};
    sqlite3 *db;

    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    /* Resolve identifier → ri */
    char ri[64] = {0};
    {
        sqlite3_stmt *s;
        const char *q = "SELECT r.ri FROM resources r JOIN schedules sch ON r.ri = sch.ri WHERE r.ri = ? OR r.rn = ? LIMIT 1";
        if (sqlite3_prepare_v2(db, q, -1, &s, NULL) == SQLITE_OK) {
            sqlite3_bind_text(s, 1, identifier, -1, SQLITE_STATIC);
            sqlite3_bind_text(s, 2, identifier, -1, SQLITE_STATIC);
            if (sqlite3_step(s) == SQLITE_ROW) {
                const char *r = (const char *)sqlite3_column_text(s, 0);
                strncpy(ri, r ? r : "", sizeof(ri) - 1);
            }
            sqlite3_finalize(s);
        }
    }
    if (ri[0] == '\0') {
        sqlite3_close(db);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
        return;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    sqlite3_stmt *stmt;
    /* Delete child row first (FK child before parent) */
    sqlite3_prepare_v2(db, "DELETE FROM schedules WHERE ri = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* Delete from resources */
    sqlite3_prepare_v2(db, "DELETE FROM resources WHERE ri = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
    sqlite3_close(db);

    if (changes > 0) {
        LOG("[Schedule] Deleted successfully: %s", identifier);
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"message\":\"Deleted successfully\"}");
    } else {
        snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
    }
    if (params->protocol && strcmp(params->protocol, "HTTP") == 0) write(params->http_socket, response, strlen(response));
}

// --- Background Scheduling logic (Phase 7.5) ---

/*
 * The scheduler fires actions through check_and_trigger_actions(), whose single
 * definition lives in the action module (handlers/action.c). We only need its
 * prototype here to call it; the linker resolves it against action.o.
 */
void check_and_trigger_actions(void);

static pthread_t       scheduler_tid;
static pthread_mutex_t scheduler_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  scheduler_cond = PTHREAD_COND_INITIALIZER;
static volatile int    scheduler_active = 0;

/* Match one cron field against a calendar value.
 * Supports: * | n | n-m | *\/step | n-m/step | comma-separated list of any above. */
static bool match_field(const char *field, int value) {
    if (strcmp(field, "*") == 0) return true;

    char buf[64];
    strncpy(buf, field, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok) {
        char *slash = strchr(tok, '/');
        int step = 1;
        if (slash) {
            step = atoi(slash + 1);
            if (step < 1) step = 1;
            *slash = '\0';
        }

        int lo, hi;
        char *dash = strchr(tok, '-');
        if (*tok == '*') {
            lo = 0; hi = 59;
        } else if (dash) {
            lo = atoi(tok);
            hi = atoi(dash + 1);
        } else {
            lo = hi = atoi(tok);
        }

        if (value >= lo && value <= hi && (value - lo) % step == 0)
            return true;

        tok = strtok(NULL, ",");
    }
    return false;
}

bool evaluate_schedule(const char *sce) {
    if (!sce) return false;

    char f[5][16];
    if (sscanf(sce, "%15s %15s %15s %15s %15s",
               f[0], f[1], f[2], f[3], f[4]) != 5)
        return false;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    /* cron field order: minute  hour  day-of-month  month  day-of-week */
    return match_field(f[0], t->tm_min)       /* 0-59  */
        && match_field(f[1], t->tm_hour)      /* 0-23  */
        && match_field(f[2], t->tm_mday)      /* 1-31  */
        && match_field(f[3], t->tm_mon + 1)   /* 1-12  */
        && match_field(f[4], t->tm_wday);     /* 0-6   */
}

static void *scheduler_thread_func(void *arg) {
    (void)arg;
    LOG("[Scheduler] Background thread started.");

    while (1) {
        /* Query every active schedule and note whether any matches now.
         * We must NOT call check_and_trigger_actions() while this read cursor
         * is open: the open SELECT holds a SHARED lock on the database
         */
        int matched = 0;
        sqlite3 *db;
        if (sqlite3_open(DB_PATH, &db) == SQLITE_OK) {
            sqlite3_stmt *stmt;
            const char *sql = "SELECT sce FROM schedules";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char *sce = (const char *)sqlite3_column_text(stmt, 0);
                    if (evaluate_schedule(sce)) {
                        LOG("[Scheduler] Schedule matched ('%s') — triggering actions.", sce);
                        matched = 1;
                    }
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
        }

        /* Cursor released — now it is safe for the action handler to write.
         * check_and_trigger_actions() re-evaluates the schedules itself, so a
         * single call per tick is enough. */
        if (matched)
            check_and_trigger_actions();

        /* Wait 60 s (cron granularity) or wake immediately on stop signal. */
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 60;

        pthread_mutex_lock(&scheduler_mtx);
        if (!scheduler_active) {
            pthread_mutex_unlock(&scheduler_mtx);
            break;
        }
        pthread_cond_timedwait(&scheduler_cond, &scheduler_mtx, &deadline);
        int still_active = scheduler_active;
        pthread_mutex_unlock(&scheduler_mtx);

        if (!still_active) break;
    }

    LOG("[Scheduler] Background thread exiting.");
    return NULL;
}

void start_scheduler_thread(void) {
    pthread_mutex_lock(&scheduler_mtx);
    scheduler_active = 1;
    pthread_mutex_unlock(&scheduler_mtx);

    if (pthread_create(&scheduler_tid, NULL, scheduler_thread_func, NULL) != 0) {
        LOG_ERROR("[Scheduler] Failed to create background thread.");
        pthread_mutex_lock(&scheduler_mtx);
        scheduler_active = 0;
        pthread_mutex_unlock(&scheduler_mtx);
        return;
    }
    LOG("[Scheduler] Thread created.");
}

void stop_scheduler_thread(void) {
    pthread_mutex_lock(&scheduler_mtx);
    if (!scheduler_active) {
        pthread_mutex_unlock(&scheduler_mtx);
        return;
    }
    scheduler_active = 0;
    pthread_cond_signal(&scheduler_cond);   /* wake thread immediately */
    pthread_mutex_unlock(&scheduler_mtx);

    pthread_join(scheduler_tid, NULL);
    LOG("[Scheduler] Thread joined cleanly.");
}