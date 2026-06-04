/*
AUTHORSHIP -------------------------------------------------

 * File:    action.c
 * Authors: Bruno Neto
 * Date:    May 2025
 * Description: Contains functions for Action.

*/
//
// Created by bmpne on 02/06/2026.
//

#include "../include/action.h"
#include "../include/container.h"
#include <sqlite3.h>
#include <json-c/json.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Helper: get ri from resource name using an already-open db connection
static int get_ri_for_rn(sqlite3 *db, const char *rn, char *out_ri, size_t out_size)
{
    if (!db || !rn || !out_ri) return 0;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT ri FROM resources WHERE rn = ? LIMIT 1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *ri = (const char *)sqlite3_column_text(stmt, 0);
        strncpy(out_ri, ri ? ri : "", out_size - 1);
        out_ri[out_size-1] = '\0';
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}
void handle_request_action_post(struct response_params *params,
                                const char *csebase,
                                const char *ae,
                                const char *container,
                                const char *body)
{
    if (!params || !csebase || !ae || !container || !body)
    {
        if (params) send_response(params, 400, "{\"error\":\"Invalid request parameters.\"}");
        return;
    }

    char resource_uri[512] = {0};
    char response_local[4096] = {0};
    snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s", csebase, ae, container);

    if (handle_action_request(body, resource_uri, "CAdmin", response_local, sizeof(response_local), params->http_socket))
    {
        send_response(params, 201, response_local);
        return;
    }

    if (response_local[0] == '\0')
    {
        send_response(params, 500, "");
        return;
    }

    if (strstr(response_local, "required") ||
        strstr(response_local, "not found") ||
        strstr(response_local, "Invalid") ||
        strstr(response_local, "invalid") ||
        strstr(response_local, "must") ||
        strstr(response_local, "formatted") ||
        strstr(response_local, "already exists") ||
        strstr(response_local, "cannot be empty") ||
        strstr(response_local, "invalid characters") ||
        strstr(response_local, "valid future datetime") ||
        strstr(response_local, "contains invalid"))
    {
        send_response(params, 422, response_local);
    }
    else
    {
        send_response(params, 500, response_local);
    }
}

/* ======================================================================
 * Shared helpers for action CRUD/wrappers
 * ====================================================================== */

static void action_init(Action *action)
{
    if (!action) return;
    memset(action, 0, sizeof(*action));
    action->action_priority = -1;
    action->eval_mode = -1;
    action->eval_control_param = -1;
    action->eval_criteria.op = (EvalOperator)-1;
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static bool get_last_uri_segment(const char *resource_uri, char *out, size_t out_size)
{
    if (!resource_uri || !out || out_size == 0) return false;

    const char *last_slash = strrchr(resource_uri, '/');
    const char *segment = last_slash ? last_slash + 1 : resource_uri;
    if (!segment || *segment == '\0') return false;

    copy_text(out, out_size, segment);
    return true;
}

static bool resource_exists(sqlite3 *db, const char *column, const char *value)
{
    if (!db || !column || !value) return false;

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT 1 FROM resources WHERE %s = ? LIMIT 1", column);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_STATIC);

    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static bool generate_unique_value(sqlite3 *db, const char *column, char *out, size_t out_size)
{
    if (!db || !column || !out || out_size == 0) return false;

    for (int attempts = 0; attempts < 64; ++attempts)
    {
        char *candidate = generate_random_sequence();
        if (!candidate) return false;

        bool exists = resource_exists(db, column, candidate);
        if (!exists)
        {
            copy_text(out, out_size, candidate);
            free(candidate);
            return true;
        }

        free(candidate);
    }
    return false;
}

static bool fetch_action_from_db(sqlite3 *db, const char *action_name, Action *action)
{
    if (!db || !action_name || !action) return false;

    action_init(action);

    const char *sql =
        "SELECT r.ri, r.rn, r.pi, r.ct, r.lt, a.et, a.subject_resource_id, "
        "a.eval_criteria_subject, a.eval_criteria_operator, a.eval_criteria_threshold, "
        "a.eval_mode, a.eval_control_param, a.object_resource_id, a.action_primitive, "
        "a.input, a.action_result, a.action_priority "
        "FROM resources r JOIN actions a ON r.ri = a.ri "
        "WHERE r.rn = ? AND r.ty = ? LIMIT 1";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, action_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, ACTION_RESOURCE_TYPE);

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        copy_text(action->ri, sizeof(action->ri), (const char *)sqlite3_column_text(stmt, 0));
        copy_text(action->rn, sizeof(action->rn), (const char *)sqlite3_column_text(stmt, 1));
        copy_text(action->pi, sizeof(action->pi), (const char *)sqlite3_column_text(stmt, 2));
        copy_text(action->ct, sizeof(action->ct), (const char *)sqlite3_column_text(stmt, 3));
        copy_text(action->lt, sizeof(action->lt), (const char *)sqlite3_column_text(stmt, 4));
        copy_text(action->et, sizeof(action->et), (const char *)sqlite3_column_text(stmt, 5));
        copy_text(action->subject_resource_id, sizeof(action->subject_resource_id), (const char *)sqlite3_column_text(stmt, 6));
        copy_text(action->eval_criteria.subject, sizeof(action->eval_criteria.subject), (const char *)sqlite3_column_text(stmt, 7));
        action->eval_criteria.op = (EvalOperator)sqlite3_column_int(stmt, 8);
        copy_text(action->eval_criteria.threshold, sizeof(action->eval_criteria.threshold), (const char *)sqlite3_column_text(stmt, 9));
        action->eval_mode = sqlite3_column_int(stmt, 10);
        action->eval_control_param = sqlite3_column_type(stmt, 11) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 11);
        copy_text(action->object_resource_id, sizeof(action->object_resource_id), (const char *)sqlite3_column_text(stmt, 12));
        copy_text(action->action_primitive, sizeof(action->action_primitive), (const char *)sqlite3_column_text(stmt, 13));
        copy_text(action->input, sizeof(action->input), (const char *)sqlite3_column_text(stmt, 14));
        copy_text(action->action_result, sizeof(action->action_result), (const char *)sqlite3_column_text(stmt, 15));
        action->action_priority = sqlite3_column_type(stmt, 16) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 16);
        ok = true;
    }

    sqlite3_finalize(stmt);
    return ok;
}

static char *action_to_json(const Action *action)
{
    if (!action) return NULL;

    json_object *root = json_object_new_object();
    json_object *obj = json_object_new_object();

    json_object_object_add(obj, "ri", json_object_new_string(action->ri));
    json_object_object_add(obj, "rn", json_object_new_string(action->rn));
    json_object_object_add(obj, "pi", json_object_new_string(action->pi));
    json_object_object_add(obj, "ty", json_object_new_int(ACTION_RESOURCE_TYPE));
    json_object_object_add(obj, "ct", json_object_new_string(action->ct));
    json_object_object_add(obj, "lt", json_object_new_string(action->lt));

    if (action->et[0] != '\0') json_object_object_add(obj, "et", json_object_new_string(action->et));
    if (action->subject_resource_id[0] != '\0') json_object_object_add(obj, "sri", json_object_new_string(action->subject_resource_id));
    if (action->object_resource_id[0] != '\0') json_object_object_add(obj, "ori", json_object_new_string(action->object_resource_id));
    if (action->action_primitive[0] != '\0') json_object_object_add(obj, "acp", json_object_new_string(action->action_primitive));
    if (action->input[0] != '\0') json_object_object_add(obj, "inp", json_object_new_string(action->input));
    if (action->action_priority >= 0) json_object_object_add(obj, "apri", json_object_new_int(action->action_priority));

    json_object *evc = json_object_new_object();
    if (action->eval_criteria.subject[0] != '\0') json_object_object_add(evc, "sus", json_object_new_string(action->eval_criteria.subject));
    if (action->eval_criteria.op >= 0) json_object_object_add(evc, "optr", json_object_new_int(action->eval_criteria.op));
    if (action->eval_criteria.threshold[0] != '\0') json_object_object_add(evc, "thr", json_object_new_string(action->eval_criteria.threshold));
    json_object_object_add(obj, "evc", evc);

    json_object_object_add(obj, "evm", json_object_new_int(action->eval_mode));
    if (action->eval_control_param >= 0) json_object_object_add(obj, "evcp", json_object_new_int(action->eval_control_param));
    if (action->action_result[0] != '\0') json_object_object_add(obj, "result", json_object_new_string(action->action_result));

    json_object_object_add(root, "m2m:act", obj);

    const char *json_text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    char *out = json_text ? strdup(json_text) : NULL;
    json_object_put(root);
    return out;
}

static bool action_parse_body(const char *body, Action *action, char *error, size_t error_size)
{
    if (!body || !action) return false;

    action_init(action);
    json_object *root = json_tokener_parse(body);
    if (!root)
    {
        snprintf(error, error_size, "{\"error\":\"JSON body incorrectly formatted.\"}");
        return false;
    }

    json_object *act_obj = NULL;
    if (!json_object_object_get_ex(root, "m2m:act", &act_obj) &&
        !json_object_object_get_ex(root, "m2m:acn", &act_obj))
    {
        snprintf(error, error_size, "{\"error\":\"'m2m:act' object not found in JSON.\"}");
        json_object_put(root);
        return false;
    }

    json_object_object_foreach(act_obj, key, val)
    {
        if (strcmp(key, "ri") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(action->ri, sizeof(action->ri), json_object_get_string(val));
        }
        else if (strcmp(key, "rn") == 0)
        {
            copy_text(action->rn, sizeof(action->rn), json_object_get_string(val));
            if (strlen(action->rn) == 0)
            {
                snprintf(error, error_size, "{\"error\":\"rn value cannot be empty.\"}");
                json_object_put(root);
                return false;
            }
            if (!is_valid_string_plus_three_extra_chars(action->rn))
            {
                snprintf(error, error_size, "{\"error\":\"rn contains invalid characters.\"}");
                json_object_put(root);
                return false;
            }
        }
        else if (strcmp(key, "et") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(action->et, sizeof(action->et), json_object_get_string(val));
            if (!is_valid_datetime_format(action->et) || !is_valid_datetime(action->et) || !is_future_datetime(action->et))
            {
                snprintf(error, error_size, "{\"error\":\"et is not a valid future datetime in 'YYYY-MM-DD HH:MM:SS' format.\"}");
                json_object_put(root);
                return false;
            }
        }
        else if (strcmp(key, "sri") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(action->subject_resource_id, sizeof(action->subject_resource_id), json_object_get_string(val));
        }
        else if (strcmp(key, "ori") == 0)
        {
            copy_text(action->object_resource_id, sizeof(action->object_resource_id), json_object_get_string(val));
        }
        else if (strcmp(key, "acp") == 0)
        {
            copy_text(action->action_primitive, sizeof(action->action_primitive), json_object_get_string(val));
        }
        else if (strcmp(key, "inp") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(action->input, sizeof(action->input), json_object_get_string(val));
        }
        else if (strcmp(key, "evm") == 0)
        {
            if (!json_object_is_type(val, json_type_int))
            {
                snprintf(error, error_size, "{\"error\":\"evm (evalMode) must be an integer.\"}");
                json_object_put(root);
                return false;
            }
            action->eval_mode = json_object_get_int(val);
        }
        else if (strcmp(key, "evcp") == 0)
        {
            if (!json_object_is_type(val, json_type_int))
            {
                snprintf(error, error_size, "{\"error\":\"evcp (evalControlParam) must be an integer.\"}");
                json_object_put(root);
                return false;
            }
            action->eval_control_param = json_object_get_int(val);
        }
        else if (strcmp(key, "apri") == 0)
        {
            if (!json_object_is_type(val, json_type_int))
            {
                snprintf(error, error_size, "{\"error\":\"apri (actionPriority) must be an integer.\"}");
                json_object_put(root);
                return false;
            }
            action->action_priority = json_object_get_int(val);
        }
        else if (strcmp(key, "evc") == 0)
        {
            if (!json_object_is_type(val, json_type_object))
            {
                snprintf(error, error_size, "{\"error\":\"evc (evalCriteria) must be a JSON object.\"}");
                json_object_put(root);
                return false;
            }

            json_object *sus_obj = NULL, *optr_obj = NULL, *thr_obj = NULL;
            if (json_object_object_get_ex(val, "sus", &sus_obj))
                copy_text(action->eval_criteria.subject, sizeof(action->eval_criteria.subject), json_object_get_string(sus_obj));
            if (json_object_object_get_ex(val, "optr", &optr_obj))
            {
                if (!json_object_is_type(optr_obj, json_type_int))
                {
                    snprintf(error, error_size, "{\"error\":\"evc.optr must be an integer (0-6).\"}");
                    json_object_put(root);
                    return false;
                }
                action->eval_criteria.op = (EvalOperator)json_object_get_int(optr_obj);
            }
            if (json_object_object_get_ex(val, "thr", &thr_obj))
                copy_text(action->eval_criteria.threshold, sizeof(action->eval_criteria.threshold), json_object_get_string(thr_obj));
        }
    }

    json_object_put(root);

    if (action->eval_criteria.subject[0] == '\0' || action->eval_criteria.op < 0 || action->eval_criteria.threshold[0] == '\0')
    {
        snprintf(error, error_size, "{\"error\":\"evc (evalCriteria) with sus, optr, and thr is required.\"}");
        return false;
    }
    if (action->eval_mode < 0)
    {
        snprintf(error, error_size, "{\"error\":\"evm (evalMode) is required.\"}");
        return false;
    }
    if (action->object_resource_id[0] == '\0')
    {
        snprintf(error, error_size, "{\"error\":\"ori (objectResourceID) is required.\"}");
        return false;
    }
    if (action->action_primitive[0] == '\0')
    {
        snprintf(error, error_size, "{\"error\":\"acp (actionPrimitive) is required.\"}");
        return false;
    }

    if ((action->eval_mode == EVAL_MODE_PERIODIC || action->eval_mode == EVAL_MODE_CONTINUOUS) && action->eval_control_param <= 0)
    {
        snprintf(error, error_size, "{\"error\":\"evcp is required when evm is periodic (2) or continuous (3).\"}");
        return false;
    }
    if ((action->eval_mode == EVAL_MODE_OFF || action->eval_mode == EVAL_MODE_ONCE) && action->eval_control_param > 0)
    {
        snprintf(error, error_size, "{\"error\":\"evcp must not be set when evm is off (0) or once (1).\"}");
        return false;
    }

    return true;
}

static bool action_insert_into_db(const char *container_name, const char *resource_uri, Action *action, char *error, size_t error_size)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }

    char container_ri[256] = {0};
    if (!get_ri_for_rn(db, container_name, container_ri, sizeof(container_ri)))
    {
        sqlite3_close(db);
        snprintf(error, error_size, "{\"error\":\"Parent container not found.\"}");
        return false;
    }

    if (action->ri[0] == '\0' && !generate_unique_value(db, "ri", action->ri, sizeof(action->ri)))
    {
        sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }
    if (action->rn[0] == '\0' && !generate_unique_value(db, "rn", action->rn, sizeof(action->rn)))
    {
        sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }

    if (resource_exists(db, "ri", action->ri))
    {
        sqlite3_close(db);
        snprintf(error, error_size, "{\"error\":\"ri already exists.\"}");
        return false;
    }
    if (resource_exists(db, "rn", action->rn))
    {
        sqlite3_close(db);
        snprintf(error, error_size, "{\"error\":\"rn already exists.\"}");
        return false;
    }

    if (action->subject_resource_id[0] == '\0')
        snprintf(action->subject_resource_id, sizeof(action->subject_resource_id), "%s", resource_uri);

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    const char *sql_res =
        "INSERT INTO resources (ty, ri, rn, pi, ct, lt) "
        "VALUES (?, ?, ?, ?, datetime('now', 'localtime'), datetime('now', 'localtime'))";
    if (sqlite3_prepare_v2(db, sql_res, -1, &stmt, NULL) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }
    sqlite3_bind_int(stmt, 1, ACTION_RESOURCE_TYPE);
    sqlite3_bind_text(stmt, 2, action->ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, action->rn, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, container_ri, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }

    const char *sql_acn =
        "INSERT INTO actions (ri, et, subject_resource_id, eval_criteria_subject, eval_criteria_operator, eval_criteria_threshold, eval_mode, eval_control_param, object_resource_id, action_primitive, input, action_result, action_priority) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?)";
    if (sqlite3_prepare_v2(db, sql_acn, -1, &stmt, NULL) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }

    sqlite3_bind_text(stmt, 1, action->ri, -1, SQLITE_STATIC);
    if (action->et[0] != '\0') sqlite3_bind_text(stmt, 2, action->et, -1, SQLITE_STATIC); else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, action->subject_resource_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, action->eval_criteria.subject, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, action->eval_criteria.op);
    sqlite3_bind_text(stmt, 6, action->eval_criteria.threshold, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, action->eval_mode);
    if (action->eval_control_param > 0) sqlite3_bind_int(stmt, 8, action->eval_control_param); else sqlite3_bind_null(stmt, 8);
    sqlite3_bind_text(stmt, 9, action->object_resource_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, action->action_primitive, -1, SQLITE_STATIC);
    if (action->input[0] != '\0') sqlite3_bind_text(stmt, 11, action->input, -1, SQLITE_STATIC); else sqlite3_bind_null(stmt, 11);
    if (action->action_priority >= 0) sqlite3_bind_int(stmt, 12, action->action_priority); else sqlite3_bind_null(stmt, 12);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        snprintf(error, error_size, "");
        return false;
    }

    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);

    Action stored;
    bool ok = fetch_action_from_db(db, action->rn, &stored);
    sqlite3_close(db);
    if (ok) *action = stored;
    return ok;
}

static bool action_delete_by_name(const char *action_name)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return false;
    }

    Action existing;
    if (!fetch_action_from_db(db, action_name, &existing))
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    if (sqlite3_prepare_v2(db, "DELETE FROM actions WHERE ri = ?", -1, &stmt, NULL) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_text(stmt, 1, existing.ri, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        return false;
    }

    if (sqlite3_prepare_v2(db, "DELETE FROM resources WHERE ri = ?", -1, &stmt, NULL) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_text(stmt, 1, existing.ri, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
    sqlite3_close(db);
    return true;
}

static bool action_update_by_name(const char *action_name, const char *body, Action *updated)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        return false;
    }

    Action current;
    if (!fetch_action_from_db(db, action_name, &current))
    {
        sqlite3_close(db);
        return false;
    }

    json_object *root = json_tokener_parse(body);
    if (!root)
    {
        sqlite3_close(db);
        return false;
    }

    json_object *patch_obj = NULL;
    if (!json_object_object_get_ex(root, "m2m:act", &patch_obj) &&
        !json_object_object_get_ex(root, "m2m:acn", &patch_obj))
    {
        json_object_put(root);
        sqlite3_close(db);
        return false;
    }

    Action patch;
    action_init(&patch);
    json_object_object_foreach(patch_obj, key, val)
    {
        if (strcmp(key, "et") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(patch.et, sizeof(patch.et), json_object_get_string(val));
        }
        else if (strcmp(key, "sri") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(patch.subject_resource_id, sizeof(patch.subject_resource_id), json_object_get_string(val));
        }
        else if (strcmp(key, "ori") == 0)
        {
            copy_text(patch.object_resource_id, sizeof(patch.object_resource_id), json_object_get_string(val));
        }
        else if (strcmp(key, "acp") == 0)
        {
            copy_text(patch.action_primitive, sizeof(patch.action_primitive), json_object_get_string(val));
        }
        else if (strcmp(key, "inp") == 0 && !json_object_is_type(val, json_type_null))
        {
            copy_text(patch.input, sizeof(patch.input), json_object_get_string(val));
        }
        else if (strcmp(key, "evm") == 0 && json_object_is_type(val, json_type_int))
        {
            patch.eval_mode = json_object_get_int(val);
        }
        else if (strcmp(key, "evcp") == 0 && json_object_is_type(val, json_type_int))
        {
            patch.eval_control_param = json_object_get_int(val);
        }
        else if (strcmp(key, "apri") == 0 && json_object_is_type(val, json_type_int))
        {
            patch.action_priority = json_object_get_int(val);
        }
        else if (strcmp(key, "evc") == 0 && json_object_is_type(val, json_type_object))
        {
            json_object *sus_obj = NULL, *optr_obj = NULL, *thr_obj = NULL;
            if (json_object_object_get_ex(val, "sus", &sus_obj))
                copy_text(patch.eval_criteria.subject, sizeof(patch.eval_criteria.subject), json_object_get_string(sus_obj));
            if (json_object_object_get_ex(val, "optr", &optr_obj) && json_object_is_type(optr_obj, json_type_int))
                patch.eval_criteria.op = (EvalOperator)json_object_get_int(optr_obj);
            if (json_object_object_get_ex(val, "thr", &thr_obj))
                copy_text(patch.eval_criteria.threshold, sizeof(patch.eval_criteria.threshold), json_object_get_string(thr_obj));
        }
    }
    json_object_put(root);

    if (patch.et[0] != '\0') copy_text(current.et, sizeof(current.et), patch.et);
    if (patch.subject_resource_id[0] != '\0') copy_text(current.subject_resource_id, sizeof(current.subject_resource_id), patch.subject_resource_id);
    if (patch.eval_criteria.subject[0] != '\0') copy_text(current.eval_criteria.subject, sizeof(current.eval_criteria.subject), patch.eval_criteria.subject);
    if (patch.eval_criteria.op >= 0) current.eval_criteria.op = patch.eval_criteria.op;
    if (patch.eval_criteria.threshold[0] != '\0') copy_text(current.eval_criteria.threshold, sizeof(current.eval_criteria.threshold), patch.eval_criteria.threshold);
    if (patch.eval_mode >= 0) current.eval_mode = patch.eval_mode;
    if (patch.eval_control_param >= 0) current.eval_control_param = patch.eval_control_param;
    if (patch.object_resource_id[0] != '\0') copy_text(current.object_resource_id, sizeof(current.object_resource_id), patch.object_resource_id);
    if (patch.action_primitive[0] != '\0') copy_text(current.action_primitive, sizeof(current.action_primitive), patch.action_primitive);
    if (patch.input[0] != '\0') copy_text(current.input, sizeof(current.input), patch.input);
    if (patch.action_priority >= 0) current.action_priority = patch.action_priority;

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);
    const char *sql =
        "UPDATE actions SET et = ?, subject_resource_id = ?, eval_criteria_subject = ?, eval_criteria_operator = ?, eval_criteria_threshold = ?, eval_mode = ?, eval_control_param = ?, object_resource_id = ?, action_primitive = ?, input = ?, action_priority = ? WHERE ri = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        return false;
    }

    if (current.et[0] != '\0') sqlite3_bind_text(stmt, 1, current.et, -1, SQLITE_STATIC); else sqlite3_bind_null(stmt, 1);
    sqlite3_bind_text(stmt, 2, current.subject_resource_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, current.eval_criteria.subject, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, current.eval_criteria.op);
    sqlite3_bind_text(stmt, 5, current.eval_criteria.threshold, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, current.eval_mode);
    if (current.eval_control_param >= 0) sqlite3_bind_int(stmt, 7, current.eval_control_param); else sqlite3_bind_null(stmt, 7);
    sqlite3_bind_text(stmt, 8, current.object_resource_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, current.action_primitive, -1, SQLITE_STATIC);
    if (current.input[0] != '\0') sqlite3_bind_text(stmt, 10, current.input, -1, SQLITE_STATIC); else sqlite3_bind_null(stmt, 10);
    if (current.action_priority >= 0) sqlite3_bind_int(stmt, 11, current.action_priority); else sqlite3_bind_null(stmt, 11);
    sqlite3_bind_text(stmt, 12, current.ri, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
    bool ok = fetch_action_from_db(db, action_name, &current);
    sqlite3_close(db);
    if (ok && updated) *updated = current;
    return ok;
}

bool handle_action_request(const char *request_json,
                           const char *resource_uri,
                           const char *originator,
                           char *response,
                           size_t response_size,
                           int client_socket)
{
    (void)originator;
    (void)client_socket;

    if (!response || response_size == 0) return false;
    response[0] = '\0';

    Action action;
    char error[256] = {0};
    if (!action_parse_body(request_json, &action, error, sizeof(error)))
    {
        snprintf(response, response_size, "%s", error[0] ? error : "{\"error\":\"Invalid action request.\"}");
        return false;
    }

    char container_name[256] = {0};
    if (!get_last_uri_segment(resource_uri, container_name, sizeof(container_name)))
    {
        snprintf(response, response_size, "{\"error\":\"Invalid resource URI.\"}");
        return false;
    }

    if (!action_insert_into_db(container_name, resource_uri, &action, error, sizeof(error)))
    {
        snprintf(response, response_size, "%s", error[0] ? error : "{\"error\":\"Failed to create action.\"}");
        return false;
    }

    char *json = action_to_json(&action);
    if (!json)
    {
        snprintf(response, response_size, "{\"error\":\"Failed to serialize action.\"}");
        return false;
    }

    snprintf(response, response_size, "%s", json);
    free(json);
    return true;
}

char *handle_request_action_get(struct response_params *params,
                                const char *csebase,
                                const char *ae,
                                const char *container,
                                const char *action_name)
{
    (void)csebase;
    (void)ae;
    (void)container;

    sqlite3 *db = NULL;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK)
    {
        if (db) sqlite3_close(db);
        send_response(params, 500, "{\"error\":\"Database error.\"}");
        return NULL;
    }

    Action action;
    bool found = fetch_action_from_db(db, action_name, &action);
    sqlite3_close(db);

    if (!found)
    {
        send_response(params, 404, "{\"error\":\"Action not found.\"}");
        return NULL;
    }

    char *json = action_to_json(&action);
    if (!json)
    {
        send_response(params, 500, "{\"error\":\"Failed to serialize action.\"}");
        return NULL;
    }

    send_response(params, 200, json);
    return json;
}

void handle_request_action_put(struct response_params *params,
                               const char *csebase,
                               const char *ae,
                               const char *container,
                               const char *action_name,
                               const char *body)
{
    (void)csebase;
    (void)ae;
    (void)container;

    Action updated;
    if (!action_update_by_name(action_name, body, &updated))
    {
        send_response(params, 404, "{\"error\":\"Action not found.\"}");
        return;
    }

    char *json = action_to_json(&updated);
    if (!json)
    {
        send_response(params, 500, "{\"error\":\"Failed to serialize action.\"}");
        return;
    }

    send_response(params, 200, json);
    free(json);
}

void handle_request_action_delete(struct response_params *params,
                                  const char *csebase,
                                  const char *ae,
                                  const char *container,
                                  const char *action_name)
{
    (void)csebase;
    (void)ae;
    (void)container;

    if (!action_delete_by_name(action_name))
    {
        send_response(params, 404, "{\"error\":\"Action not found.\"}");
        return;
    }

    send_response(params, 200, "{\"msg\":\"Action deleted successfully.\"}");
}

bool handle_get_action(int client_socket,
                       const char *resource_name,
                       const char *resource_uri)
{
    (void)resource_uri;
    struct response_params params = {0};
    params.http_socket = client_socket;
    params.protocol = "HTTP";
    char *json = handle_request_action_get(&params, NULL, NULL, NULL, resource_name);
    bool ok = (json != NULL);
    if (json) free(json);
    return ok;
}

bool handle_delete_action(int client_socket,
                          const char *resource_name)
{
    char response[BUFFER_SIZE] = {0};

    if (!action_delete_by_name(resource_name))
    {
        snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\r\n\r\n");
        write(client_socket, response, strlen(response));
        return false;
    }

    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
             strlen("Action deleted successfully"), "Action deleted successfully");
    write(client_socket, response, strlen(response));
    return true;
}

