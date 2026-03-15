/*
AUTHORSHIP -------------------------------------------------

 * File:    ae.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions necessary for the AE.

*/

#include "../include/ae.h"

char *handle_request_ae_get(struct response_params *params, char *csebase_name, char *ae_name, char *request_type)
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
        "ae.api, ae.aei, ae.rr, ae.et "
        "FROM application_entities ae "
        "JOIN resources r ON ae.ri = r.ri "
        "WHERE r.pi IN ("
            "SELECT r2.ri "
            "FROM resources r2 "
            "JOIN cse_bases cb ON r2.ri = cb.ri "
            "WHERE r2.rn = ? "
            ") "
        "AND r.rn = ?";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    // Bind parameters for the SQL statement
    sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ae_name, -1, SQLITE_STATIC);

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

        snprintf(jsonBody, BUFFER_SIZE, "{\"m2m:ae\": {");

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
            else if (strcmp(column_name, "rr") == 0 || strcmp(column_name, "ty") == 0)
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
                "SELECT r.ri "
                "FROM resources r "
                "JOIN application_entities ae ON r.ri = ae.ri "
                "WHERE r.pi IN ( "
                    "SELECT r2.ri "
                    "FROM resources r2 "
                    "JOIN cse_bases cb ON r2.ri = cb.ri "
                    "WHERE r2.rn = ? "
                ") "
                "AND r.rn = ? "
            ")";

        sqlite3_stmt *stmt_lbl;
        sqlite3_prepare_v2(db, sql_lbl, -1, &stmt_lbl, NULL);

        sqlite3_bind_text(stmt_lbl, 1, csebase_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_lbl, 2, ae_name, -1, SQLITE_STATIC);

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

        // Retrieve poa values from the points_of_access table
        char *sql_poa =
            "SELECT poa "
            "FROM points_of_access "
            "WHERE ri IN ("
                "SELECT ae.ri "
                "FROM application_entities ae "
                "JOIN resources r ON ae.ri = r.ri "
                "WHERE r.pi IN ("
                    "SELECT r2.ri "
                    "FROM resources r2 "
                    "JOIN cse_bases cb ON r2.ri = cb.ri "
                    "WHERE r2.rn = ?"
                ") "
                "AND r.rn = ?"
            ")";

        sqlite3_stmt *stmt_poa;
        sqlite3_prepare_v2(db, sql_poa, -1, &stmt_poa, NULL);

        sqlite3_bind_text(stmt_poa, 1, csebase_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_poa, 2, ae_name, -1, SQLITE_STATIC);
        
        strcat(jsonBody, ", \"poa\": [");
        while (sqlite3_step(stmt_poa) == SQLITE_ROW)
        {
            const char *poa_value = (const char *)sqlite3_column_text(stmt_poa, 0);
            snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "\"%s\", ", poa_value);
        }
        if (strlen(jsonBody) >= 2 && jsonBody[strlen(jsonBody) - 2] == ',')
        {
            jsonBody[strlen(jsonBody) - 2] = '\0';
        }
        strcat(jsonBody, "]");

        // Finalize the statement for poa retrieval
        sqlite3_finalize(stmt_poa);

        // Retrieve srv values from the supported_release_versions table
        char *sql_srv =
            "SELECT srv "
            "FROM supported_release_versions "
            "WHERE ri IN ("
                "SELECT ae.ri "
                "FROM application_entities ae "
                "JOIN resources r ON ae.ri = r.ri "
                "WHERE r.pi IN ("
                    "SELECT r2.ri "
                    "FROM resources r2 "
                    "JOIN cse_bases cb ON r2.ri = cb.ri "
                    "WHERE r2.rn = ?"
                ") "
                "AND r.rn = ?"
            ")";

        sqlite3_stmt *stmt_srv;
        sqlite3_prepare_v2(db, sql_srv, -1, &stmt_srv, NULL);

        sqlite3_bind_text(stmt_srv, 1, csebase_name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_srv, 2, ae_name, -1, SQLITE_STATIC);
        
        strcat(jsonBody, ", \"srv\": [");
        while (sqlite3_step(stmt_srv) == SQLITE_ROW)
        {
            const char *srv_value = (const char *)sqlite3_column_text(stmt_srv, 0);
            snprintf(jsonBody + strlen(jsonBody), BUFFER_SIZE - strlen(jsonBody), "\"%s\", ", srv_value);
        }
        if (strlen(jsonBody) >= 2 && jsonBody[strlen(jsonBody) - 2] == ',')
        {
            jsonBody[strlen(jsonBody) - 2] = '\0';
        }
        strcat(jsonBody, "]");

        // Finalize the statement for srv retrieval
        sqlite3_finalize(stmt_srv);

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
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_add_data(params->coap_response, strlen(response), (const uint8_t *)response);
        }
    }
    else if (strcmp(request_type, "POST") == 0)
    {
        free(ae_name);
    }
    return strdup(jsonBody); // Return a copy of jsonBody
}

void handle_request_ae_post(struct response_params *params, char *csebase_name, char *request, char *body)
{
    char response[BUFFER_SIZE] = {0};
    char *err_msg = 0;
    sqlite3 *db;
    int rc;

    // Variables to store values
    const char *ri = NULL;
    const char *aei = NULL;
    int ty = -1; // Initialize to an invalid value
    int rr = -1; // Initialize to an invalid value
    const char *api = NULL;
    const char *rn = NULL;
    const char *et = NULL;
    const char *lbl[LABELS_NUMBER] = {NULL};
    const char *poa[POAS_NUMBER] = {NULL};
    const char *srv[SRVS_NUMBER] = {NULL};

    // CHECK IF csebase_name EXISTS
    const char *pi = handle_request_csebase_get(params, csebase_name, "INTERNAL");

    if (pi == NULL)
    {
        // Handle absence of resource ID
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_NOT_FOUND);
        }
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
        return;
    }

    // PARSE X-M2M-ORIGIN HEADER (RI and AEI)
    const char *origin_header = strstr(request, "X-M2M-Origin:");
    if (!origin_header)
    {
        do
        {
            // If no header is provided, generate a random sequence and check if that random sequence ri already exists.
            ri = generate_random_sequence_C();
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
                free((char *)pi);
                return;
            }
            aei = strdup(ri); // Assign aei with the same value as ri
            if (aei == NULL)
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
                free((char *)pi);
                return;
            }

            sqlite3_stmt *stmt;
            const char *sql = "SELECT * FROM resources WHERE ri = ? OR aei = ?";

            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

            sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, aei, -1, SQLITE_STATIC);

            // Execute the statement
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } while (rc == SQLITE_ROW);
    }
    else
    {
        const char *origin_value_start = origin_header + strlen("X-M2M-Origin:");
        origin_value_start += strspn(origin_value_start, " \t"); // Skip leading whitespace
        const char *origin_value_end = origin_value_start + strcspn(origin_value_start, "\r\n");

        while (origin_value_end > origin_value_start && isspace(origin_value_end[-1])) // Skip trailing whitespace
        {
            origin_value_end--;
        }

        size_t ri_length = origin_value_end - origin_value_start;
        char *temp_ri = (char *)malloc(ri_length + 1);
        if (temp_ri == NULL)
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
            free((char *)pi);
            return;
        }
        strncpy(temp_ri, origin_value_start, ri_length);
        temp_ri[ri_length] = '\0'; // Null-terminate the string

        if (temp_ri[0] != 'S')
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value must start with the character 'S' (%s)\"}", temp_ri);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                const char *error_msg = "{\"error\":\"X-M2M-Origin header value must start with the character 'S' (%s)\"}", temp_ri;
                uint8_t buf[4]; //max size to encode any coap option value
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
            }
            
            sqlite3_close(db);
            free((char *)pi);
            free(temp_ri);
            return;
        }

        if (!is_valid_string_plus_three_extra_chars(temp_ri))
        {
            
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", temp_ri);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                const char *error_msg = "{\"error\":\"X-M2M-Origin header value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", temp_ri;
                uint8_t buf[4]; //max size to encode any coap option value
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
            }
            sqlite3_close(db);
            free((char *)pi);
            free(temp_ri);
            return;
        }

        ri = temp_ri;

        sqlite3_stmt *stmt;
        const char *sql = "SELECT * FROM resources WHERE ri = ?";

        sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

        sqlite3_bind_text(stmt, 1, ri, -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);

        sqlite3_finalize(stmt);

        if (rc == SQLITE_ROW)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value must be unique (%s).\"}", ri);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                const char *error_msg = "{\"error\":\"X-M2M-Origin header value must be unique (%s).\"}", ri;
                uint8_t buf[4]; //max size to encode any coap option value
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
            }
            sqlite3_close(db);
            free((char *)ri);
            free((char *)pi);
            return;
        }

        aei = strdup(ri); // Assign aei with the same value as ri
        if (aei == NULL)
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
            free((char *)pi);
            return;
        }
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
                if (ty_value != 2)
                {
                    // Error: Invalid ty value
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ty value must be '2'\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"ty value must be '2'\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    free((char *)ri);
                    free((char *)aei);
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
            ty = 2;
        }
    }
    else
    {
        ty = 2;
    }

    // Parse JSON Body
    json_object *root = json_tokener_parse(body);
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
            uint8_t buf[4]; //max size to encode any coap option value
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        free((char *)ri);
        free((char *)aei);
        free((char *)pi);
        return;
    }

    // Retrieve values from JSON object
    json_object *ae_object;
    if (json_object_object_get_ex(root, "m2m:ae", &ae_object))
    {
        json_object_object_foreach(ae_object, key, val)
        {
            if (strcmp(key, "rn") == 0)
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
                        const char *error_msg = "{\"error\":\"rn value cannot be empty.\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
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
                        const char *error_msg = "{\"error\":\"rn value must contain only alphanumeric characters, which may also include the characters '_', '-' and '.' (%s).\"}", rn;
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                // Check if rn already exists
                sqlite3_stmt *stmt;
                const char *sql =
                    "SELECT * "
                    "FROM resources "
                    "WHERE rn = ? "
                    "AND pi IN ("
                        "SELECT r.ri "
                        "FROM resources r "
                        "JOIN cse_bases cb ON r.ri = cb.ri "
                        "WHERE r.rn = ?"
                    ")";

                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, csebase_name, -1, SQLITE_STATIC);

                // Execute the statement
                rc = sqlite3_step(stmt);

                sqlite3_finalize(stmt);

                // If an AE is found with the same rn, return error
                if (rc == SQLITE_ROW)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rn value must be unique (%s).\"}", rn);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"rn value must be unique (%s).\"}", rn;
                        write(params->http_socket, response, strlen(response));
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "api") == 0)
            {
                api = strdup(json_object_get_string(val));

                // Check if api is NULL, empty, or doesn't start with 'R' or 'N'
                if (api == NULL || strlen(api) == 0 || (api[0] != 'R' && api[0] != 'N'))
                {
                    // Create an appropriate error message
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"api value must start with 'R' or 'N'.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"api value must start with 'R' or 'N'.\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                // Check if api already exists
                sqlite3_stmt *stmt;
                const char *sql =
                    "SELECT * "
                    "FROM application_entities "
                    "WHERE api = ? ";

                sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

                sqlite3_bind_text(stmt, 1, api, -1, SQLITE_STATIC);

                rc = sqlite3_step(stmt);

                sqlite3_finalize(stmt);

                // If an AE is found with the same api, return error
                if (rc == SQLITE_ROW)
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"api value must be unique (%s).\"}", rn);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"api value must be unique (%s).\"}", rn;
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }
            }
            else if (strcmp(key, "rr") == 0)
            {
                // First, check if the JSON value is valid and can be converted to an integer
                if (json_object_is_type(val, json_type_int))
                {
                    rr = json_object_get_int(val);

                    // Check if rr is either 0 or 1
                    if (rr != 0 && rr != 1)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rr value must be 0, 1, true, or false.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"rr value must be 0, 1, true, or false.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }
                }
                else if (json_object_is_type(val, json_type_boolean))
                {
                    rr = json_object_get_boolean(val); // This returns 1 for true and 0 for false
                }
                else
                {
                    // If the value is neither an integer nor a boolean, it's an invalid type
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rr value must be 0, 1, true, or false.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"rr value must be 0, 1, true, or false.\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }
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
                            const char *error_msg = "{\"error\":\"et value format should be 'YYYY-MM-DD HH:MM:SS'.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
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
                            const char *error_msg = "{\"error\":\"et value is not a valid date.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
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
                            write(params->http_socket, response, strlen(response));
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }
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
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > LABELS_NUMBER)
                {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),"{\"error\":\"lbl value must not surpass %d elements.\"}", LABELS_NUMBER);
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n%s",error_msg);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
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
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
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
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }

                    // Check for duplicates
                    for (int j = 0; j < i; j++) {
                        if (strcmp(lbl[j], label_str) == 0) {
                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"lbl value elements cannot be duplicated.\"}";
                                uint8_t buf[4]; //max size to encode any coap option value
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                            }
                            sqlite3_close(db);
                            json_object_put(root);
                            free((char *)ri);
                            free((char *)aei);
                            free((char *)pi);
                            if (rn != NULL)
                                free((char *)rn);
                            if (api != NULL)
                                free((char *)api);
                            if (et != NULL)
                                free((char *)et);
                            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                            {
                                free((char *)lbl[i]);
                            }
                            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                            {
                                free((char *)poa[i]);
                            }
                            for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                            {
                                free((char *)srv[i]);
                            }
                            return;
                        }
                    }

                    lbl[i] = strdup(label_str);
                }
            }
            else if (strcmp(key, "poa") == 0)
            {
                if (!json_object_is_type(val, json_type_array))
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value format must an array of strings.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"poa value format must an array of strings.\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > POAS_NUMBER)
                {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),"{\"error\":\"poa value must not surpass %d elements.\"}", POAS_NUMBER);
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n%s",error_msg);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                for (int i = 0; i < array_length; i++)
                {
                    json_object *poa_item = json_object_array_get_idx(val, i);
                    if (!json_object_is_type(poa_item, json_type_string))
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements must be strings.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"poa value elements must be strings.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }

                    const char *poa_str = json_object_get_string(poa_item);
                    if (poa_str == NULL || strlen(poa_str) == 0)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements cannot be empty.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"poa value elements cannot be empty.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }

                    // Check for duplicates
                    for (int j = 0; j < i; j++) {
                        if (strcmp(poa[j], poa_str) == 0) {
                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"poa value elements cannot be duplicated.\"}";
                                uint8_t buf[4]; //max size to encode any coap option value
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                            }
                            sqlite3_close(db);
                            json_object_put(root);
                            free((char *)ri);
                            free((char *)aei);
                            free((char *)pi);
                            if (rn != NULL)
                                free((char *)rn);
                            if (api != NULL)
                                free((char *)api);
                            if (et != NULL)
                                free((char *)et);
                            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                            {
                                free((char *)lbl[i]);
                            }
                            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                            {
                                free((char *)poa[i]);
                            }
                            for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                            {
                                free((char *)srv[i]);
                            }
                            return;
                        }
                    }

                    poa[i] = strdup(poa_str);
                }
            }
            else if (strcmp(key, "srv") == 0)
            {
                if (!json_object_is_type(val, json_type_array))
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"srv value format must an array of strings.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"srv value format must an array of strings.\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > SRVS_NUMBER)
                {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),"{\"error\":\"srv value must not surpass %d elements.\"}", SRVS_NUMBER);
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE,"HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n%s",error_msg);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    json_object_put(root);
                    free((char *)ri);
                    free((char *)aei);
                    free((char *)pi);
                    if (rn != NULL)
                        free((char *)rn);
                    if (api != NULL)
                        free((char *)api);
                    if (et != NULL)
                        free((char *)et);
                    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                    {
                        free((char *)lbl[i]);
                    }
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                    {
                        free((char *)srv[i]);
                    }
                    return;
                }

                for (int i = 0; i < array_length; i++)
                {
                    json_object *srv_item = json_object_array_get_idx(val, i);
                    if (!json_object_is_type(srv_item, json_type_string))
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements must be strings.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"poa value elements must be strings.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value //MAX size to encode any CoAP option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }

                    const char *srv_str = json_object_get_string(srv_item);
                    if (srv_str == NULL || strlen(srv_str) == 0)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"srv value elements cannot be empty.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"srv value elements cannot be empty.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                        }
                        sqlite3_close(db);
                        json_object_put(root);
                        free((char *)ri);
                        free((char *)aei);
                        free((char *)pi);
                        if (rn != NULL)
                            free((char *)rn);
                        if (api != NULL)
                            free((char *)api);
                        if (et != NULL)
                            free((char *)et);
                        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                        {
                            free((char *)lbl[i]);
                        }
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                        {
                            free((char *)srv[i]);
                        }
                        return;
                    }

                    // Check for duplicates
                    for (int j = 0; j < i; j++) {
                        if (strcmp(srv[j], srv_str) == 0) {
                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"srv value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"srv value elements cannot be duplicated.\"}";
                                uint8_t buf[4]; //max size to encode any coap option value
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                            }
                            sqlite3_close(db);
                            json_object_put(root);
                            free((char *)ri);
                            free((char *)aei);
                            free((char *)pi);
                            if (rn != NULL)
                                free((char *)rn);
                            if (api != NULL)
                                free((char *)api);
                            if (et != NULL)
                                free((char *)et);
                            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                            {
                                free((char *)lbl[i]);
                            }
                            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                            {
                                free((char *)poa[i]);
                            }
                            for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                            {
                                free((char *)srv[i]);
                            }
                            return;
                        }
                    }

                    srv[i] = strdup(srv_str);
                }
            }
        }
    }
    else
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"'m2m:ae' object not found in JSON.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"'m2m:ae' object not found in JSON.\"}";
            uint8_t buf[4]; //max size to encode any coap option value
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ri);
        free((char *)aei);
        free((char *)pi);
        return;
    }

    // Handle the case where "rn" does not exist
    if (rn == NULL)
    {
        do
        {
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
                free((char *)ri);
                free((char *)aei);
                free((char *)pi);
                if (rn != NULL)
                    free((char *)rn);
                if (api != NULL)
                    free((char *)api);
                if (et != NULL)
                    free((char *)et);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
                for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                {
                    free((char *)poa[i]);
                }
                for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                {
                    free((char *)srv[i]);
                }
                return;
            }

            sqlite3_stmt *stmt;
            const char *sql =
                "SELECT * "
                "FROM resources "
                "WHERE rn = ? "
                "AND pi IN ("
                    "SELECT r.ri "
                    "FROM resources r "
                    "JOIN cse_bases cb ON r.ri = cb.ri "
                    "WHERE r.rn = ?"
                ")";

            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

            sqlite3_bind_text(stmt, 1, rn, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, csebase_name, -1, SQLITE_STATIC);

            // Execute the statement
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } while (rc == SQLITE_ROW);
    }

    // Handle the case where "api" does not exist
    if (api == NULL)
    {
        do
        {
            // If no api is provided, generate a random sequence and check if that random sequence api already exists.
            api = generate_random_sequence_N();
            if (api == NULL)
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
                json_object_put(root);
                free((char *)ri);
                free((char *)aei);
                free((char *)pi);
                if (rn != NULL)
                    free((char *)rn);
                if (api != NULL)
                    free((char *)api);
                if (et != NULL)
                    free((char *)et);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
                for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                {
                    free((char *)poa[i]);
                }
                for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
                {
                    free((char *)srv[i]);
                }
                return;
            }

            sqlite3_stmt *stmt;
            const char *sql =
                "SELECT * "
                "FROM application_entities "
                "WHERE api = ? ";

            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

            sqlite3_bind_text(stmt, 1, api, -1, SQLITE_STATIC);

            // Execute the statement
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } while (rc == SQLITE_ROW);
    }

    if (rr == -1)
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rr is required.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"rr is required.\"}";
            uint8_t buf[4]; //max size to encode any coap option value
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ri);
        free((char *)aei);
        free((char *)pi);
        if (rn != NULL)
            free((char *)rn);
        if (api != NULL)
            free((char *)api);
        if (et != NULL)
            free((char *)et);
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            free((char *)lbl[i]);
        }
        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
        {
            free((char *)poa[i]);
        }
        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
        {
            free((char *)srv[i]);
        }
        return;
    }

    printf("pi:  %s\n", pi);
    printf("ri:  %s\n", ri);
    printf("aei: %s\n", aei);
    printf("ty:  %d\n", ty);
    printf("rn:  %s\n", rn);
    printf("api: %s\n", api);
    printf("rr:  %d\n", rr);
    printf("et:  %s\n", et);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        printf("lbl[%d]: %s\n", i, lbl[i]);
    }
    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
    {
        printf("poa[%d]: %s\n", i, poa[i]);
    }
    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
    {
        printf("srv[%d]: %s\n", i, srv[i]);
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
        free((char *)ri);
        free((char *)aei);
        free((char *)pi);
        free((char *)rn);
        free((char *)api);
        if (et != NULL)
            free((char *)et);
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            free((char *)lbl[i]);
        }
        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
        {
            free((char *)poa[i]);
        }
        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
        {
            free((char *)srv[i]);
        }
        return;
    }

    // Write to application_entities table
    // SQL statement for application_entities table
    const char *sql_template_ae;

    if (et != NULL)
    {
        sql_template_ae = "INSERT INTO application_entities (ri, api, aei, rr, et) "
                          "VALUES (?, ?, ?, ?, ?)";
    }
    else
    {
        sql_template_ae = "INSERT INTO application_entities (ri, api, aei, rr) "
                          "VALUES (?, ?, ?, ?)";
    }

    // Prepare SQL statement
    sqlite3_stmt *stmt_ae;
    sqlite3_prepare_v2(db, sql_template_ae, -1, &stmt_ae, NULL);

    sqlite3_bind_text(stmt_ae, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_ae, 2, api, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_ae, 3, aei, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt_ae, 4, rr);
    if (et != NULL)
    {
        sqlite3_bind_text(stmt_ae, 5, et, -1, SQLITE_STATIC);
    }

    int rc_ae = sqlite3_step(stmt_ae);

    // Finalize statement
    sqlite3_finalize(stmt_ae);

    if (rc_ae != SQLITE_DONE)
    {
        fprintf(stderr, "Failed to insert into application_entities table: %s\n", sqlite3_errmsg(db));
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
        free((char *)ri);
        free((char *)aei);
        free((char *)pi);
        free((char *)rn);
        free((char *)api);
        if (et != NULL)
            free((char *)et);
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            free((char *)lbl[i]);
        }
        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
        {
            free((char *)poa[i]);
        }
        for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
        {
            free((char *)srv[i]);
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
            free((char *)ri);
            free((char *)aei);
            free((char *)pi);
            free((char *)rn);
            free((char *)api);
            if (et != NULL)
                free((char *)et);
            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
            {
                free((char *)lbl[i]);
            }
            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
            {
                free((char *)poa[i]);
            }
            for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
            {
                free((char *)srv[i]);
            }
            return;
        }
    }

    // Write to points_of_access table
    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
    {
        // SQL statement for points_of_access table
        const char *sql_template_poa = "INSERT INTO points_of_access (ri, poa) VALUES (?, ?)";

        // Prepare SQL statement
        sqlite3_stmt *stmt_poa;
        sqlite3_prepare_v2(db, sql_template_poa, -1, &stmt_poa, NULL);

        sqlite3_bind_text(stmt_poa, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_poa, 2, poa[i], -1, SQLITE_STATIC);

        int rc_poa = sqlite3_step(stmt_poa);

        // Finalize statement
        sqlite3_finalize(stmt_poa);

        if (rc_poa != SQLITE_DONE)
        {
            fprintf(stderr, "Failed to insert into points_of_access table: %s\n", sqlite3_errmsg(db));
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            json_object_put(root);
            free((char *)ri);
            free((char *)aei);
            free((char *)pi);
            free((char *)rn);
            free((char *)api);
            if (et != NULL)
                free((char *)et);
            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
            {
                free((char *)lbl[i]);
            }
            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
            {
                free((char *)poa[i]);
            }
            for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
            {
                free((char *)srv[i]);
            }
            return;
        }
    }

    // Write to supported_release_versions table
    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
    {
        // SQL statement for supported_release_versions table
        const char *sql_template_srv = "INSERT INTO supported_release_versions (ri, srv) VALUES (?, ?)";

        // Prepare SQL statement
        sqlite3_stmt *stmt_srv;
        sqlite3_prepare_v2(db, sql_template_srv, -1, &stmt_srv, NULL);

        sqlite3_bind_text(stmt_srv, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_srv, 2, srv[i], -1, SQLITE_STATIC);

        int rc_srv = sqlite3_step(stmt_srv);

        // Finalize statement
        sqlite3_finalize(stmt_srv);

        if (rc_srv != SQLITE_DONE)
        {
            fprintf(stderr, "Failed to insert into supported_release_versions table: %s\n", sqlite3_errmsg(db));
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            json_object_put(root);
            free((char *)ri);
            free((char *)aei);
            free((char *)pi);
            free((char *)rn);
            free((char *)api);
            if (et != NULL)
                free((char *)et);
            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
            {
                free((char *)lbl[i]);
            }
            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
            {
                free((char *)poa[i]);
            }
            for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
            {
                free((char *)srv[i]);
            }
            return;
        }
    }

    // Commit transaction
    sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);

    // Close the SQLite database
    sqlite3_close(db);

    printf("Transaction completed successfully.\n");

    // Free the JSON object
    json_object_put(root);

    // Free allocated memory
    free((char *)ri);
    free((char *)aei);
    free((char *)pi);
    free((char *)api);
    if (et != NULL)
        free((char *)et);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        free((char *)lbl[i]);
    }
    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
    {
        free((char *)poa[i]);
    }
    for (int i = 0; i < SRVS_NUMBER && srv[i] != NULL; i++)
    {
        free((char *)srv[i]);
    }

    // Allocate memory for the destination string
    char *rn_copy = NULL;
    rn_copy = strdup(rn); // Use strdup to duplicate strings
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
    char *jsonBody = handle_request_ae_get(params, csebase_name, rn_copy, "POST");
    if (jsonBody != NULL)
    {
        free(jsonBody);
    }

    // Free the allocated memory for rn_copy
    free(rn_copy);
}

void handle_request_ae_put(struct response_params *params, char *csebase_name, char *ae_name, char *request, char *body)
{
    char response[BUFFER_SIZE] = {0};
    char *err_msg = 0;
    sqlite3 *db;
    int rc;

    // Variables to store values
    int rr = -1; // Initialize to an invalid value
    const char *ri = NULL;
    const char *et = NULL;
    const char *lbl[LABELS_NUMBER] = {NULL};
    const char *poa[POAS_NUMBER] = {NULL};
    int has_et = 0;
    int has_lbl = 0;
    int has_poa = 0;

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
        // Parsing failed, print an error message
        printf("Failed to parse JSON. Invalid JSON string: %s\n", jsonBody);

        // Send a 500 Internal Server Error response to the client
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }

        // Free the allocated jsonBody memory
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
                ri = strdup(ri_str);
                printf("ri: %s\n", ri);
            }
            else
            {
                printf("Failed to get string for ri\n");
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
        free((char *)ri);
        return;
    }

    // PARSE X-M2M-ORIGIN HEADER (RI)
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

        size_t ri_length = origin_value_end - origin_value_start;
        char *temp_ri = (char *)malloc(ri_length + 1);
        if (temp_ri == NULL)
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
            free((char *)ri);
            return;
        }
        strncpy(temp_ri, origin_value_start, ri_length);
        temp_ri[ri_length] = '\0'; // Null-terminate the string

        // Check if header is equal to ri
        if (ri != NULL && strcmp(ri, temp_ri) != 0)
        {
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"X-M2M-Origin header value is not valid (%s).\"}", temp_ri);
                write(params->http_socket, response, strlen(response));
            }
            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
            {
                const char *error_msg = "{\"error\":\"X-M2M-Origin header value is not valid (%s).\"}", temp_ri;
                uint8_t buf[4]; //max size to encode any coap option value
                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
            }
            sqlite3_close(db);
            free((char *)ri);
            free(temp_ri);
            return;
        }

        free(temp_ri);
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
                if (ty_value != 2)
                {
                    // Error: Invalid ty value
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"ty value must be 2.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"ty value must be 2.\"}";
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
                    }
                    sqlite3_close(db);
                    free((char *)ri);
                    return;
                }
            }
        }
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
            uint8_t buf[4]; //max size to encode any coap option value
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        free((char *)ri);
        return;
    }

    // Retrieve values from JSON object
    json_object *ae_object_body;
    if (json_object_object_get_ex(root, "m2m:ae", &ae_object_body))
    {
        json_object_object_foreach(ae_object_body, key, val)
        {
            if (strcmp(key, "rr") == 0)
            {
                // First, check if the JSON value is valid and can be converted to an integer
                if (json_object_is_type(val, json_type_int))
                {
                    rr = json_object_get_int(val);

                    // Check if rr is either 0 or 1
                    if (rr != 0 && rr != 1)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rr value must be 0, 1, true, or false.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"rr value must be 0, 1, true, or false.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        return;
                    }
                }
                else if (json_object_is_type(val, json_type_boolean))
                {
                    rr = json_object_get_boolean(val); // This returns 1 for true and 0 for false
                }
                else
                {
                    // If the value is neither an integer nor a boolean, it's an invalid type
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"rr value must be 0, 1, true, or false.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"rr value must be 0, 1, true, or false.\"}";
                                uint8_t buf[4]; //max size to encode any coap option value
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    return;
                }
            }
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
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
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
                            const char *error_msg = "{\"error\":\"et value is not a valid date.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
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
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        return;
                    }
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
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > 5)
                {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),"{\"error\":\"lbl value must not surpass %d elements.\"}", LABELS_NUMBER);
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n%s",error_msg);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
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
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
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
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        return;
                    }

                    // Check for duplicates
                    for (int j = 0; j < i; j++) {
                        if (strcmp(lbl[j], lbl_str) == 0) {
                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"lbl value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"lbl value elements cannot be duplicated.\"}";
                                uint8_t buf[4]; //max size to encode any coap option value
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                            {
                                free((char *)poa[i]);
                            }
                            return;
                        }
                    }

                    lbl[i] = strdup(lbl_str);
                }
            }
            else if (strcmp(key, "poa") == 0)
            {
                has_poa = 1;

                if (!json_object_is_type(val, json_type_array))
                {
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value format must be an array of strings.\"}");
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        const char *error_msg = "{\"error\":\"poa value format must be an array of strings.\"}";
                        write(params->http_socket, response, strlen(response));
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
                    }
                    return;
                }

                int array_length = json_object_array_length(val);
                if (array_length > 5)
                {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),"{\"error\":\"poa value must not surpass %d elements.\"}", POAS_NUMBER);
                    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                    {
                        snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n%s",error_msg);
                        write(params->http_socket, response, strlen(response));
                    }
                    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                    {
                        uint8_t buf[4]; //max size to encode any coap option value
                        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                        coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                        coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                    {
                        free((char *)poa[i]);
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
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements must be strings.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"poa value elements must be strings.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        return;
                    }

                    const char *poa_str = json_object_get_string(label);
                    if (poa_str == NULL || strlen(poa_str) == 0)
                    {
                        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                        {
                            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements cannot be empty.\"}");
                            write(params->http_socket, response, strlen(response));
                        }
                        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                        {
                            const char *error_msg = "{\"error\":\"poa value elements cannot be empty.\"}";
                            uint8_t buf[4]; //max size to encode any coap option value
                            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                        {
                            free((char *)poa[i]);
                        }
                        return;
                    }

                    // Check for duplicates
                    for (int j = 0; j < i; j++) {
                        if (strcmp(poa[j], poa_str) == 0) {
                            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
                            {
                                snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"poa value elements cannot be duplicated.\"}");
                                write(params->http_socket, response, strlen(response));
                            }
                            else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
                            {
                                const char *error_msg = "{\"error\":\"poa value elements cannot be duplicated.\"}";
                                uint8_t buf[4]; //max size to encode any coap option value
                                coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
                                coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
                                coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
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
                            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                            {
                                free((char *)poa[i]);
                            }
                            return;
                        }
                    }

                    poa[i] = strdup(poa_str);
                }
            }
        }
    }
    else
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"'m2m:ae' object not found in JSON.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"'m2m:ae' object not found in JSON.\"}";
            uint8_t buf[4]; //max size to encode any coap option value
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
            coap_add_data(params->coap_response, strlen(error_msg), (const uint8_t *)error_msg);
        }
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ri);
        return;
    }

    printf("ri:  %s\n", ri);
    printf("rr:  %d\n", rr);
    printf("et:  %s\n", et);
    for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
    {
        printf("lbl[i]: %s\n", lbl[i]);
    }
    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
    {
        printf("poa[i]: %s\n", poa[i]);
    }

    if (et == NULL && rr == -1 && has_lbl == 0 && has_poa == 0)
    {
        // No update needed if nothing was provided
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 422 Unprocessable Entity\r\nContent-Type: application/json\r\n\r\n{\"error\":\"nothing to update.\"}");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            const char *error_msg = "{\"error\":\"nothing to update.\"}";
            uint8_t buf[4]; //max size to encode any coap option value
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_UNPROCESSABLE);
            coap_add_option(params->coap_response,COAP_OPTION_CONTENT_FORMAT,coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),buf);
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
        fprintf(stderr, "Failed to update resources table: %s\n", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        json_object_put(root);
        free((char *)ri);
        if (et != NULL)
            free((char *)et);
        for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
        {
            free((char *)lbl[i]);
        }
        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
        {
            free((char *)poa[i]);
        }
        return;
    }

    if (has_et == 1 || rr != -1)
    {
        // SQL statement for application_entities table
        char sql_template_ae[BUFFER_SIZE];
        snprintf(sql_template_ae, sizeof(sql_template_ae), "UPDATE application_entities SET");

        int first_param = 1;

        // Append fields to be updated based on the provided values
        if (has_et == 1) {
            strncat(sql_template_ae, " et = ?", sizeof(sql_template_ae) - strlen(sql_template_ae) - 1);
            first_param = 0;
        }
        if (rr != -1) {
            if (!first_param) {
                strncat(sql_template_ae, ",", sizeof(sql_template_ae) - strlen(sql_template_ae) - 1);
            }
            strncat(sql_template_ae, " rr = ?", sizeof(sql_template_ae) - strlen(sql_template_ae) - 1);
            first_param = 0;
        }

        // Add the WHERE clause
        strncat(sql_template_ae, " WHERE ri = ?", sizeof(sql_template_ae) - strlen(sql_template_ae) - 1);

        // Prepare SQL statement
        sqlite3_stmt *stmt_ae;
        sqlite3_prepare_v2(db, sql_template_ae, -1, &stmt_ae, NULL);

        // Bind parameters based on the provided values
        int param_index = 1;
        if (has_et == 1) {
            sqlite3_bind_text(stmt_ae, param_index++, et, -1, SQLITE_STATIC);
        }
        if (rr != -1) {
            sqlite3_bind_int(stmt_ae, param_index++, rr);
        }
        sqlite3_bind_text(stmt_ae, param_index++, ri, -1, SQLITE_STATIC);

        int rc_ae = sqlite3_step(stmt_ae);

        sqlite3_finalize(stmt_ae);

        if (rc_ae != SQLITE_DONE)
        {
            fprintf(stderr, "Failed to update application_entities table: %s\n", sqlite3_errmsg(db));
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
            free((char *)ri);
            if (et != NULL)
                free((char *)et);
            for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
            {
                free((char *)lbl[i]);
            }
            for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
            {
                free((char *)poa[i]);
            }
            return;
        }
    }

    if (has_lbl == 1)
    {
        // DELETE statement for labels table
        const char *sql_lbl = "DELETE FROM labels WHERE ri = ?";
        sqlite3_stmt *stmt_lbl;

        sqlite3_prepare_v2(db, sql_lbl, -1, &stmt_lbl, NULL);

        sqlite3_bind_text(stmt_lbl, 1, ri, -1, SQLITE_STATIC);

        sqlite3_step(stmt_lbl);

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
                fprintf(stderr, "Failed to insert into labels table: %s\n", sqlite3_errmsg(db));
                sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
                sqlite3_close(db);
                json_object_put(root);
                free((char *)ri);
                if (et != NULL)
                    free((char *)et);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
                for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                {
                    free((char *)poa[i]);
                }
                return;
            }
        }
    }

    if (has_poa == 1)
    {
        // DELETE statement for points_of_access table
        const char *sql_poa = "DELETE FROM points_of_access WHERE ri = ?";
        sqlite3_stmt *stmt_poa;
        sqlite3_prepare_v2(db, sql_poa, -1, &stmt_poa, NULL);

        sqlite3_bind_text(stmt_poa, 1, ri, -1, SQLITE_STATIC);

        sqlite3_step(stmt_poa);

        sqlite3_finalize(stmt_poa);

        // Write to points_of_access table
        for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
        {
            // SQL statement for points_of_access table
            const char *sql_template_poa = "INSERT INTO points_of_access (ri, poa) VALUES (?, ?)";

            // Prepare SQL statement
            sqlite3_stmt *stmt_poa;
            sqlite3_prepare_v2(db, sql_template_poa, -1, &stmt_poa, NULL);

            sqlite3_bind_text(stmt_poa, 1, ri, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt_poa, 2, poa[i], -1, SQLITE_STATIC);

            int rc_poa = sqlite3_step(stmt_poa);

            sqlite3_finalize(stmt_poa);

            if (rc_poa != SQLITE_DONE)
            {
                fprintf(stderr, "Failed to insert into points_of_access table: %s\n", sqlite3_errmsg(db));
                sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
                sqlite3_close(db);
                json_object_put(root);
                free((char *)ri);
                if (et != NULL)
                    free((char *)et);
                for (int i = 0; i < LABELS_NUMBER && lbl[i] != NULL; i++)
                {
                    free((char *)lbl[i]);
                }
                for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
                {
                    free((char *)poa[i]);
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
    for (int i = 0; i < POAS_NUMBER && poa[i] != NULL; i++)
    {
        free((char *)poa[i]);
    }

    printf("GET /%s/%s/\n", csebase_name, ae_name);

    // RETURN THE ROW INSERTED BACK TO THE CLIENT WITH ALL THE DATA
    jsonBody = handle_request_ae_get(params, csebase_name, ae_name, "PUT");
    if (jsonBody != NULL)
    {
        free(jsonBody);
    }
}

void handle_request_ae_delete(struct response_params *params, char *csebase_name, char *ae_name, char *request_type, sqlite3 *db)
{
    char response[BUFFER_SIZE] = {0};
    int rc;
    char *jsonBody;

    if (strcmp(request_type, "DELETE") == 0)
    {
        // CHECK IF ae_name EXISTS
        jsonBody = handle_request_ae_get(params, csebase_name, ae_name, "INTERNAL");

        if (strlen(jsonBody) == 0)
        {
            // Handle absence of resource ID
            if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n");
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
        int rc = sqlite3_open(DB_PATH, &db);

        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
            free(jsonBody);
            return;
        }

        // Begin transaction
        sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);
    }

    // Delete associated containers
    const char *sql_query = 
        "SELECT r.rn "
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
        ")";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);

    sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, ae_name, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // Fetch container_name (rn) from the result set
        char *container_name = (char *)sqlite3_column_text(stmt, 0);

        // Call handle_request_container_delete function for each container
        handle_request_container_delete(params, csebase_name, ae_name, container_name, "INTERNAL", db);
    }
    sqlite3_finalize(stmt);

    // DELETE statement for labels table
    const char *sql_lbl =
        "DELETE FROM labels "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "JOIN application_entities ae ON r.ri = ae.ri "
            "WHERE r.pi IN ( "
                "SELECT r2.ri "
                "FROM resources r2 "
                "JOIN cse_bases cb ON r2.ri = cb.ri "
                "WHERE r2.rn = ? "
            ") "
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_lbl;

    sqlite3_prepare_v2(db, sql_lbl, -1, &stmt_lbl, NULL);

    sqlite3_bind_text(stmt_lbl, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_lbl, 2, ae_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_lbl);

    sqlite3_finalize(stmt_lbl);

    // DELETE statement for points_of_access table
    const char *sql_poa =
        "DELETE FROM points_of_access "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "JOIN application_entities ae ON r.ri = ae.ri "
            "WHERE r.pi IN ( "
                "SELECT r2.ri "
                "FROM resources r2 "
                "JOIN cse_bases cb ON r2.ri = cb.ri "
                "WHERE r2.rn = ? "
            ") "
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_poa;

    sqlite3_prepare_v2(db, sql_poa, -1, &stmt_poa, NULL);

    sqlite3_bind_text(stmt_poa, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_poa, 2, ae_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_poa);

    sqlite3_finalize(stmt_poa);

    // DELETE statement for supported_release_versions table
    const char *sql_srv =
        "DELETE FROM supported_release_versions "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "JOIN application_entities ae ON r.ri = ae.ri "
            "WHERE r.pi IN ( "
                "SELECT r2.ri "
                "FROM resources r2 "
                "JOIN cse_bases cb ON r2.ri = cb.ri "
                "WHERE r2.rn = ? "
            ") "
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_srv;

    sqlite3_prepare_v2(db, sql_srv, -1, &stmt_srv, NULL);

    sqlite3_bind_text(stmt_srv, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_srv, 2, ae_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_srv);

    sqlite3_finalize(stmt_srv);

    // DELETE statement for application_entities table
    const char *sql_ae =
        "DELETE FROM application_entities "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "WHERE r.pi IN ( "
                "SELECT cb.ri "
                "FROM cse_bases cb "
                "JOIN resources r2 ON cb.ri = r2.ri "
                "WHERE r2.rn = ? "
            ") "
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_ae;

    sqlite3_prepare_v2(db, sql_ae, -1, &stmt_ae, NULL);

    sqlite3_bind_text(stmt_ae, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_ae, 2, ae_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_ae);

    sqlite3_finalize(stmt_ae);
    
    // DELETE statement for resources table
    const char *sql_rs =
        "DELETE FROM resources "
        "WHERE ri IN ( "
            "SELECT r.ri "
            "FROM resources r "
            "WHERE r.pi IN ( "
                "SELECT cb.ri "
                "FROM cse_bases cb "
                "JOIN resources r2 ON cb.ri = r2.ri "
                "WHERE r2.rn = ? "
            ") "
            "AND r.rn = ? "
        ")";

    sqlite3_stmt *stmt_rs;

    sqlite3_prepare_v2(db, sql_rs, -1, &stmt_rs, NULL);

    sqlite3_bind_text(stmt_rs, 1, csebase_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_rs, 2, ae_name, -1, SQLITE_STATIC);

    sqlite3_step(stmt_rs);

    sqlite3_finalize(stmt_rs);

    if (strcmp(request_type, "DELETE") == 0)
    {
        // Commit transaction
        sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, 0);

        // Close the SQLite database
        sqlite3_close(db);

        // Send the response
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
