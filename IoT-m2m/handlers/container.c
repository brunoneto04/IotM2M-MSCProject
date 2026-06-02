/*
AUTHORSHIP -------------------------------------------------

 * File:    container.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions for container.

*/

#include "../include/container.h"
#include "../include/subscription.h"

char *handle_request_container_get(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request_type)
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

    // Prepare SQL statement for retrieving application entity
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT r.ty, r.ri, r.rn, r.pi, r.ct, r.lt, "
        "c.et, c.st, c.cni, c.cbs, c.mbs, c.mia, c.mni "
        "FROM containers c "
        "JOIN resources r ON c.ri = r.ri "
        "WHERE r.pi IN ( "
            "SELECT r2.ri "
            "FROM resources r2 "
            "JOIN application_entities ae ON r2.ri = ae.ri "
            "WHERE r2.pi IN ( "
                "SELECT cb.ri "
                "FROM cse_bases cb "
                "JOIN resources r3 ON cb.ri = r3.ri "
                "WHERE r3.rn = ? "
            ") "
            "AND r2.rn = ? "
        ") "
        "AND r.rn = ?";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    // Bind parameters for the SQL statement
    sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, container_name, -1, SQLITE_STATIC);

    // Execute the SQL statement
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        // Start constructing the JSON response
        if (strcmp(request_type, "GET") == 0)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
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
        else if (strcmp(request_type, "PUT") == 0)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_CHANGED);
            }
            
        }


        snprintf(jsonBody, BUFFER_SIZE, "{\"m2m:cnt\": {");

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
            else if (strcmp(column_name, "ty") == 0 || strcmp(column_name, "st") == 0 || strcmp(column_name, "cni") == 0 || strcmp(column_name, "cbs") == 0 || strcmp(column_name, "mbs") == 0 || strcmp(column_name, "mia") == 0 || strcmp(column_name, "mni") == 0)
            {
                snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "%s, ", column_value);
            }
            else
            {
                snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "\"%s\", ", column_value);
            }
        }

        // Retrieve lbl values from the labels table
        char *sql_lbl =
            "SELECT lbl "
            "FROM labels "
            "WHERE ri IN ( "
                "SELECT c.ri "
                "FROM containers c "
                "JOIN resources r ON c.ri = r.ri "
                "WHERE r.pi IN ( "
                    "SELECT r2.ri "
                    "FROM resources r2 "
                    "JOIN application_entities ae ON r2.ri = ae.ri "
                    "WHERE r2.pi IN ( "
                        "SELECT cb.ri "
                        "FROM cse_bases cb "
                        "JOIN resources r3 ON cb.ri = r3.ri "
                        "WHERE r3.rn = ? "
                    ") "
                    "AND r2.rn = ? "
                ") "
                "AND r.rn = ? "
            ")";

        sqlite3_stmt *stmt_lbl;
        sqlite3_prepare_v2(db, sql_lbl, -1, &stmt_lbl, NULL);

        sqlite3_bind_text(stmt_lbl, 1, csebase_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_lbl, 2, ae_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_lbl, 3, container_name, -1, SQLITE_STATIC);

        strcat(jsonBody, "\"lbl\": [");
        int has_labels = 0; // Flag to track if any labels are retrieved
        while (sqlite3_step(stmt_lbl) == SQLITE_ROW)
        {
            if (has_labels)
            {
                strcat(jsonBody, ", "); // Add comma separator if this isn't the first label
            }
            const char *lbl_value = (const char *)sqlite3_column_text(stmt_lbl, 0);
            strcat(jsonBody, "\"");
            strcat(jsonBody, lbl_value);
            strcat(jsonBody, "\"");
            has_labels = 1; // Set flag to indicate labels have been retrieved
        }
        strcat(jsonBody, "]");

        // Finalize the statement for lbl retrieval
        sqlite3_finalize(stmt_lbl);

        // Add closing braces for the JSON response
        strcat(jsonBody, "}}");
        strcat(response, jsonBody);
    }
    else
    {
        // If the row is not found, set 404 Not Found response
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
            write(params->http_socket, response, strlen(response));
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
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            // Send HTTP response
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            // Send CoAP response
            coap_add_data(params->coap_response, strlen(response),(const uint8_t *)response);
        }
    }
    else if (strcmp(request_type, "POST") == 0)
    {
        free(container_name);
    }
    return strdup(jsonBody); // Return a copy of jsonBody
}

void handle_request_container_post(struct response_params *params, char *csebase_name, char *ae_name, char *request, char *body)
{
    char response[BUFFER_SIZE] = {0};
    char *err_msg = 0;
    sqlite3 *db;
    int rc;

    // Variables to store values
    const char *ri = NULL;
    int ty = -1;
    const char *rn = NULL;
    const char *pi = NULL;
    const char *et = NULL;
    long long int mbs = -1;
    long long int mia = -1;
    long long int mni = -1;
    const char *lbl[LABELS_NUMBER] = {NULL};

    // CHECK IF csebase_name and ae EXIST
    char *jsonBody = handle_request_ae_get(params, csebase_name, ae_name, "INTERNAL");
    if (jsonBody[0] == '\0')
    {
        // Handle absence of ae
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_NOT_FOUND);

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

    json_object *ae_object;
    if (json_object_object_get_ex(root, "m2m:ae", &ae_object))
    {
        json_object *ri_object;
        if (json_object_object_get_ex(ae_object, "ri", &ri_object))
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
        else
        {
            printf("ri key not found\n");
        }
    }
    else
    {
        printf("m2m:ae key not found\n");
    }

    // Clean up JSON objects and free memory
    json_object_put(root);
    free((char *)jsonBody);

    // PARSE X-M2M-ORIGIN HEADER (PI)
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

        size_t pi_length = origin_value_end - origin_value_start;
        char *temp_pi = (char *)malloc(pi_length + 1);
        if (temp_pi == NULL)
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

            free((char *)pi);
            return;
        }
        strncpy(temp_pi, origin_value_start, pi_length);
        temp_pi[pi_length] = '\0'; // Null-terminate the string

        if (strcmp(pi, temp_pi) != 0)
        {

            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value is not valid (%s).\"}", temp_pi);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                coap_add_data(params->coap_response, strlen(temp_pi), (const uint8_t *)temp_pi);
            }

            free((char *)pi);
            free(temp_pi);
            return;
        }
        free(temp_pi);
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
                if (ty_value != 3)
                {
                    
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Error: Invalid ty value
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ty value must be 3.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"ty value must be 3.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
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
            ty = 3;
        }
    }
    else
    {
        ty = 3;
    }

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
            const char *error_msg = "{\"error\":\"JSON body incorrectly formatted.\"}";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }

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
        free((char *)pi);
        return;
    }

    // Retrieve values from JSON object
    json_object *cnt_object;
    if (json_object_object_get_ex(root, "m2m:cnt", &cnt_object))
    {
        json_object_object_foreach(cnt_object, key, val)
        {
            if (strcmp(key, "ri") == 0)
            {
                ri = strdup(json_object_get_string(val));

                if (ri == NULL || strlen(ri) == 0)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where rn is NULL or an empty string
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ri value cannot be empty.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"ri value cannot be empty.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }

                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                if (!is_valid_string_plus_three_extra_chars(ri))
                {
                   
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                    // Handle the case where rn is NULL or an empty string
                    snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ri value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", ri);

                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        char coap_error_msg[256];  // Buffer sized for long RI values
                        snprintf(coap_error_msg, sizeof(coap_error_msg),"{\"error\":\"ri value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}",ri);
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                    }

                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                // Check if ri already exists
                sqlite3_stmt *stmt;
                const char *sql = "SELECT * FROM resources WHERE ri = ?";

                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);

                // Execute the statement
                rc = sqlite3_step(stmt);

                // If a Container is found with the same ri, return error
                if (rc == SQLITE_ROW)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where rn is NULL or an empty string
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ri value must be unique (%s).\"}", ri);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        char coap_error_msg[256];
                        snprintf(coap_error_msg, sizeof(coap_error_msg),
                            "{\"error\":\"ri value must be unique (%s).\"}",
                            ri);
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                    }
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                sqlite3_finalize(stmt);
            }
            else if (strcmp(key, "rn") == 0)
            {
                rn = strdup(json_object_get_string(val));

                if (rn == NULL || strlen(rn) == 0)
                {
                    
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where rn is NULL or an empty string
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value cannot be empty.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"rn value cannot be empty.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                if (!is_valid_string_plus_three_extra_chars(rn))
                {

                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where rn is NULL or an empty string
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", rn);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        char coap_error_msg[256];  // Buffer sized for long resource names
                        snprintf(coap_error_msg, sizeof(coap_error_msg),
                            "{\"error\":\"rn value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}",
                            rn);
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                // Check if rn already exists
                sqlite3_stmt *stmt;
                const char *sql =
                    "SELECT * "
                    "FROM resources "
                    "WHERE rn = ? "
                    "AND pi IN ( "
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
                    ")";

                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, csebase_name, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, ae_name, -1, SQLITE_STATIC);

                // Execute the statement
                rc = sqlite3_step(stmt);

                // If a Container is found with the same rn, return error
                if (rc == SQLITE_ROW)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where rn is NULL or an empty string
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value must be unique (%s).\"}", rn);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        char coap_error_msg[256];  // Buffer sized for long resource names
                        snprintf(coap_error_msg, sizeof(coap_error_msg),
                            "{\"error\":\"rn value must be unique (%s).\"}",
                            rn);
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                    }

                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
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
                            // Handle the case where rn is NULL or an empty string
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value format should be 'YYYY-MM-DD HH:MM:SS'.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"et value format should be 'YYYY-MM-DD HH:MM:SS'.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }

                    // Check if the datetime is valid
                    if (!is_valid_datetime(et))
                    {

                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where rn is NULL or an empty string
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value is not a valid date.\"}");

                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"et value is not a valid date.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }

                    // Check if the datetime is in the future
                    if (!is_future_datetime(et))
                    {
                        
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where rn is NULL or an empty string
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"et value is not in the future.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"et value is not in the future.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                }
            }
            else if (strcmp(key, "mbs") == 0)
            {
                if (json_object_is_type(val, json_type_int))
                {
                    mbs = json_object_get_int64(val);

                    // Check if the conversion was successful and the entire string was valid
                    if (mbs <= 0 || mbs > INT_MAX)
                    {
                        

                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where mbs is not within a valid range
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mbs value must be between 1 and %d.\"}", INT_MAX);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char error_msg[100];
                            snprintf(error_msg, sizeof(error_msg), "{\"error\":\"mbs value must be between 1 and %d.\"}", INT_MAX);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                }
                else
                {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                             // Handle the case where val is not an integer
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mbs value must be a number.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"mbs value must be a number.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                    
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "mia") == 0)
            {
                if (json_object_is_type(val, json_type_int))
                {
                    mia = json_object_get_int64(val);

                    // Check if the conversion was successful and the entire string was valid
                    if (mia <= 0 || mia > INT_MAX)
                    {
                        
                        
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where mia is not within a valid range
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mia value must be between 1 and %d.\"}", INT_MAX);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char error_msg[100];
                            snprintf(error_msg, sizeof(error_msg), "{\"error\":\"mia value must be between 1 and %d.\"}", INT_MAX);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                }
                else
                {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where val is not an integer
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mia value must be a number.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"mia value must be a number.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                    

                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "mni") == 0)
            {
                if (json_object_is_type(val, json_type_int))
                {
                    mni = json_object_get_int64(val);

                    // Check if the conversion was successful and the entire string was valid
                    if (mni <= 0 || mni > INT_MAX)
                    {
                        

                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where mni is not within a valid range
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mni value must be between 1 and %d.\"}", INT_MAX);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char coap_error_msg[128];
                            snprintf(coap_error_msg, sizeof(coap_error_msg),
                                "{\"error\":\"mni value must be between 1 and %d.\"}",
                                INT_MAX);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                }
                else
                {
                   
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                         // Handle the case where val is not an integer
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mni value must be a number.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"mni value must be a number.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }

                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "lbl") == 0)
            {
                if (!json_object_is_type(val, json_type_array))
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value format must be an array of strings.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"lbl value format must be an array of strings.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }

                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > LABELS_NUMBER)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value must not surpass %d elements.\"}", LABELS_NUMBER);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        char coap_error_msg[128];
                        snprintf(coap_error_msg, sizeof(coap_error_msg),
                            "{\"error\":\"lbl value must not surpass %d elements.\"}",
                            LABELS_NUMBER);
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                    }

                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                for (int i = 0; i < array_length; i++)
                {
                    json_object *label = json_object_array_get_idx(val, i);
                    if (!json_object_is_type(label, json_type_string))
                    {
                        
                        
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements must be strings.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"lbl value elements must be strings.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }

                    const char *label_str = json_object_get_string(label);
                    if (label_str == NULL || strlen(label_str) == 0)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements cannot be empty.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"lbl value elements cannot be empty.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }

                    // Check forplicates
                    for (int j = 0; j < i; j++)
                    {
                        if (strcmp(lbl[j], label_str) == 0)
                        {
                            
                            

                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"lbl value elements cannot be duplicated.\"}";
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                            }

                            sqlite3_close(db);
                            json_object_put(root);
                            free((char *)pi);
                            if (rn != NULL)
                                free((char *)rn);
                            if (et != NULL)
                                free((char *)et);
                            if (ri != NULL)
                                free((char *)ri);
                            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                            {
                                free((char *)lbl[i]);
                            }
                            return;
                        }
                    }

                    lbl[i] = strdup(label_str);
                }
            }
        }
    }
    else
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"'m2m:cnt' object not found in JSON.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"'m2m:cnt' object not found in JSON.\"}";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }

        sqlite3_close(db);
        json_object_put(root);
        free((char *)pi);
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
                

                if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                {
                   // Memory allocation failed
                    snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
                    write(params->http_socket, response, strlen(response));
                }
                else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                {
                    coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
                }

                sqlite3_close(db);
                json_object_put(root);
                free((char *)pi);
                if (rn != NULL)
                    free((char *)rn);
                if (et != NULL)
                    free((char *)et);
                if (ri != NULL)
                    free((char *)ri);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
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
                
                if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                {
                   // Memory allocation failed
                    snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
                    write(params->http_socket, response, strlen(response));
                }
                else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                {
                    coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
                }

                sqlite3_close(db);
                json_object_put(root);
                free((char *)pi);
                if (rn != NULL)
                    free((char *)rn);
                if (et != NULL)
                    free((char *)et);
                if (ri != NULL)
                    free((char *)ri);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
                return;
            }

            sqlite3_stmt *stmt;
            const char *sql =
                "SELECT * "
                "FROM resources "
                "WHERE rn = ? "
                "AND pi IN ( "
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
                ")";

            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

            sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, csebase_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, ae_name, -1, SQLITE_STATIC);

            // Execute the statement
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } while (rc == SQLITE_ROW);
    }

    if (mbs == -1)
    {
        mbs = MBS_DEFAULT;
    }
    if (mia == -1)
    {
        mia = MIA_DEFAULT;
    }
    if (mni == -1)
    {
        mni = MNI_DEFAULT;
    }

    printf("pi: %s\n", pi);
    printf("ty: %d\n", ty);
    printf("ri: %s\n", ri);
    printf("rn: %s\n", rn);
    printf("et: %s\n", et);
    printf("mbs: %lld\n", mbs);
    printf("mia: %lld\n", mia);
    printf("mni: %lld\n", mni);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        printf("lbl[%d]: %s\n", i, lbl[i]);
    }

    // WRITE TO DB------------------------------------------

    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

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
        fprintf(stderr, "Failed to insert into containers table: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);

        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            // Send error response to client
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        
        json_object_put(root);
        free((char *)pi);
        if (rn != NULL)
            free((char *)rn);
        if (et != NULL)
            free((char *)et);
        if (ri != NULL)
            free((char *)ri);
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            free((char *)lbl[i]);
        }
        return;
    }

    // Write to containers table
    // SQL statement for containers table
    const char *sql_template_cnt;

    if (et != NULL)
    {
        sql_template_cnt = "INSERT INTO containers (ri, st, cni, cbs, mbs, mia, mni, et) "
                           "VALUES (?, 0, 0, 0, ?, ?, ?, ?)";
    }
    else
    {
        sql_template_cnt = "INSERT INTO containers (ri, st, cni, cbs, mbs, mia, mni) "
                           "VALUES (?, 0, 0, 0, ?, ?, ?)";
    }

    // Prepare SQL statement
    sqlite3_stmt *stmt_cnt;
    sqlite3_prepare_v2(db, sql_template_cnt, -1, &stmt_cnt, NULL);

    sqlite3_bind_text(stmt_cnt, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt_cnt, 2, mbs);
    sqlite3_bind_int64(stmt_cnt, 3, mia);
    sqlite3_bind_int64(stmt_cnt, 4, mni);
    if (et != NULL)
    {
        sqlite3_bind_text(stmt_cnt, 5, et, -1, SQLITE_STATIC);
    }
    int rc_cnt = sqlite3_step(stmt_cnt);

    // Finalize statement
    sqlite3_finalize(stmt_cnt);

    if (rc_cnt != SQLITE_DONE)
    {
        fprintf(stderr, "Failed to insert into containers table: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);


        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            // Send error response to client
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }

        json_object_put(root);
        free((char *)pi);
        if (rn != NULL)
            free((char *)rn);
        if (et != NULL)
            free((char *)et);
        if (ri != NULL)
            free((char *)ri);
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            free((char *)lbl[i]);
        }
        return;
    }

    // Write to labels table
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        // SQL statement for labels table
        const char *sql_template_lb = "INSERT INTO labels (ri, lbl) VALUES (?, ?)";

        // Prepare SQL statement
        sqlite3_stmt *stmt_lb;
        sqlite3_prepare_v2(db, sql_template_lb, -1, &stmt_lb, NULL);

        sqlite3_bind_text(stmt_lb, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_lb, 2, lbl[i], -1, SQLITE_STATIC);
        int rc_lb = sqlite3_step(stmt_lb);

        // Finalize statement
        sqlite3_finalize(stmt_lb);

        if (rc_lb != SQLITE_DONE)
        {
            fprintf(stderr, "Failed to insert into labels table: %s\n", sqlite3_errmsg(db));
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            json_object_put(root);
            free((char *)pi);
            if (rn != NULL)
                free((char *)rn);
            if (et != NULL)
                free((char *)et);
            if (ri != NULL)
                free((char *)ri);
            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
            {
                free((char *)lbl[i]);
            }
            return;
        }
    }

    // Commit transaction
    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);
    printf("Transaction completed successfully.\n");
    sqlite3_close(db);
    json_object_put(root);
    free((char *)pi);
    free((char *)ri);
    free((char *)et);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        free((char *)lbl[i]);
    }

    // Allocate memory for the destination string
    char *rn_copy = NULL;
    rn_copy = strdup(rn); // Use strdup toplicate strings
    if (rn_copy == NULL)
    {


        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            // Memory allocation failed
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
    jsonBody = handle_request_container_get(params, csebase_name, ae_name, rn_copy, "POST");
    if (jsonBody != NULL)
    {
        free(jsonBody);
    }

    // Free the allocated memory for rn_copy
    free(rn_copy);
}

void handle_request_container_put(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request, char *body)
{
    char response[BUFFER_SIZE] = {0};
    char *err_msg = 0;
    sqlite3 *db;
    int rc;

    const char *pi = NULL;
    const char *ri = NULL;
    const char *et = NULL;
    int mbs = -1;
    int mia = -1;
    int mni = -1;
    const char *lbl[LABELS_NUMBER] = {NULL};
    int has_et = 0;
    int has_lbl = 0;

    // CHECK IF csebase_name, ae and container EXIST
    char *jsonBody = handle_request_container_get(params, csebase_name, ae_name, container_name, "INTERNAL");
    if (jsonBody[0] == '\0')
    {
        printf("jsonBody was empty\n");
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            // Handle absence of container
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_NOT_FOUND);
        }
        free((char *)jsonBody);
        return;
    }

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
                ri = strdup(ri_str);
            }
        }
        if (json_object_object_get_ex(cnt_object, "pi", &pi_object))
        {
            const char *pi_str = json_object_get_string(pi_object);
            if (pi_str != NULL)
            {
                pi = strdup(pi_str);
            }
        }
    }
    // Clean up JSON objects and free memory
    json_object_put(root);
    free((char *)jsonBody);

    // PARSE X-M2M-ORIGIN HEADER (PI)
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

        size_t pi_length = origin_value_end - origin_value_start;
        char *temp_pi = (char *)malloc(pi_length + 1);
        if (temp_pi == NULL)
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
            free((char *)pi);
            free((char *)ri);
            return;
        }
        strncpy(temp_pi, origin_value_start, pi_length);
        temp_pi[pi_length] = '\0'; // Null-terminate the string

        if (strcmp(pi, temp_pi) != 0)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value is not valid (%s).\"}", temp_pi);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                char coap_error_msg[256];  // Increased buffer size for potential long origin values
                snprintf(coap_error_msg, sizeof(coap_error_msg),
                    "{\"error\":\"X-M2M-Origin header value is not valid (%s).\"}",
                    temp_pi);
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
            }
            free((char *)pi);
            free((char *)ri);
            free(temp_pi);
            return;
        }
        free(temp_pi);
    }

    free((char *)pi);

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
                if (ty_value != 3)
                {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Error: Invalid ty value
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ty value must be 3.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"ty value must be 3.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }

                    sqlite3_close(db);
                    free((char *)ri);
                    return;
                }
            }
        }
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
        sqlite3_close(db);
        free((char *)ri);
        return;
    }

    // Parse JSON Body
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
            const char *error_msg = "{\"error\":\"JSON body incorrectly formatted.\"}";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        free((char *)ri);
        return;
        }

    // Retrieve values from JSON object
    json_object *cnt_object_body;
    if (json_object_object_get_ex(root, "m2m:cnt", &cnt_object_body))
    {
        json_object_object_foreach(cnt_object_body, key, val)
        {
            if (strcmp(key, "et") == 0)
            {
                has_et = 1;
                if (json_object_is_type(val, json_type_null))
                {
                    et = NULL;
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
                            const char *error_msg = "{\"error\":\"et value format should be 'YYYY-MM-DD HH:MM:SS'.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        free((char *)ri);
                        return;
                    }
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
                            const char *error_msg = "{\"error\":\"et value is not a valid date.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
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
                            const char *error_msg = "{\"error\":\"et value is not in the future.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;

                }
            }
            else if (strcmp(key, "mbs") == 0)
            {
                if (json_object_is_type(val, json_type_int))
                {
                    mbs = json_object_get_int64(val);
                    // Check if the conversion was successful and the entire string was valid
                    if (mbs <= 0 || mbs > INT_MAX)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where mbs is not within a valid range
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mbs value must be between 1 and %d.\"}", INT_MAX);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char coap_error_msg[100];  // Buffer sized for typical INT_MAX values
                            snprintf(coap_error_msg, sizeof(coap_error_msg),"{\"error\":\"mbs value must be between 1 and %d.\"}", INT_MAX);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                        }

                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                }
                else
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        /// Handle the case where val is not an integer
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mbs value must be a number.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"mbs value must be a number.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "mia") == 0)
            {
                if (json_object_is_type(val, json_type_int))
                {
                    mia = json_object_get_int64(val);

                    // Check if the conversion was successful and the entire string was valid
                    if (mia <= 0 || mia > INT_MAX)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where mia is not within a valid range
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mia value must be between 1 and %d.\"}", INT_MAX);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char coap_error_msg[128];  // Buffer sized for INT_MAX (2147483647)
                            snprintf(coap_error_msg, sizeof(coap_error_msg),"{\"error\":\"mia value must be between 1 and %d.\"}",INT_MAX);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                } else {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where val is not an integer
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mia value must be a number.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"mia value must be a number.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "mni") == 0)
            {
                if (json_object_is_type(val, json_type_int))
                {
                    mni = json_object_get_int64(val);

                    // Check if the conversion was successful and the entire string was valid
                    if (mni <= 0 || mni > INT_MAX)
                    {
                        
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            // Handle the case where mni is not within a valid range
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mni value must be between 1 and %d.\"}", INT_MAX);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char coap_error_msg[128];  // Buffer sized for INT_MAX (2147483647)
                            snprintf(coap_error_msg, sizeof(coap_error_msg),
                                "{\"error\":\"mni value must be between 1 and %d.\"}",
                                INT_MAX);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                }
                else
                {
                    
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        // Handle the case where val is not an integer
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"mni value must be a number.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"mni value must be a number.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "lbl") == 0)
            {
                has_lbl = 1;
                if (!json_object_is_type(val, json_type_array))
                {
                    
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value format must be an array of strings.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"lbl value format must be an array of strings.\"}";
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > 5)
                {
                    
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value must not surpass %d elements.\"}", LABELS_NUMBER);
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            char coap_error_msg[128];  // Buffer sized for typical label counts
                            snprintf(coap_error_msg, sizeof(coap_error_msg),
                                "{\"error\":\"lbl value must not surpass %d elements.\"}",
                                LABELS_NUMBER);
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(coap_error_msg), (const uint8_t *)coap_error_msg);
                        }
                    sqlite3_close(db);
                    json_object_put(root);
                    if (et != NULL)
                        free((char *)et);
                    if (ri != NULL)
                        free((char *)ri);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    return;
                }

                for (int i = 0; i < array_length; i++)
                {
                    json_object *label = json_object_array_get_idx(val, i);
                    if (!json_object_is_type(label, json_type_string))
                    {
                        
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements must be strings.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"lbl value elements must be strings.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }

                    const char *lbl_str = json_object_get_string(label);
                    if (lbl_str == NULL || strlen(lbl_str) == 0)
                    {
                        
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements cannot be empty.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"lbl value elements cannot be empty.\"}";
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        if (et != NULL)
                            free((char *)et);
                        if (ri != NULL)
                            free((char *)ri);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        return;
                    }
                    // Check for duplicates
                    for (int j = 0; j < i; j++)
                    {
                        if (strcmp(lbl[j], lbl_str) == 0)
                        {
                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"lbl value elements cannot be duplicated.\"}";
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                            }
                            sqlite3_close(db);
                            json_object_put(root);
                            if (et != NULL)
                                free((char *)et);
                            if (ri != NULL)
                                free((char *)ri);
                            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                            {
                                free((char *)lbl[i]);
                            }
                            return;
                        }
                    }
                    lbl[i] = strdup(lbl_str);
                }
            }
        }
    }
    else
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"'m2m:cnt' object not found in JSON.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"'m2m:cnt' object not found in JSON.\"}";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ri);
        return;
    }

    printf("ri:  %s\n", ri);
    printf("et:  %s\n", et);
    printf("mbs:  %d\n", mbs);
    printf("mia:  %d\n", mia);
    printf("mni:  %d\n", mni);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        printf("lbl[i]: %s\n", lbl[i]);
    }

    if (has_et == 0 && mbs == -1 && mia == -1 && mni == -1 && has_lbl == 0)
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            // No update needed if nothing was provided
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"nothing to update.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"nothing to update.\"}";
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ri);
        return;
    }

    // WRITE TO DB------------------------------------------
    // Begin transaction
    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    if (has_et == 1 || mbs != -1 || mia != -1 || mni != -1)
    {
        // SQL statement for resources table
        const char *sql_template_rs = "UPDATE resources SET lt = datetime('now', 'localtime') WHERE ri = ?";

        // Prepare SQL statement
        sqlite3_stmt *stmt_rs;
        sqlite3_prepare_v2(db, sql_template_rs, -1, &stmt_rs, NULL);

        sqlite3_bind_text(stmt_rs, 1, ri, -1, SQLITE_STATIC);

        int rc_rs = sqlite3_step(stmt_rs);

        sqlite3_finalize(stmt_rs);

        if (rc_rs != SQLITE_DONE)
        {
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
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
            free((char *)ri);
            if (et != NULL)
                free((char *)et);
            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
            {
                free((char *)lbl[i]);
            }
            return;
        }

        char sql_template_cnt[BUFFER_SIZE];
        snprintf(sql_template_cnt, sizeof(sql_template_cnt), "UPDATE containers SET st = st + 1");

        if (mbs != -1)
        {
            strncat(sql_template_cnt, ", mbs = ?", sizeof(sql_template_cnt) - strlen(sql_template_cnt) - 1);
        }
        if (mia != -1)
        {
            strncat(sql_template_cnt, ", mia = ?", sizeof(sql_template_cnt) - strlen(sql_template_cnt) - 1);
        }
        if (mni != -1)
        {
            strncat(sql_template_cnt, ", mni = ?", sizeof(sql_template_cnt) - strlen(sql_template_cnt) - 1);
        }
        if (has_et == 1)
        {
            strncat(sql_template_cnt, ", et = ?", sizeof(sql_template_cnt) - strlen(sql_template_cnt) - 1);
        }
        strncat(sql_template_cnt, " WHERE ri = ?", sizeof(sql_template_cnt) - strlen(sql_template_cnt) - 1);

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, sql_template_cnt, -1, &stmt, NULL);

        int param_index = 1;

        if (mbs != -1)
        {
            sqlite3_bind_int(stmt, param_index++, mbs);
        }
        if (mia != -1)
        {
            sqlite3_bind_int(stmt, param_index++, mia);
        }
        if (mni != -1)
        {
            sqlite3_bind_int(stmt, param_index++, mni);
        }
        if (has_et == 1)
        {
            sqlite3_bind_text(stmt, param_index++, et, -1, SQLITE_STATIC);
        }
        sqlite3_bind_text(stmt, param_index++, ri, -1, SQLITE_STATIC);

        int rc = sqlite3_step(stmt);

        sqlite3_finalize(stmt);
    }

    if (has_lbl == 1)
    {
        // DELETE statement for labels table
        const char *sql_lbl = "DELETE FROM labels WHERE ri = ?";
        sqlite3_stmt *stmt_lbl;

        sqlite3_prepare_v2(db, sql_lbl, -1, &stmt_lbl, NULL);

        sqlite3_bind_text(stmt_lbl, 1, ri, -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt_lbl);

        sqlite3_finalize(stmt_lbl);

        // Write to labels table
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            // SQL statement for labels table
            const char *sql_template_lb = "INSERT INTO labels (ri, lbl) VALUES (?, ?)";

            // Prepare SQL statement
            sqlite3_stmt *stmt_lb;
            sqlite3_prepare_v2(db, sql_template_lb, -1, &stmt_lb, NULL);

            sqlite3_bind_text(stmt_lb, 1, ri, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt_lb, 2, lbl[i], -1, SQLITE_STATIC);

            int rc_lb = sqlite3_step(stmt_lb);

            sqlite3_finalize(stmt_lb);

            if (rc_lb != SQLITE_DONE)
            {
                sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
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
                free((char *)ri);
                if (et != NULL)
                    free((char *)et);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
                return;
            }
        }
    }

    // Commit transaction
    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);

    // Close the SQLite database
    sqlite3_close(db);

    printf("Transaction completed successfully.\n");

    char resource_uri[512];
    snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s", csebase_name, ae_name, container_name);

    // Construir o JSON do container atualizado para a notificação
    json_object *notification_content = json_object_new_object();
    json_object *container_obj = json_object_new_object();

    json_object_object_add(container_obj, "rn", json_object_new_string(container_name));
    json_object_object_add(container_obj, "ri", json_object_new_string(ri));
    json_object_object_add(container_obj, "ty", json_object_new_int(3)); // container type
    if (et != NULL)
    {
        json_object_object_add(container_obj, "et", json_object_new_string(et));
    }

    // Adicionar labels se existirem
    if (lbl[0] != NULL)
    {
        json_object *labels_array = json_object_new_array();
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            json_object_array_add(labels_array, json_object_new_string(lbl[i]));
        }
        json_object_object_add(container_obj, "lbl", labels_array);
    }

    json_object_object_add(notification_content, "m2m:cnt", container_obj);
    const char *notification_json = json_object_to_json_string(notification_content);

    // Enviar notificação para evento Update_of_Resource (net=1)
#ifdef ENABLE_MQTT
    handle_mqtt_notification(resource_uri, body, 1);
#endif

    // Limpar objeto JSON da notificação
    json_object_put(notification_content);

    // Free the JSON object
    json_object_put(root);

    // Free allocated memory
    free((char *)ri);
    if (et != NULL)
        free((char *)et);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        free((char *)lbl[i]);
    }

    printf("GET /%s/%s/%s/\n", csebase_name, ae_name, container_name);

    // RETURN THE ROW INSERTED BACK TO THE CLIENT WITH ALL THE DATA
    jsonBody = handle_request_container_get(params, csebase_name, ae_name, container_name, "PUT");
    if (jsonBody != NULL)
    {
        free(jsonBody);
    }
}

void handle_request_container_delete(struct response_params *params, char *csebase_name, char *ae_name, char *container_name, char *request_type, sqlite3 *db)
{
    char response[BUFFER_SIZE] = {0};
    int rc;
    char *jsonBody;
    char *container_data_json = NULL;

    if (strcmp(request_type, "DELETE") == 0)
    {
        // CHECK IF container_name EXISTS
        jsonBody = handle_request_container_get(params, csebase_name, ae_name, container_name, "INTERNAL");

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

        // ========== NOTIFICAÇÃO ==========

        // Primeiro, vamos verificar a estrutura da tabela resources
        sqlite3_stmt *pragma_stmt;
        const char *pragma_sql = "PRAGMA table_info(resources)";
        if (sqlite3_prepare_v2(db, pragma_sql, -1, &pragma_stmt, 0) == SQLITE_OK)
        {
            while (sqlite3_step(pragma_stmt) == SQLITE_ROW)
            {
                const char *col_name = (const char *)sqlite3_column_text(pragma_stmt, 1);
            }
            sqlite3_finalize(pragma_stmt);
        }
        // Obter dados do container que vai ser eliminado (sem coluna et por agora)
        sqlite3_stmt *stmt_read;
        const char *sql_read =
            "SELECT r.ri, r.rn "
            "FROM containers c "
            "JOIN resources r ON c.ri = r.ri "
            "WHERE r.pi IN ( "
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
            "AND r.rn = ?";

        int prepare_result = sqlite3_prepare_v2(db, sql_read, -1, &stmt_read, 0);

        if (prepare_result == SQLITE_OK)
        {
            sqlite3_bind_text(stmt_read, 1, csebase_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt_read, 2, ae_name, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt_read, 3, container_name, -1, SQLITE_STATIC);

            int step_result = sqlite3_step(stmt_read);

            if (step_result == SQLITE_ROW)
            {
                const char *ri = (const char *)sqlite3_column_text(stmt_read, 0);
                const char *rn = (const char *)sqlite3_column_text(stmt_read, 1);
                const char *et = NULL; // Por agora não vamos buscar o et

                // Construir JSON do container antes da eliminação
                json_object *notification_content = json_object_new_object();
                json_object *container_obj = json_object_new_object();

                json_object_object_add(container_obj, "rn", json_object_new_string(rn));
                json_object_object_add(container_obj, "ri", json_object_new_string(ri));
                json_object_object_add(container_obj, "ty", json_object_new_int(3)); // container type

                // Tentar obter et de uma tabela separada (containers)
                sqlite3_stmt *stmt_et;
                const char *sql_et = "SELECT et FROM containers WHERE ri = ?";
                if (sqlite3_prepare_v2(db, sql_et, -1, &stmt_et, 0) == SQLITE_OK)
                {
                    sqlite3_bind_text(stmt_et, 1, ri, -1, SQLITE_STATIC);
                    if (sqlite3_step(stmt_et) == SQLITE_ROW)
                    {
                        const char *et_value = (const char *)sqlite3_column_text(stmt_et, 0);
                        if (et_value != NULL)
                        {
                            json_object_object_add(container_obj, "et", json_object_new_string(et_value));
                        }
                    }
                    sqlite3_finalize(stmt_et);
                }
                
                // Obter labels se existirem
                sqlite3_stmt *stmt_labels;
                const char *sql_labels = "SELECT lbl FROM labels WHERE ri = ?";
                if (sqlite3_prepare_v2(db, sql_labels, -1, &stmt_labels, 0) == SQLITE_OK)
                {
                    sqlite3_bind_text(stmt_labels, 1, ri, -1, SQLITE_STATIC);

                    json_object *labels_array = json_object_new_array();
                    while (sqlite3_step(stmt_labels) == SQLITE_ROW)
                    {
                        const char *label = (const char *)sqlite3_column_text(stmt_labels, 0);
                        json_object_array_add(labels_array, json_object_new_string(label));
                    }

                    if (json_object_array_length(labels_array) > 0)
                    {
                        json_object_object_add(container_obj, "lbl", labels_array);
                    }
                    else
                    {
                        json_object_put(labels_array);
                    }
                    sqlite3_finalize(stmt_labels);
                }
               
                json_object_object_add(notification_content, "m2m:cnt", container_obj);
                container_data_json = strdup(json_object_to_json_string(notification_content));
                if (container_data_json != NULL) 
                {                 
                    // Construir a URI do recurso que vai ser eliminado
                    char resource_uri[512];
                    snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s", csebase_name, ae_name, container_name);

                    printf("A Enviar notificação de DELETE_OF_RESOURCE");

#ifdef ENABLE_MQTT
                    handle_mqtt_notification(resource_uri, "", 2);
#endif

                    free(container_data_json);
            }
            json_object_put(notification_content);
            }
            else
            {
                printf("Erro SQL: %s\n", sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt_read);
        }
        else
        {
            printf("ERRO ao preparar query de leitura: %s\n", sqlite3_errmsg(db));
        }
        // ========== FIM DA NOTIFICAÇÃO ==========

        // Begin transaction
        sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);
    }

    // Delete associated content instances
    const char *sql_query =
        "SELECT r.rn "
        "FROM content_instances ci "
        "JOIN resources r ON ci.ri = r.ri "
        "WHERE r.pi IN ( "
            "SELECT r.ri "
            "FROM containers c "
            "JOIN resources r ON c.ri = r.ri "
            "WHERE r.pi IN ( "
                "SELECT r2.ri "
                "FROM resources r2 "
                "JOIN application_entities ae ON r2.ri = ae.ri "
                "WHERE r2.pi IN ( "
                    "SELECT cb.ri "
                    "FROM cse_bases cb "
                    "JOIN resources r3 ON cb.ri = r3.ri "
                    "WHERE r3.rn = ? "
                ") "
                "AND r2.rn = ? "
            ")"
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);

    sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, container_name, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        // Fetch cin_name (rn) from the result set
        char *cin_name = (char *)sqlite3_column_text(stmt, 0);

        // Call handle_request_cin_delete function for each cin
        handle_request_cin_delete(params, csebase_name, ae_name, container_name, cin_name, "INTERNAL", db);
    }
    sqlite3_finalize(stmt);

    // DELETE statement for labels table
    const char *sql_lbl =
        "DELETE FROM labels "
        "WHERE ri IN ( "
            "SELECT c.ri "
            "FROM containers c "
            "JOIN resources r ON c.ri = r.ri "
            "WHERE r.pi IN ( "
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
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_lbl;

    sqlite3_prepare_v2(db, sql_lbl, -1, &stmt_lbl, NULL);

    sqlite3_bind_text(stmt_lbl, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_lbl, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_lbl, 3, container_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_lbl);

    sqlite3_finalize(stmt_lbl);

    // DELETE statement for containers table
    const char *sql_cnt =
        "DELETE FROM containers "
        "WHERE ri IN ( "
            "SELECT c.ri "
            "FROM containers c "
            "JOIN resources r ON c.ri = r.ri "
            "WHERE r.pi IN ( "
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
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_cnt;

    sqlite3_prepare_v2(db, sql_cnt, -1, &stmt_cnt, NULL);

    sqlite3_bind_text(stmt_cnt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_cnt, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_cnt, 3, container_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_cnt);

    sqlite3_finalize(stmt_cnt);

    // DELETE statement for resources table
    const char *sql_rs =
        "DELETE FROM resources "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "WHERE r.pi IN ( "
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
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_rs;

    sqlite3_prepare_v2(db, sql_rs, -1, &stmt_rs, NULL);

    sqlite3_bind_text(stmt_rs, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_rs, 2, ae_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_rs, 3, container_name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt_rs);

    sqlite3_finalize(stmt_rs);

    if (strcmp(request_type, "DELETE") == 0)
    {
        // Commit transaction
        sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);

        // Close the SQLite database
        sqlite3_close(db);

        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", jsonBody);
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_DELETED);
            coap_add_data(params->coap_response, strlen(jsonBody), (const uint8_t *)jsonBody);
        }

        // Free allocated memory
        free(jsonBody);
    }
}
