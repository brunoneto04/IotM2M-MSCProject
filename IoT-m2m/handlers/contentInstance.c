/*
AUTHORSHIP -------------------------------------------------

 * File:    contentInstance.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions for content instance.

*/

#include "../include/contentInstance.h"
#include "subscription.h"

char *handle_request_cin_get(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *cin_name, char *request_type)
{
    char response[BUFFER_SIZE] = {0};
    char jsonBody[BUFFER_SIZE] = {0};
    sqlite3 *db;

    // Open the SQLite database file
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        sqlite3_close(db);
        return NULL;
    }

    // Prepare SQL statement for retrieving content instance
    sqlite3_stmt *stmt;
    const char *sql_base =
        "SELECT r.ty, r.ri, r.rn, r.pi, r.ct, r.lt, "
        "ci.con, ci.cs, ci.et, ci.st "
        "FROM content_instances ci "
        "JOIN resources r ON ci.ri = r.ri "
        "WHERE r.pi IN ( "
            "SELECT c.ri "
            "FROM containers c "
            "JOIN resources r1 ON c.ri = r1.ri "
            "WHERE r1.pi IN ( "
                "SELECT ae.ri "
                "FROM application_entities ae "
                "JOIN resources r2 ON ae.ri = r2.ri "
                "WHERE r2.pi IN ( "
                    "SELECT cb.ri "
                    "FROM cse_bases cb "
                    "JOIN resources r3 ON cb.ri = r3.ri "
                    "WHERE r3.rn = ? "
                ") "
                "AND r2.rn = ? "
            ") "
            "AND r1.rn = ? "
        ") ";

    char sql_combined[BUFFER_SIZE];

    if (strcmp(cin_name, "ol") == 0) {
        // Select the oldest entry
        snprintf(sql_combined, sizeof(sql_combined), "%s ORDER BY r.ct ASC LIMIT 1", sql_base);
    } else if (strcmp(cin_name, "la") == 0) {
        // Select the latest entry
        snprintf(sql_combined, sizeof(sql_combined), "%s ORDER BY r.ct DESC LIMIT 1", sql_base);
    } else {
        // Default query
        snprintf(sql_combined, sizeof(sql_combined), "%s AND r.rn = ?", sql_base);
    }

    sqlite3_prepare_v2(db, sql_combined, -1, &stmt, NULL);

    // Bind parameters for the SQL statement
    sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, container_name, -1, SQLITE_STATIC);

    if (strcmp(cin_name, "ol") != 0 && strcmp(cin_name, "la") != 0) {
        sqlite3_bind_text(stmt, 4, cin_name, -1, SQLITE_STATIC);
    }

    // Execute the SQL statement
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        // Start constructing the JSON response
        if (strcmp(request_type, "GET") == 0)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_CONTENT);
            }
        }
        else if (strcmp(request_type, "POST") == 0)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\n\r\n");
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_CREATED);
            }
            
        }

        snprintf(jsonBody, BUFFER_SIZE, "{\"m2m:cin\": {");

        // Retrieve and add each column value to the JSON response
        for (int i = 0; i < sqlite3_column_count(stmt); i++)
        {
            const char *column_name = sqlite3_column_name(stmt, i);
            const char *column_value = (const char *)sqlite3_column_text(stmt, i);

            snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "\"%s\": ", column_name);

            if (strcmp(column_name, "et") == 0 && column_value == NULL)
            {
                snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "null, ");
            }
            else if (strcmp(column_name, "ty") == 0 || strcmp(column_name, "st") == 0 || strcmp(column_name, "cs") == 0)
            {
                snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "%s, ", column_value);
            }
            else
            {
                snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "\"%s\", ", column_value);
            }
        }

        // Add closing braces for the JSON response
        strcat(jsonBody, "}}");

        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            strcat(response, jsonBody);
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_add_data(params->coap_response, strlen(jsonBody), (const uint8_t *)jsonBody);
        }
    }
    else
    {
        // If the row is not found, set 404 response
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_NOT_FOUND);
        }
    }

    // Finalize the SQL statement and close the database connection
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // Send the response if the request type is not internal
    if (strcmp(request_type, "INTERNAL") != 0)
    {
        //If its HTTP, write the response to the socket, else CoAP lib will handle it
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_add_data(params->coap_response, strlen(response), (const uint8_t *)response);
        }
    }
    else if (strcmp(request_type, "POST") == 0)
    {
        free(cin_name);
    }

    return strdup(jsonBody); // Return a copy of jsonBody
}

void handle_request_cin_post(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request, char *body)
{
    char response[BUFFER_SIZE] = {0};
    char *err_msg = 0;
    sqlite3 *db;
    int rc;

    // Variables to store values
    const char *ri = NULL;
    int ty = -1;
    const char *rn = NULL;
    const char *ae_ri = NULL;
    const char *pi = NULL;
    const char *et = NULL;
    const char *con = NULL;
    int cs = -1;
    int st = 0;

    // container variables
    int mni, mbs, mia, cni, cbs;

    // CHECK IF csebase_name, ae and container EXIST
    char *jsonBody = handle_request_container_get(params, csebase_name, ae_name, container_name, "INTERNAL");
    if (jsonBody[0] == '\0')
    {
        // Handle absence of container
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_NOT_FOUND);
            coap_add_data(params->coap_response, strlen(response), (const uint8_t *)response);
        }
        free((char *)jsonBody);
        return;
    }

    // Print the JSON body for debugging purposes
    printf("jsonBody: %s\n", jsonBody);

    // Attempt to parse the JSON string
    json_object *root = json_tokener_parse(jsonBody);
    if (root == NULL)
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        free((char *)jsonBody);
        return;
    }

    json_object *cnt_object;
    if (json_object_object_get_ex(root, "m2m:cnt", &cnt_object))
    {
        json_object *ri_object;
        json_object *pi_object;
        if (json_object_object_get_ex(cnt_object, "ri", &ri_object))
        {
            const char *ri_str = json_object_get_string(ri_object);
            if (ri_str != NULL)
            {
                pi = strdup(ri_str);
            }
            else
            {
                printf("Failed to get string for pi\n");
            }
        }
        if (json_object_object_get_ex(cnt_object, "pi", &pi_object))
        {
            const char *pi_str = json_object_get_string(pi_object);
            if (pi_str != NULL)
            {
                ae_ri = strdup(pi_str);
            }
            else
            {
                printf("Failed to get string for ae_ri\n");
            }
        }
        else
        {
            printf("pi key not found\n");
        }
    }
    else
    {
        printf("m2m:cnt key not found\n");
    }

    // Clean up JSON objects and free memory
    json_object_put(root);
    free((char *)jsonBody);

    // PARSE X-M2M-ORIGIN HEADER (AE_RI)
    const char *origin_header = strstr(request, "X-M2M-Origin:");
    if (origin_header)
    {
        const char *origin_value_start = origin_header + strlen("X-M2M-Origin:");
        origin_value_start += strspn(origin_value_start, " \t"); // Skip leading whitespace
        const char *origin_value_end = origin_value_start + strcspn(origin_value_start, "\r\n");

        while (origin_value_end > origin_value_start && isspace(origin_value_end[-1])) // Skip trailing whitespace
        {
            origin_value_end--;
        }

        size_t ae_ri_length = origin_value_end - origin_value_start;
        char *temp_ae_ri = (char *)malloc(ae_ri_length + 1);
        if (temp_ae_ri == NULL)
        {
            // Memory allocation failed
             if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
            }
            free((char *)ae_ri);
            free((char *)pi);
            return;
        }
        strncpy(temp_ae_ri, origin_value_start, ae_ri_length);
        temp_ae_ri[ae_ri_length] = '\0'; // Null-terminate the string

        if (strcmp(ae_ri, temp_ae_ri) != 0)
        {
            
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value is not valid (%s).\"}", temp_ae_ri);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                const char *error_msg = "X-M2M-Origin header value is not valid (%s).", temp_ae_r;
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
            }
            free((char *)ae_ri);
            free((char *)pi);
            free(temp_ae_ri);
            return;
        }
        free(temp_ae_ri);
    }

    // PARSE CONTENT-TYPE HEADER (ty)
    const char *content_type_header = strstr(request, "Content-Type:");
    if (content_type_header)
    {
        const char *content_type_value_start = content_type_header + strlen("Content-Type:");
        const char *ty_position = strstr(content_type_value_start, "ty=");
        if (ty_position)
        {
            int ty_value;
            if (sscanf(ty_position, "ty=%d", &ty_value) == 1)
            {
                if (ty_value != 4)
                {
                    // Error: Invalid ty value
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ty value must be 4.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "ty value must be 4.";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    free((char *)ae_ri);
                    free((char *)pi);
                    return;
                }
                else
                {
                    ty = ty_value;
                }
            }
        }
        else
        {
            ty = 4;
        }
    }
    else
    {
        ty = 4;
    }

    // parse do body do pedido
    root = json_tokener_parse(body);
    if (root == NULL)
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"JSON body incorrectly formatted.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "JSON body incorrectly formatted.";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        free((char *)ae_ri);
        free((char *)pi);
        return;
    }

    // Open the SQLite database file
    rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ae_ri);
        free((char *)pi);
        return;
    }

    // Select the new state_tag value
    const char *select_sql = "SELECT st, mni, mbs, mia, cni, cbs FROM containers WHERE ri = ?";
    sqlite3_stmt *stmt_select_cnt;
    sqlite3_prepare_v2(db, select_sql, -1, &stmt_select_cnt, NULL);
    sqlite3_bind_text(stmt_select_cnt, 1, pi, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt_select_cnt);
    if (rc == SQLITE_ROW) {
        st = sqlite3_column_int(stmt_select_cnt, 0);
        mni = sqlite3_column_int(stmt_select_cnt, 1); // max nr of instances
        mbs = sqlite3_column_int(stmt_select_cnt, 2); // max byte size
        mia = sqlite3_column_int(stmt_select_cnt, 3); // max instance age
        cni = sqlite3_column_int(stmt_select_cnt, 4); // current nor of instances
        cbs = sqlite3_column_int(stmt_select_cnt, 5); // current byte size

        // Print retrieved values
        printf("st: %d\n", st);
        printf("mni: %d\n", mni);
        printf("mbs: %d\n", mbs);
        printf("mia: %d\n", mia);
        printf("cni: %d\n", cni);
        printf("cbs: %d\n", cbs);
    }
    sqlite3_finalize(stmt_select_cnt);

    // Retrieve values from JSON object
    json_object *cin_object;
    if (json_object_object_get_ex(root, "m2m:cin", &cin_object))
    {
        json_object_object_foreach(cin_object, key, val)
        {
            if (strcmp(key, "ri") == 0)
            {
                ri = strdup(json_object_get_string(val));

                if (ri == NULL || strlen(ri) == 0)
                {
                    // Handle the case where ri is NULL or an empty string
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ri value cannot be empty.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "ri value cannot be empty.";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }

                if (!is_valid_string_plus_three_extra_chars(ri))
                {
                    // Handle the case where ri is NULL or an empty string
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ri value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", ri);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "ri value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).", ri;
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }

                // Check if ri already exists
                sqlite3_stmt *stmt;
                const char *sql = "SELECT * FROM resources WHERE ri = ?";

                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);

                // Execute the statement
                rc = sqlite3_step(stmt);

                // If a resource is found with the same ri, return error
                if (rc == SQLITE_ROW)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ri value must be unique (%s).\"}", ri);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "ri value must be unique (%s).", ri;
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }

                sqlite3_finalize(stmt);
            }
            else if (strcmp(key, "rn") == 0)
            {
                rn = strdup(json_object_get_string(val));

                if (rn == NULL || strlen(rn) == 0)
                {
                    // Handle the case where rn is NULL or an empty string
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value cannot be empty.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "rn value cannot be empty.";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }

                if (!is_valid_string_plus_three_extra_chars(rn))
                {
                    // Handle the case where rn is NULL or an empty string
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", rn);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "rn value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).", rn;
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }

                // Check if rn already exists
                sqlite3_stmt *stmt;
                const char *sql =
                    "SELECT * "
                    "FROM resources "
                    "WHERE rn = ? "
                    "AND pi IN ( "
                        "SELECT c.ri "
                        "FROM resources r1 "
                        "JOIN containers c ON r1.ri = c.ri "
                        "WHERE r1.pi IN ( "
                            "SELECT ae.ri "
                            "FROM resources r2 "
                            "JOIN application_entities ae ON r2.ri = ae.ri "
                            "WHERE r2.pi IN ( "
                                "SELECT cb.ri "
                                "FROM resources r3 "
                                "JOIN cse_bases cb ON r3.ri = cb.ri "
                                "WHERE r3.rn = ? "
                            ") "
                            "AND r2.rn = ? "
                        ") "
                        "AND r1.rn = ? "
                    ")";

                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, csebase_name, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, ae_name, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 4, container_name, -1, SQLITE_STATIC);

                // Execute the statement
                rc = sqlite3_step(stmt);

                // If a content instance is found with the same rn, return error
                if (rc == SQLITE_ROW)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value must be unique (%s).\"}", rn);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "rn value must be unique (%s).\"}", rn;
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }

                sqlite3_finalize(stmt);
            }
            if (strcmp(key, "et") == 0)
            {
                if (json_object_is_type(val, json_type_null))
                {
                    continue;
                }
                else
                {
                    et = strdup(json_object_get_string(val));

                    // Validate datetime format
                    if (!is_valid_datetime_format(et))
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value format should be 'YYYY-MM-DD HH:MM:SS'.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "et value format should be 'YYYY-MM-DD HH:MM:SS'.";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ae_ri);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        if (con != NULL)
                            free((char *)con);
                        return;
                    }

                    // Check if the datetime is valid
                    if (!is_valid_datetime(et))
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value is not a valid date.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "et value is not a valid date.";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ae_ri);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        if (con != NULL)
                            free((char *)con);
                        return;
                    }
                    // Check if the datetime is in the future
                    if (!is_future_datetime(et))
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value is not in the future.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "et value is not in the future.";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ae_ri);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        if (con != NULL)
                            free((char *)con);
                        return;
                    }

                    // Check if datetime is less than Current Time + mia (of container)
                    if (!is_datetime_less_than_current_plus_mia(et, mia))
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value is not less than the current time plus %d seconds (mia of its container).\"}", mia);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "et value is not less than the current time plus %d seconds (mia of its container).\"}", mia;
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ae_ri);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        if (con != NULL)
                            free((char *)con);
                        return;
                    }
                }
            }
            if (strcmp(key, "con") == 0)
            {
                con = strdup(json_object_get_string(val));
                
                if (con == NULL || strlen(con) == 0)
                {
                    // Handle the case where con is NULL or an empty string
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"con value cannot be empty.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "con value cannot be empty.";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ae_ri);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    if (con != NULL)
                        free((char *)con);
                    return;
                }
            }
        }
    }
    else
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"'m2m:cin' object not found in JSON.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "'m2m:cin' object not found in JSON.";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ae_ri);
        free((char *)pi);
        return;
    }

    // Handle the case where "rn" does not exist
    if (con == NULL)
    {
        // Memory allocation failed
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"con is required.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "con is required.";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_BAD_REQUEST);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ae_ri);
        free((char *)pi);
        if (rn != NULL)
            free((char *)rn);
        if (et != NULL)
            free((char *)et);
        if (ri != NULL)
            free((char *)ri);
        return;
    }

    if (ri == NULL)
    {
        do
        {
            if (ri != NULL)
                free((char *)ri);

            // If no header is provided, generate a random sequence and check if that random sequence ri already exists.
            ri = generate_random_sequence();
            if (ri == NULL)
            {
                // Memory allocation failed
                if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                {
                    snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
                    write(params->http_socket, response, strlen(response));
                }
                else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                {
                    coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
                }
                sqlite3_close(db);
                json_object_put(root);
                free((char *)ae_ri);
                free((char *)pi);
                if (rn != NULL)
                    free((char *)rn);
                if (et != NULL)
                    free((char *)et);
                if (con != NULL)
                    free((char *)con);
                return;
            }

            sqlite3_stmt *stmt;
            const char *sql = "SELECT * FROM resources WHERE ri = ?";

            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

            sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);

            // Execute the statement
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } while (rc == SQLITE_ROW);
    }

    // Handle the case where "rn" does not exist
    if (rn == NULL)
    {
        do
        {
            if (rn != NULL)
                free((char *)rn);

            // If no rn is provided, generate a random sequence and check if that random sequence rn already exists.
            rn = generate_random_sequence();
            if (rn == NULL)
            {
                // Memory allocation failed
                if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                {
                    snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
                    write(params->http_socket, response, strlen(response));
                }
                else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                {
                    coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
                }
                sqlite3_close(db);
                json_object_put(root);
                free((char *)ae_ri);
                free((char *)pi);
                if (et != NULL)
                    free((char *)et);
                if (ri != NULL)
                    free((char *)ri);
                if (con != NULL)
                    free((char *)con);
                return;
            }

            sqlite3_stmt *stmt;
            const char *sql =
                "SELECT * "
                "FROM resources "
                "WHERE rn = ? "
                "AND pi IN ( "
                    "SELECT c.ri "
                    "FROM resources r1 "
                    "JOIN containers c ON r1.ri = c.ri "
                    "WHERE r1.pi IN ( "
                        "SELECT ae.ri "
                        "FROM resources r2 "
                        "JOIN application_entities ae ON r2.ri = ae.ri "
                        "WHERE r2.pi IN ( "
                            "SELECT cb.ri "
                            "FROM resources r3 "
                            "JOIN cse_bases cb ON r3.ri = cb.ri "
                            "WHERE r3.rn = ? "
                        ") "
                        "AND r2.rn = ? "
                    ") "
                    "AND r1.rn = ? "
                ")";

            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

            sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, csebase_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, ae_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, container_name, -1, SQLITE_STATIC);

            // Execute the statement
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } while (rc == SQLITE_ROW);
    }

    if (et == NULL)
    {
        et = get_current_time_plus_mia(mia);
    }

    cs = strlen(con);

    printf("ae_ri: %s\n", ae_ri);
    printf("pi: %s\n", pi);
    printf("ty: %d\n", ty);
    printf("ri: %s\n", ri);
    printf("rn: %s\n", rn);
    printf("et: %s\n", et);
    printf("con: %s\n", con);
    printf("cs: %d\n", cs);
    printf("st: %d\n", st);

    // WRITE TO DB------------------------------------------
    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    // Enquanto o currentNrOfInstances cni >= maxNrOfInstances mni, vai apagando content instances mais antigos
    // Enquanto o (currentByteSize cbs + contentSize cs) > maxByteSize mbs, vai apagando content instances mais antigos até o (currentByteSize cbs + contentSize cs) <= maxByteSize mbs
    // currentNrOfInstances cni++
    // currentByteSize cbs += contentSize cs

    while (cni >= mni || cbs + cs > mbs) 
    {
        handle_request_cin_delete(params, csebase_name, ae_name, container_name, "ol", "INTERNAL", db);

        // Select the new state_tag value
        const char *select_sql_cnt_data = "SELECT cni, cbs FROM containers WHERE ri = ?";
        sqlite3_stmt *stmt_select_cnt_data;
        sqlite3_prepare_v2(db, select_sql_cnt_data, -1, &stmt_select_cnt_data, NULL);
        sqlite3_bind_text(stmt_select_cnt_data, 1, pi, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt_select_cnt_data);
        if (rc == SQLITE_ROW) {
            cni = sqlite3_column_int(stmt_select_cnt_data, 0); // current nr of instances
            cbs = sqlite3_column_int(stmt_select_cnt_data, 1); // current byte size
        }
        sqlite3_finalize(stmt_select_cnt_data);
    }

    // Update the st, cni and cbs of container
    const char *update_sql = "UPDATE containers SET st = st + 1, cni = cni + 1, cbs = cbs + ? WHERE ri = ?";
    st++;
    sqlite3_stmt *stmt_update_cnt;
    sqlite3_prepare_v2(db, update_sql, -1, &stmt_update_cnt, NULL);
    sqlite3_bind_int(stmt_update_cnt, 1, cs);
    sqlite3_bind_text(stmt_update_cnt, 2, pi, -1, SQLITE_STATIC);
    sqlite3_step(stmt_update_cnt);
    sqlite3_finalize(stmt_update_cnt);

    // Write to resources table
    // SQL statement for resources table
    const char *sql_template_res = "INSERT INTO resources (ty, ri, rn, pi, ct, lt) "
                                   "VALUES (?, ?, ?, ?, datetime('now', 'localtime'), datetime('now', 'localtime'))";

    // Prepare SQL statement
    sqlite3_stmt *stmt_res;
    sqlite3_prepare_v2(db, sql_template_res, -1, &stmt_res, NULL);

    sqlite3_bind_int(stmt_res, 1, ty);
    sqlite3_bind_text(stmt_res, 2, ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_res, 3, rn, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_res, 4, pi, -1, SQLITE_STATIC);

    int rc_res = sqlite3_step(stmt_res);

    // Finalize statement
    sqlite3_finalize(stmt_res);

    if (rc_res != SQLITE_DONE)
    {
        fprintf(stderr, "Failed to insert into resources table: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        // Send error response to client
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        json_object_put(root);
        free((char *)ae_ri);
        free((char *)pi);
        if (rn != NULL)
            free((char *)rn);
        if (et != NULL)
            free((char *)et);
        if (ri != NULL)
            free((char *)ri);
        if (con != NULL)
            free((char *)con);
        return;
    }

    // Write to content_instances table
    // SQL statement for content_instances table
    const char *sql_template_cin;

    sql_template_cin = "INSERT INTO content_instances (ri, st, con, cs, et) "
                            "VALUES (?, ?, ?, ?, ?)";

    // Prepare SQL statement
    sqlite3_stmt *stmt_cin;
    sqlite3_prepare_v2(db, sql_template_cin, -1, &stmt_cin, NULL);

    sqlite3_bind_text(stmt_cin, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt_cin, 2, st);
    sqlite3_bind_text(stmt_cin, 3, con, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt_cin, 4, cs);
    sqlite3_bind_text(stmt_cin, 5, et, -1, SQLITE_STATIC);
    int rc_cin = sqlite3_step(stmt_cin);

    // Finalize statement
    sqlite3_finalize(stmt_cin);

    if (rc_cin != SQLITE_DONE)
    {
        fprintf(stderr, "Failed to insert into content_instances table: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        // Send error response to client
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        json_object_put(root);
        free((char *)ae_ri);
        free((char *)pi);
        if (rn != NULL)
            free((char *)rn);
        if (et != NULL)
            free((char *)et);
        if (ri != NULL)
            free((char *)ri);
        if (con != NULL)
            free((char *)con);
        return;
    }

        // Commit transaction
        sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
        printf("Transaction completed successfully.\n");

    // Add subscription notification here
    sqlite3_stmt *stmt_sub;
    const char *sql_sub = "SELECT * FROM subscriptions";  // Simplified query first for debug
    char resource_uri[512] = {0};
    char child_resource_uri[512] = {0};

    // URI do container (pai)
    snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s", 
            csebase_name, ae_name, container_name);

    // URI do filho criado (nova content instance)
    snprintf(child_resource_uri, sizeof(child_resource_uri), "/%s/%s/%s/%s", 
            csebase_name, ae_name, container_name, rn);

    //printf("\n=== Checking Subscriptions ===\n");
    //printf("Container URI (pai): %s\n", resource_uri);
    //printf("Child URI (filho criado): %s\n", child_resource_uri);

    if (sqlite3_prepare_v2(db, sql_sub, -1, &stmt_sub, NULL) == SQLITE_OK) {
        int count = 0;
        while (sqlite3_step(stmt_sub) == SQLITE_ROW) {
            count++;
            const char* sub_resource_uri = (const char*)sqlite3_column_text(stmt_sub, 1); // resource_uri column
            //printf("\nFound subscription %d:\n", count);
            //printf("- Subscription URI: %s\n", sub_resource_uri);
            
            // Create notification content
            json_object* notification = json_object_new_object();
            json_object* cin_obj = json_object_new_object();
            json_object_object_add(cin_obj, "con", json_object_new_string(con));
            json_object_object_add(notification, "m2m:cin", cin_obj);
            
            const char* notification_content = json_object_to_json_string(notification);
            
            handle_mqtt_notification(child_resource_uri, notification_content, 3);
            
            json_object_put(notification);
        }
        
        if (count == 0) {
            printf("[INFO] No subscriptions found in database!\n");
    
            sqlite3_stmt *stmt_table;
            const char *sql_table = "SELECT sql FROM sqlite_master WHERE type='table' AND name='subscriptions'";
            if (sqlite3_prepare_v2(db, sql_table, -1, &stmt_table, NULL) == SQLITE_OK) {
                if (sqlite3_step(stmt_table) == SQLITE_ROW) {
                    printf("%s\n", sqlite3_column_text(stmt_table, 0));
                }
                sqlite3_finalize(stmt_table);
            }
        }
        
        sqlite3_finalize(stmt_sub);
    }

    sqlite3_close(db);
    json_object_put(root);
    free((char *)ae_ri);
    free((char *)pi);
    if (et != NULL)
        free((char *)et);
    if (ri != NULL)
        free((char *)ri);
    if (con != NULL)
        free((char *)con);
    
    // Allocate memory for the destination string
    char *rn_copy = NULL;
    rn_copy = strdup(rn); // Use strdup toplicate strings
    if (rn_copy == NULL)
    {
        // Memory allocation failed
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        return;
    }
    free((char *)rn);
    printf("GET /%s/%s/\n", csebase_name, rn_copy);

    // RETURN THE ROW INSERTED BACK TO THE CLIENT WITH ALL THE DATA
    jsonBody = handle_request_cin_get(params, csebase_name, ae_name, container_name, rn_copy, "POST");
    if (jsonBody != NULL)
    {
        free(jsonBody);
    }

    // Free the allocated memory for rn_copy
    free(rn_copy);
}

void handle_request_cin_delete(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *cin_name, char *request_type, sqlite3 *db)
{
    char response[BUFFER_SIZE] = {0};
    int rc;
    char *jsonBody;

    if (strcmp(request_type, "DELETE") == 0)
    {
        // CHECK IF cin_name EXISTS
        jsonBody = handle_request_cin_get(params, csebase_name, ae_name, container_name, cin_name, "INTERNAL");

        if (strlen(jsonBody) == 0)
        {
            // Handle absence of resource ID
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found");
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_NOT_FOUND);
            }
            free(jsonBody);
            return;
        }

        // Open the SQLite database file
        rc = sqlite3_open(DB_PATH, &db);

        if (rc != SQLITE_OK)
        {
             if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
            }
            free(jsonBody);
            return;
        }

        // Begin transaction
        sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);
    }

    // Update the container st, cni and cbs
    char* sql_cnt = 
        "UPDATE containers "
        "SET cni = cni - 1, "
        "st = st + 1, "
        "cbs = cbs - ("
            "SELECT cs "
            "FROM content_instances ci "
            "JOIN resources r ON ci.ri = r.ri "
            "WHERE r.pi = containers.ri "
            "AND r.pi IN ("
                "SELECT c.ri "
                "FROM containers c "
                "JOIN resources r2 ON c.ri = r2.ri "
                "WHERE r2.pi IN ("
                    "SELECT ae.ri "
                    "FROM application_entities ae "
                    "JOIN resources r3 ON ae.ri = r3.ri "
                    "WHERE r3.pi IN ("
                        "SELECT cb.ri "
                        "FROM cse_bases cb "
                        "JOIN resources r4 ON cb.ri = r4.ri "
                        "WHERE r4.rn = ?"
                    ") "
                    "AND r3.rn = ?"
                ") "
                "AND r2.rn = ?"
            ") ";

    char* sql_where_clause =
        "WHERE ri IN ("
            "SELECT c.ri "
            "FROM containers c "
            "JOIN resources r2 ON c.ri = r2.ri "
            "WHERE r2.pi IN ("
                "SELECT ae.ri "
                "FROM application_entities ae "
                "JOIN resources r3 ON ae.ri = r3.ri "
                "WHERE r3.pi IN ("
                    "SELECT cb.ri "
                    "FROM cse_bases cb "
                    "JOIN resources r4 ON cb.ri = r4.ri "
                    "WHERE r4.rn = ?"
                ") "
                "AND r3.rn = ?"
            ") "
            "AND r2.rn = ?"
        ")";
    
    char update_sql[BUFFER_SIZE];

    if (strcmp(cin_name, "ol") == 0) {
        // Select the oldest entry
        snprintf(update_sql, sizeof(update_sql), "%s ORDER BY r.ct ASC LIMIT 1) %s", sql_cnt, sql_where_clause);
    } else if (strcmp(cin_name, "la") == 0) {
        // Select the latest entry
        snprintf(update_sql, sizeof(update_sql), "%s ORDER BY r.ct DESC LIMIT 1) %s", sql_cnt, sql_where_clause);
    } else {
        // Default query
        snprintf(update_sql, sizeof(update_sql), "%s AND r.rn = ?) %s", sql_cnt, sql_where_clause);
    }

    sqlite3_stmt *stmt_update_cnt;
    sqlite3_prepare_v2(db, update_sql, -1, &stmt_update_cnt, NULL);
    sqlite3_bind_text(stmt_update_cnt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_update_cnt, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_update_cnt, 3, container_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_update_cnt, 4, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_update_cnt, 5, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_update_cnt, 6, container_name, -1, SQLITE_STATIC);
    sqlite3_step(stmt_update_cnt);
    sqlite3_finalize(stmt_update_cnt);

    // DELETE statement for content_instances table
    const char *sql_cin =
        "DELETE FROM content_instances  "
        "WHERE ri IN ("
            "SELECT ci.ri FROM content_instances ci "
            "JOIN resources r ON ci.ri = r.ri "
            "WHERE r.pi IN ("
                "SELECT c.ri FROM containers c "
                "JOIN resources r2 ON c.ri = r2.ri "
                "WHERE r2.pi IN ("
                    "SELECT ae.ri FROM application_entities ae "
                    "JOIN resources r3 ON ae.ri = r3.ri "
                    "WHERE r3.pi IN ("
                        "SELECT cb.ri FROM cse_bases cb "
                        "JOIN resources r4 ON cb.ri = r4.ri "
                        "WHERE r4.rn = ?"
                    ") "
                    "AND r3.rn = ?"
                ") "
                "AND r2.rn = ?"
            ") ";

    sqlite3_stmt *stmt_cin;

    char sql_combined_cin[BUFFER_SIZE];

    if (strcmp(cin_name, "ol") == 0) {
        // Select the oldest entry
        snprintf(sql_combined_cin, sizeof(sql_combined_cin), "%s ORDER BY r.ct ASC LIMIT 1 )", sql_cin);
    } else if (strcmp(cin_name, "la") == 0) {
        // Select the latest entry
        snprintf(sql_combined_cin, sizeof(sql_combined_cin), "%s ORDER BY r.ct DESC LIMIT 1 )", sql_cin);
    } else {
        // Default query
        snprintf(sql_combined_cin, sizeof(sql_combined_cin), "%s AND r.rn = ? )", sql_cin);
    }

    sqlite3_prepare_v2(db, sql_combined_cin, -1, &stmt_cin, NULL);

    // Bind parameters for the SQL statement
    sqlite3_bind_text(stmt_cin, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_cin, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_cin, 3, container_name, -1, SQLITE_STATIC);
    if (strcmp(cin_name, "ol") != 0 && strcmp(cin_name, "la") != 0) {
        sqlite3_bind_text(stmt_cin, 4, cin_name, -1, SQLITE_STATIC);
    }

    sqlite3_step(stmt_cin);

    sqlite3_finalize(stmt_cin);

    // DELETE statement for resources table
    const char *sql_rs =
        "DELETE FROM resources "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "WHERE r.pi IN ( "
                "SELECT c.ri "
                "FROM containers c "
                "JOIN resources r2 ON c.ri = r2.ri "
                "WHERE r2.pi IN ( "
                    "SELECT ae.ri "
                    "FROM application_entities ae "
                    "JOIN resources r3 ON ae.ri = r3.ri "
                    "WHERE r3.pi IN ( "
                        "SELECT cb.ri "
                        "FROM cse_bases cb "
                        "JOIN resources r4 ON cb.ri = r4.ri "
                        "WHERE r4.rn = ? "
                    ") "
                    "AND r3.rn = ? "
                ") "
                "AND r2.rn = ? "
            ") "
        ") ";

    sqlite3_stmt *stmt_rs;

    char sql_combined_rs[BUFFER_SIZE];

    if (strcmp(cin_name, "ol") == 0) {
        // Select the oldest entry
        snprintf(sql_combined_rs, sizeof(sql_combined_rs), "%s ORDER BY ct ASC LIMIT 1", sql_rs);
    } else if (strcmp(cin_name, "la") == 0) {
        // Select the latest entry
        snprintf(sql_combined_rs, sizeof(sql_combined_rs), "%s ORDER BY ct DESC LIMIT 1", sql_rs);
    } else {
        // Default query
        snprintf(sql_combined_rs, sizeof(sql_combined_rs), "%s AND rn = ?", sql_rs);
    }

    sqlite3_prepare_v2(db, sql_combined_rs, -1, &stmt_rs, NULL);

    // Bind parameters for the SQL statement
    sqlite3_bind_text(stmt_rs, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_rs, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_rs, 3, container_name, -1, SQLITE_STATIC);
    if (strcmp(cin_name, "ol") != 0 && strcmp(cin_name, "la") != 0) {
        sqlite3_bind_text(stmt_rs, 4, cin_name, -1, SQLITE_STATIC);
    }

    sqlite3_step(stmt_rs);

    sqlite3_finalize(stmt_rs);

    if (strcmp(request_type, "DELETE") == 0)
    {
        // Commit transaction
        sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);

        // Close the SQLite database
        sqlite3_close(db);

        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", jsonBody);
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_DELETED);
            coap_add_data(params->coap_response, strlen(response), (const uint8_t *)response);
        }
        // Free allocated memory
        free(jsonBody);
    }
}
