/*
AUTHORSHIP -------------------------------------------------

 * File:    cseBase.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions for csrBase.

*/

#include "../include/cseBase.h"


void handle_request_csebase_create()
{
    char *err_msg = 0;
    sqlite3 *db;

    char csi[BUFFER_SIZE + 2];
    int cst;
    char ri[BUFFER_SIZE + 2];
    char rn[BUFFER_SIZE];
    char *lbl[LABELS_NUMBER];
    char *poa[POAS_NUMBER];
    int lbl_count = 0;
    int poa_count = 0;

    // Open the SQLite database file
    int rc = sqlite3_open(DB_PATH, &db);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    // Verificar se existe uma CSEBase criada anteriormente
 /*   if (csebase_exists(db))
    {
        char choice;

        do
        {
            printf("Pretende substituir a CSEBase existente? (s/n): ");
            choice = getchar();
            while (getchar() != '\n')
                ;

            choice = tolower(choice);

            if (choice == 's')
            {
                break;
            }
            else if (choice == 'n')
            {
                // Close the SQLite database
                sqlite3_close(db);
                return;
            }
            else
                printf("Valor inserido inválido.\n");
        } while (choice != 's' && choice != 'n');
    }

    // Set default values
    cst = DEFAULT_CST;

    // Parse default lbl and poa
    parse_input(DEFAULT_LBL, lbl, &lbl_count);
    parse_input(DEFAULT_POA, poa, &poa_count);

    // Prompt user for resourceName
    char input[BUFFER_SIZE];
    do
    {
        printf("Insira o resourceName (default: %s): ", DEFAULT_RN);

        if (fgets(input, sizeof(input), stdin) != NULL)
        {
            if (input[0] != '\n')
            {
                input[strcspn(input, "\n")] = 0;
                if (is_valid_string_plus_three_extra_chars(input))
                {
                    strcpy(rn, input);
                    break;
                }
                else
                {
                    printf("Valor inserido inválido. Tente novamente.\n");
                }
            }
            else
            {
                strcpy(rn, DEFAULT_RN);
                break;
            }
        }
    } while (1);

    // Prompt user for labels
    printf("Insira a lista de Labels separadas por ',' (default: %s): ", DEFAULT_LBL);
    if (fgets(input, sizeof(input), stdin) != NULL)
    {
        if (input[0] != '\n')
        {
            input[strcspn(input, "\n")] = 0;
            lbl_count = 0; // Reset lbl count
            parse_input(input, lbl, &lbl_count);
        }
    }

    // Prompt user for pointOfAccess
    printf("Insira a lista de Points of Access separados por ',' (default: %s): ", DEFAULT_POA);
    if (fgets(input, sizeof(input), stdin) != NULL)
    {
        if (input[0] != '\n')
        {
            input[strcspn(input, "\n")] = 0;
            poa_count = 0; // Reset poa count
            parse_input(input, poa, &poa_count);
        }
    }

    // Automatically set csi and ri based on rn
    snprintf(csi, sizeof(csi), "\\%s", rn);
    snprintf(ri, sizeof(ri), "%sID", rn);
*/
    // TEMP: usar valores por defeito para correr Valgrind (interacao removida temporariamente)
    if (csebase_exists(db))
    {
        sqlite3_close(db);
        return;
    }

    // Set default values
    cst = DEFAULT_CST;
    parse_input(DEFAULT_LBL, lbl, &lbl_count);
    parse_input(DEFAULT_POA, poa, &poa_count);
    strcpy(rn, DEFAULT_RN);

    // Automatically set csi and ri based on rn
    snprintf(csi, sizeof(csi), "\\%s", rn);
    snprintf(ri, sizeof(ri), "%sID", rn);

    // Output the CSE_Base values
    printf("---------------------\nCSE_Base resource values:\n");
    printf("CSE-ID: %s\n", csi);
    printf("cseType: %d\n", cst);
    printf("resourceID: %s\n", ri);
    printf("resourceName: %s\n", rn);
    printf("labels: ");
    for (int i = 0; i < lbl_count; i++)
    {
        printf("%s", lbl[i]);
        if (i < lbl_count - 1)
        {
            printf(", ");
        }
    }
    printf("\n");
    printf("pointOfAccess: ");
    for (int i = 0; i < poa_count; i++)
    {
        printf("%s", poa[i]);
        if (i < poa_count - 1)
        {
            printf(", ");
        }
    }
    printf("\n---------------------\n");

    // Begin transaction
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, &err_msg);

    delete_csebases(db);

    // SQL statement for inserting a new row into the resources table
    sqlite3_stmt *stmt_sql_resources;
    const char *sql_resources = "INSERT INTO resources (ty, ri, rn, pi, ct, lt) "
                                "VALUES (5, ?, ?, '', datetime('now', 'localtime'), datetime('now', 'localtime'))";

    sqlite3_prepare_v2(db, sql_resources, -1, &stmt_sql_resources, NULL);

    sqlite3_bind_text(stmt_sql_resources, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_sql_resources, 2, rn, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt_sql_resources);
    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "SQL error for resources: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_sql_resources);
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        for (int i = 0; i < lbl_count; i++)
            free(lbl[i]);
        for (int i = 0; i < poa_count; i++)
            free(poa[i]);
        return;
    }
    else
    {
        printf("resources: Row inserted successfully.\n");
    }

    sqlite3_finalize(stmt_sql_resources);

    // SQL statement for inserting a new row into the cse_bases table
    sqlite3_stmt *stmt_sql_cse_bases;
    const char *sql_cse_bases = "INSERT INTO cse_bases (ri, csi, cst) VALUES (?, ?, ?)";

    sqlite3_prepare_v2(db, sql_cse_bases, -1, &stmt_sql_cse_bases, NULL);

    sqlite3_bind_text(stmt_sql_cse_bases, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_sql_cse_bases, 2, csi, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt_sql_cse_bases, 3, cst);

    rc = sqlite3_step(stmt_sql_cse_bases);
    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "SQL error for cse_bases: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_sql_cse_bases);
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        for (int i = 0; i < lbl_count; i++)
            free(lbl[i]);
        for (int i = 0; i < poa_count; i++)
            free(poa[i]);
        return;
    }
    else
    {
        printf("cse_bases: Row inserted successfully.\n");
    }

    sqlite3_finalize(stmt_sql_cse_bases);

    // Insert into access_control_policy_ids table
    sqlite3_stmt *stmt_sql_access_control_policy_ids;
    const char *sql_access_control_policy_ids = "INSERT INTO access_control_policy_ids (ri, acpi) VALUES (?, ?)";
    const char *acpi = "mn-nameIDAcp";

    sqlite3_prepare_v2(db, sql_access_control_policy_ids, -1, &stmt_sql_access_control_policy_ids, NULL);

    sqlite3_bind_text(stmt_sql_access_control_policy_ids, 1, ri, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_sql_access_control_policy_ids, 2, acpi, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt_sql_access_control_policy_ids);
    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "SQL error for access_control_policy_ids: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt_sql_access_control_policy_ids);
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
        sqlite3_close(db);
        for (int i = 0; i < lbl_count; i++)
            free(lbl[i]);
        for (int i = 0; i < poa_count; i++)
            free(poa[i]);
        return;
    }
    else
    {
        printf("access_control_policy_ids: Row inserted successfully.\n");
    }

    sqlite3_finalize(stmt_sql_access_control_policy_ids);

    // Insert into content_serializations table
    sqlite3_stmt *stmt_sql_content_serializations;
    const char *sql_content_serializations = "INSERT INTO content_serializations (ri, csz) VALUES (?, ?)";

    const char *csz_values[] = {
        "application/json"};
    size_t csz_count = sizeof(csz_values) / sizeof(csz_values[0]);

    for (size_t i = 0; i < csz_count; ++i)
    {
        sqlite3_prepare_v2(db, sql_content_serializations, -1, &stmt_sql_content_serializations, NULL);

        sqlite3_bind_text(stmt_sql_content_serializations, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_sql_content_serializations, 2, csz_values[i], -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt_sql_content_serializations);
        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "SQL error for content_serializations: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt_sql_content_serializations);
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            for (int i = 0; i < lbl_count; i++)
                free(lbl[i]);
            for (int i = 0; i < poa_count; i++)
                free(poa[i]);
            return;
        }
        else
        {
            printf("content_serializations: Row inserted successfully for csz = %s.\n", csz_values[i]);
        }

        sqlite3_finalize(stmt_sql_content_serializations);
    }

    // Insert into labels table
    sqlite3_stmt *stmt_sql_labels;
    const char *sql_labels = "INSERT INTO labels (ri, lbl) VALUES (?, ?)";

    for (size_t i = 0; i < lbl_count; ++i)
    {
        sqlite3_prepare_v2(db, sql_labels, -1, &stmt_sql_labels, NULL);

        sqlite3_bind_text(stmt_sql_labels, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_sql_labels, 2, lbl[i], -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt_sql_labels);
        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "SQL error for labels: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt_sql_labels);
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            for (int i = 0; i < lbl_count; i++)
                free(lbl[i]);
            for (int i = 0; i < poa_count; i++)
                free(poa[i]);
            return;
        }
        else
        {
            printf("labels: Row inserted successfully for lbl = %s.\n", lbl[i]);
        }

        sqlite3_finalize(stmt_sql_labels);
    }

    // Insert into points_of_access table
    sqlite3_stmt *stmt_sql_points_of_access;
    const char *sql_points_of_access = "INSERT INTO points_of_access (ri, poa) VALUES (?, ?)";

    for (size_t i = 0; i < poa_count; ++i)
    {
        sqlite3_prepare_v2(db, sql_points_of_access, -1, &stmt_sql_points_of_access, NULL);

        sqlite3_bind_text(stmt_sql_points_of_access, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_sql_points_of_access, 2, poa[i], -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt_sql_points_of_access);
        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "SQL error for points_of_access: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt_sql_points_of_access);
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            for (int i = 0; i < lbl_count; i++)
                free(lbl[i]);
            for (int i = 0; i < poa_count; i++)
                free(poa[i]);
            return;
        }
        else
        {
            printf("points_of_access: Row inserted successfully for poa = %s.\n", poa[i]);
        }

        sqlite3_finalize(stmt_sql_points_of_access);
    }

    // Insert into supported_resource_types table
    sqlite3_stmt *stmt_sql_supported_resource_types;
    const char *sql_supported_resource_types = "INSERT INTO supported_resource_types (ri, srt) VALUES (?, ?)";

    int srt_values[] = {2, 3, 4, 5};
    size_t srt_count = sizeof(srt_values) / sizeof(srt_values[0]);

    for (size_t i = 0; i < srt_count; ++i)
    {
        sqlite3_prepare_v2(db, sql_supported_resource_types, -1, &stmt_sql_supported_resource_types, NULL);

        sqlite3_bind_text(stmt_sql_supported_resource_types, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt_sql_supported_resource_types, 2, srt_values[i]);

        rc = sqlite3_step(stmt_sql_supported_resource_types);
        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "SQL error for supported_resource_types: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt_sql_supported_resource_types);
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            for (int i = 0; i < lbl_count; i++)
                free(lbl[i]);
            for (int i = 0; i < poa_count; i++)
                free(poa[i]);
            return;
        }
        else
        {
            printf("supported_resource_types: Row inserted successfully for srt = %d.\n", srt_values[i]);
        }

        sqlite3_finalize(stmt_sql_supported_resource_types);
    }

    // Insert into supported_release_versions table
    sqlite3_stmt *stmt_sql_supported_release_versions;
    const char *sql_supported_release_versions = "INSERT INTO supported_release_versions (ri, srv) VALUES (?, ?)";

    const char *srv_values[] = {"1", "2", "3"};
    size_t srv_count = sizeof(srv_values) / sizeof(srv_values[0]);

    for (size_t i = 0; i < srv_count; ++i)
    {
        sqlite3_prepare_v2(db, sql_supported_release_versions, -1, &stmt_sql_supported_release_versions, NULL);

        sqlite3_bind_text(stmt_sql_supported_release_versions, 1, ri, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt_sql_supported_release_versions, 2, srv_values[i], -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt_sql_supported_release_versions);
        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "SQL error for supported_release_versions: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt_sql_supported_release_versions);
            sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
            sqlite3_close(db);
            for (int i = 0; i < lbl_count; i++)
                free(lbl[i]);
            for (int i = 0; i < poa_count; i++)
                free(poa[i]);
            return;
        }
        else
        {
            printf("supported_release_versions: Row inserted successfully for srv = %s.\n", srv_values[i]);
        }

        sqlite3_finalize(stmt_sql_supported_release_versions);
    }

    // Commit transaction if all inserts are successful
    rc = sqlite3_exec(db, "COMMIT TRANSACTION", 0, 0, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error (COMMIT TRANSACTION): %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK TRANSACTION", 0, 0, 0);
    }

    // Free allocated memory for labels and pointOfAccess
    for (int i = 0; i < lbl_count; i++)
        free(lbl[i]);
    for (int i = 0; i < poa_count; i++)
        free(poa[i]);

    // Free any remaining error message memory
    sqlite3_free(err_msg);

    // Close the SQLite database
    sqlite3_close(db);
}

char *handle_request_csebase_get(struct response_params *params, char *csebase_name, char *request_type)
{
    char response[BUFFER_SIZE] = {0};
    char *ri = NULL;
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

    sqlite3_stmt *stmt;
    const char *sql = "SELECT resources.ty, resources.ri, resources.rn, resources.pi, resources.ct, resources.lt, cse_bases.csi, cse_bases.cst "
                      "FROM cse_bases JOIN resources ON cse_bases.ri = resources.ri WHERE resources.rn = ?";

    // Prepare the SQL statement
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    // Bind the parameter value
    rc = sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);

    // Execute the statement
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW)
    {
        // Construct the JSON response
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            snprintf(response, BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{");
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_CONTENT);
        }
        
        strcat(response, "\"m2m:cb\": {");

        // Declare a flag to track whether 'ri' has been assigned
        int ri_assigned = 0;

        // Iterate through columns
        for (int i = 0; i < sqlite3_column_count(stmt); i++)
        {
            const char *column_name = sqlite3_column_name(stmt, i);
            const char *column_value = (const char *)sqlite3_column_text(stmt, i);
            strcat(response, "\"");
            strcat(response, column_name);
            strcat(response, "\": ");

            if (strcmp(column_name, "cst") == 0 || strcmp(column_name, "ty") == 0)
            {
                strcat(response, column_value);
            }
            else
            {
                strcat(response, "\"");
                strcat(response, column_value);
                strcat(response, "\"");
            }

            // Add comma if not the last column
            if (i < sqlite3_column_count(stmt) - 1)
            {
                strcat(response, ",");
            }

            // CHECK IF COLUMN NAME IS ri AND IF IT'S NOT ALREADY ASSIGNED
            if (strcmp(column_name, "ri") == 0 && !ri_assigned)
            {
                // Allocate memory for ri and copy the value
                ri = strdup(column_value);
                ri_assigned = 1; // Set the flag to indicate 'ri' has been assigned
            }
        }
        strcat(response, ",");

        // Retrieve acpi values from the table
        strcat(response, "\"acpi\": [");
        sqlite3_stmt *acpi_stmt;
        const char *acpi_sql = "SELECT acpi FROM access_control_policy_ids WHERE ri = ?";
        rc = sqlite3_prepare_v2(db, acpi_sql, -1, &acpi_stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(acpi_stmt, 1, ri, -1, SQLITE_STATIC);
            while (sqlite3_step(acpi_stmt) == SQLITE_ROW)
            {
                strcat(response, "\"");
                strcat(response, (const char *)sqlite3_column_text(acpi_stmt, 0));
                strcat(response, "\",");
            }
        }
        sqlite3_finalize(acpi_stmt);
        // Remove trailing comma if present
        if (strlen(response) > 0 && response[strlen(response) - 1] == ',')
        {
            response[strlen(response) - 1] = '\0';
        }
        strcat(response, "],");

        // Retrieve csz values from the table
        strcat(response, "\"csz\": [");
        sqlite3_stmt *csz_stmt;
        const char *csz_sql = "SELECT csz FROM content_serializations WHERE ri = ?";
        rc = sqlite3_prepare_v2(db, csz_sql, -1, &csz_stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(csz_stmt, 1, ri, -1, SQLITE_STATIC);
            while (sqlite3_step(csz_stmt) == SQLITE_ROW)
            {
                strcat(response, "\"");
                strcat(response, (const char *)sqlite3_column_text(csz_stmt, 0));
                strcat(response, "\",");
            }
        }
        sqlite3_finalize(csz_stmt);
        // Remove trailing comma if present
        if (strlen(response) > 0 && response[strlen(response) - 1] == ',')
        {
            response[strlen(response) - 1] = '\0';
        }
        strcat(response, "],");

        // Retrieve lbl values from the table
        strcat(response, "\"lbl\": [");
        sqlite3_stmt *lbl_stmt;
        const char *lbl_sql = "SELECT lbl FROM labels WHERE ri = ?";
        rc = sqlite3_prepare_v2(db, lbl_sql, -1, &lbl_stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(lbl_stmt, 1, ri, -1, SQLITE_STATIC);
            while (sqlite3_step(lbl_stmt) == SQLITE_ROW)
            {
                strcat(response, "\"");
                strcat(response, (const char *)sqlite3_column_text(lbl_stmt, 0));
                strcat(response, "\",");
            }
        }
        sqlite3_finalize(lbl_stmt);
        // Remove trailing comma if present
        if (strlen(response) > 0 && response[strlen(response) - 1] == ',')
        {
            response[strlen(response) - 1] = '\0';
        }
        strcat(response, "],");

        // Retrieve poa values from the table
        strcat(response, "\"poa\": [");
        sqlite3_stmt *poa_stmt;
        const char *poa_sql = "SELECT poa FROM points_of_access WHERE ri = ?";
        rc = sqlite3_prepare_v2(db, poa_sql, -1, &poa_stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(poa_stmt, 1, ri, -1, SQLITE_STATIC);
            while (sqlite3_step(poa_stmt) == SQLITE_ROW)
            {
                strcat(response, "\"");
                strcat(response, (const char *)sqlite3_column_text(poa_stmt, 0));
                strcat(response, "\",");
            }
        }
        sqlite3_finalize(poa_stmt);
        // Remove trailing comma if present
        if (strlen(response) > 0 && response[strlen(response) - 1] == ',')
        {
            response[strlen(response) - 1] = '\0';
        }
        strcat(response, "],");

        // Retrieve srt values from the table
        strcat(response, "\"srt\": [");
        sqlite3_stmt *srt_stmt;
        const char *srt_sql = "SELECT srt FROM supported_resource_types WHERE ri = ?";
        rc = sqlite3_prepare_v2(db, srt_sql, -1, &srt_stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(srt_stmt, 1, ri, -1, SQLITE_STATIC);
            while (sqlite3_step(srt_stmt) == SQLITE_ROW)
            {
                strcat(response, (const char *)sqlite3_column_text(srt_stmt, 0));
                strcat(response, ",");
            }
        }
        sqlite3_finalize(srt_stmt);
        // Remove trailing comma if present
        if (strlen(response) > 0 && response[strlen(response) - 1] == ',')
        {
            response[strlen(response) - 1] = '\0';
        }
        strcat(response, "],");

        // Retrieve srv values from the table
        strcat(response, "\"srv\": [");
        sqlite3_stmt *srv_stmt;
        const char *srv_sql = "SELECT srv FROM supported_release_versions WHERE ri = ?";
        rc = sqlite3_prepare_v2(db, srv_sql, -1, &srv_stmt, NULL);
        if (rc == SQLITE_OK)
        {
            sqlite3_bind_text(srv_stmt, 1, ri, -1, SQLITE_STATIC);
            while (sqlite3_step(srv_stmt) == SQLITE_ROW)
            {
                strcat(response, "\"");
                strcat(response, (const char *)sqlite3_column_text(srv_stmt, 0));
                strcat(response, "\",");
            }
        }
        sqlite3_finalize(srv_stmt);
        // Remove trailing comma if present
        if (strlen(response) > 0 && response[strlen(response) - 1] == ',')
        {
            response[strlen(response) - 1] = '\0';
        }
        strcat(response, "]");

        strcat(response, "}}");
    }
    else
    {
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

    // Finalize the statement
    sqlite3_finalize(stmt);

    // Close the SQLite database
    sqlite3_close(db);

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
            coap_add_data(params->coap_response, strlen(response),
                          (const uint8_t *)response);
        }
    }

    return ri;
}

// Delete CSEBase
int delete_csebases(sqlite3 *db)
{
    int rc;
    sqlite3_stmt *stmt;

    // Delete from supported_release_versions
    const char *srv_sql = "DELETE FROM supported_release_versions";
    rc = sqlite3_prepare_v2(db, srv_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from supported_resource_types
    const char *srt_sql = "DELETE FROM supported_resource_types";
    rc = sqlite3_prepare_v2(db, srt_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from points_of_access
    const char *poa_sql = "DELETE FROM points_of_access";
    rc = sqlite3_prepare_v2(db, poa_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from labels
    const char *lbl_sql = "DELETE FROM labels";
    rc = sqlite3_prepare_v2(db, lbl_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from content_serializations
    const char *csz_sql = "DELETE FROM content_serializations";
    rc = sqlite3_prepare_v2(db, csz_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from access_control_policy_ids
    const char *acpi_sql = "DELETE FROM access_control_policy_ids";
    rc = sqlite3_prepare_v2(db, acpi_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from content_instances
    const char *cin_sql = "DELETE FROM content_instances";
    rc = sqlite3_prepare_v2(db, cin_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from containers
    const char *cnt_sql = "DELETE FROM containers";
    rc = sqlite3_prepare_v2(db, cnt_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from application_entities
    const char *ae_sql = "DELETE FROM application_entities";
    rc = sqlite3_prepare_v2(db, ae_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from cse_bases
    const char *cse_base_sql = "DELETE FROM cse_bases";
    rc = sqlite3_prepare_v2(db, cse_base_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete from resources
    const char *resource_sql = "DELETE FROM resources";
    rc = sqlite3_prepare_v2(db, resource_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

// Função para verificar se existe uma CSEBase criada anteriormente
bool csebase_exists(sqlite3 *db)
{
    sqlite3_stmt *stmt;
    const char *sql = "SELECT 1 FROM cse_bases LIMIT 1";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK)
    {
        return false;
    }

    rc = sqlite3_step(stmt);
    bool exists = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    return exists;
}

// DISCOVER
void add_containers_to_ae(sqlite3 *db, json_object *json_array, const char *csebase_rn, const char *ae_ri, const char *ae_rn)
{
    sqlite3_stmt *stmt;
    const char *sql = "SELECT r.rn FROM containers c JOIN resources r ON c.ri = r.ri WHERE r.pi = ?;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, ae_ri, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *container_name = (const char *)sqlite3_column_text(stmt, 0);
        char container_path[256];
        snprintf(container_path, sizeof(container_path), "/%s/%s/%s", csebase_rn, ae_rn, container_name);
        json_object_array_add(json_array, json_object_new_string(container_path));
    }

    sqlite3_finalize(stmt);
}

void add_aes_to_csebase(sqlite3 *db, json_object *json_array, const char *csebase_rn, const char *csebase_ri, const char *app_name)
{
    sqlite3_stmt *stmt;
    const char *sql = app_name ? "SELECT ae.ri, r.rn FROM application_entities ae JOIN resources r ON ae.ri = r.ri WHERE r.pi = ? AND r.rn = ?;" : "SELECT ae.ri, r.rn FROM application_entities ae JOIN resources r ON ae.ri = r.ri WHERE r.pi = ?;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, csebase_ri, -1, SQLITE_STATIC);
    if (app_name)
    {
        sqlite3_bind_text(stmt, 2, app_name, -1, SQLITE_STATIC);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *ae_ri = (const char *)sqlite3_column_text(stmt, 0);
        const char *ae_name = (const char *)sqlite3_column_text(stmt, 1);

        if (!app_name)
        {
            char ae_path[256];
            snprintf(ae_path, sizeof(ae_path), "/%s/%s", csebase_rn, ae_name);
            json_object_array_add(json_array, json_object_new_string(ae_path));
        }

        add_containers_to_ae(db, json_array, csebase_rn, ae_ri, ae_name);
    }

    sqlite3_finalize(stmt);
}

void get_resource_names_json(struct response_params *params, const char *csebase_name, const char *app_name)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;
    // Open the database
    rc = sqlite3_open(DB_PATH, &db);

    if (rc != SQLITE_OK)
    {
        if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
        {
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            write(params->http_socket, response, strlen(response));
        }
        else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
        {
            coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_INTERNAL_ERROR);
        }
        return;
    }

    // Create the root JSON object
    json_object *root = json_object_new_object();
    json_object *json_array = json_object_new_array();

    // Query CSE Bases
    const char *sql = csebase_name ? "SELECT cb.ri, r.rn FROM cse_bases cb JOIN resources r ON cb.ri = r.ri WHERE r.rn = ?;" : "SELECT cb.ri, r.rn FROM cse_bases cb JOIN resources r ON cb.ri = r.ri;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (csebase_name)
    {
        sqlite3_bind_text(stmt, 1, csebase_name, -1, SQLITE_STATIC);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *csebase_ri = (const char *)sqlite3_column_text(stmt, 0);
        const char *csebase_rn = (const char *)sqlite3_column_text(stmt, 1);

        if (!csebase_name)
        {
            char csebase_path[256];
            snprintf(csebase_path, sizeof(csebase_path), "/%s", csebase_rn);
            json_object_array_add(json_array, json_object_new_string(csebase_path));
        }

        add_aes_to_csebase(db, json_array, csebase_rn, csebase_ri, app_name);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // Add the URIs array to the JSON object
    json_object_object_add(root, "m2m:uril", json_array);

    // Convert JSON object to string
    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);

    // Construct HTTP response
    char response[BUFFER_SIZE];
    if (params->protocol && strcmp(params->protocol, "HTTP") == 0)
    {
        snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json_str);
        write(params->http_socket, response, strlen(response));
    }
    else if (params->protocol && strcmp(params->protocol, "COAP") == 0)
    {
        coap_pdu_set_code(params->coap_response, COAP_RESPONSE_CODE_CONTENT);
        coap_add_data(params->coap_response, strlen(json_str), (const uint8_t *)json_str);
    }
    // Free JSON object
    json_object_put(root);
}

// Helper function to parse a comma-separated string into an array of strings
void parse_input(const char *input, char *output[], int *count)
{
    char *input_copy = strdup(input);
    char *token = strtok(input_copy, ",");
    while (token != NULL && *count < LABELS_NUMBER)
    {
        output[*count] = strdup(token);
        (*count)++;
        token = strtok(NULL, ",");
    }
    free(input_copy);
}