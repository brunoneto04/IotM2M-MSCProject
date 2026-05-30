/*
AUTHORSHIP EXAMPLE -------------------------------------------------

 * File:    main.c
 * Authors: Tiago Gomes, José Carpinteiro
 * Date:    28 March 2024
 * Description: Contains functions necessary to start the program.

COMMANDS TO RUN THE PROGRAM ----------------------------------------

Directory: /home/ubuntu/Desktop/iotm2m

gcc -o iotm2m main.c handlers/cseBase.c handlers/ae.c handlers/container.c handlers/contentInstance.c handlers/subscription.c handlers/common.c -I include -l sqlite3 -l json-c

./iotm2m
valgrind ./iotm2m
valgrind --leak-check=yes ./iotm2m
*/

/*
AUTHORSHIP File Updated

 * File:    main.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
*/

#include "main.h"
#include "response_params.h"
#include "mqtt_client.h"
#include "mqtt_handler.h"
#include <signal.h>
#include <pthread.h>

pthread_t web_server_thread, check_thread, coap_server_thread;
volatile sig_atomic_t stop = 0;

//Signal handler
void handle_signal(int signal)
{
    if (signal == SIGINT) {
        stop = 1;
    }
}

int main()
{

    system("mkdir -p certs");

    // Set up the SIGINT signal handler
    signal(SIGINT, handle_signal);

    // Write CSEBase value to database
    handle_request_csebase_create();

    // Start check and delete expired resources thread
    if (pthread_create(&check_thread, NULL, check_and_delete_expired_resources, NULL) != 0)
    {
        fprintf(stderr, "Error creating check thread\n");
        return 1;
    }

    // Create and start the web server thread
    if (pthread_create(&web_server_thread, NULL, start_web_server, NULL) != 0)
    {
        fprintf(stderr, "Error creating web server thread\n");
        return 1;
    }

    sleep(1); // Give the web server some time to start

    // Create and start the CoAP server thread
    if (pthread_create(&coap_server_thread, NULL, start_coap_server, NULL) != 0) {
        fprintf(stderr, "Error creating CoAP server thread\n");
        return 1;
    }

    sleep(1); // Give the CoAP server some time to start

    // Wait for the threads to complete (they won't in this case)
    pthread_join(check_thread, NULL);
    pthread_join(web_server_thread, NULL);
    pthread_join(coap_server_thread, NULL);

    return 0;
}

void *start_web_server(void *arg) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    struct timeval timeout;

    // Create the server socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        return NULL;
    }

    // Allow address reuse to avoid "Address already in use" error
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_socket);
        return NULL;
    }

    // Initialize the server address struct
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    // Bind the server socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket binding failed");
        close(server_socket);
        return NULL;
    }

    // Listen for incoming connections
    if (listen(server_socket, 10) == -1) {
        perror("Socket listening failed");
        close(server_socket);
        return NULL;
    }

    printf("[HTTP] Server listening on port %d...\n", PORT);

    // Accept incoming connections and handle requests
    while (!stop) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_socket, &fds);
        
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;

        int activity = select(server_socket + 1, &fds, NULL, NULL, &timeout);
        
        if (activity > 0 && FD_ISSET(server_socket, &fds)) {
            client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
            if (client_socket == -1) {
                perror("Accepting connection failed");
                continue;
            }
            handle_request(client_socket);
            close(client_socket);
        }
    }

    // Close the server socket
    close(server_socket);
    return NULL;
}

void *start_coap_server(void *arg) { 
    coap_context_t *ctx;
    coap_address_t serv_addr;
    coap_resource_t *resource;

    coap_startup();

    coap_address_init(&serv_addr);
    serv_addr.addr.sin.sin_family = AF_INET;
    serv_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;
    serv_addr.addr.sin.sin_port = htons(COAP_PORT);

    ctx = coap_new_context(NULL);
    if (!ctx) return NULL;

    if (!coap_new_endpoint(ctx, &serv_addr, COAP_PROTO_UDP)) {
        coap_free_context(ctx);
        return NULL;
    }

    printf("[CoAP] Server listening on port %d...\n", COAP_PORT);
    resource = coap_resource_unknown_init(handle_coap_request);
    coap_register_handler(resource, COAP_REQUEST_GET, handle_coap_request);
    coap_register_handler(resource, COAP_REQUEST_POST, handle_coap_request);
    coap_register_handler(resource, COAP_REQUEST_PUT, handle_coap_request);
    coap_register_handler(resource, COAP_REQUEST_DELETE, handle_coap_request);
    coap_add_resource(ctx, resource);

    while (!stop) {
        coap_io_process(ctx, 1000);
    }

    coap_free_context(ctx);
    coap_cleanup();
    printf("[CoAP] CoAP cleanup...");
    return NULL;
}

void handle_request(int client_socket)
{
    struct response_params http_params = {
    .http_socket = client_socket,
    .coap_session = NULL,
    .coap_response = NULL,
    .protocol = "HTTP"};
    char request[BUFFER_SIZE] = {0};
    char request_copy[BUFFER_SIZE] = {0}; // Copy of the original request
    char *method = NULL;
    char *csebase_name = NULL;
    char *app_name = NULL;
    char *container_name = NULL;
    char *content_name = NULL;

    char *subscription_name = NULL;

    char *key = NULL;

    char response[BUFFER_SIZE] = {0};
    

    // Read the HTTP request
    ssize_t bytes_read = read(client_socket, request, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        // Connection closed or error, do not process
        printf("[HTTP] Empty request\n");
        close(client_socket);
        return;
    }
    request[bytes_read] = '\0'; // Null-terminate

    strncpy(request_copy, request, BUFFER_SIZE - 1); // Make a copy of the request
    
    // Get key on the request
    key = get_json_key_from_request(request);

    // Find the start of query parameters and terminate the string there
    char *query_start = strchr(request_copy, '?');
    if (query_start != NULL)
    {
        *query_start = '\0';
    }

    // Tokenize the HTTP request using spaces as delimiters
    char *token = strtok(request_copy, " ");
    if (token != NULL)
    {
        method = strdup(token);
    }
    // Validate HTTP method
    if (method == NULL || (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0 && strcmp(method, "PUT") != 0 && strcmp(method, "DELETE") != 0))
    {
        // Invalid or unsupported HTTP method
        printf("[HTTP] Unsupported HTTP method: %s\n", method);
        const char *error_message = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
        write(client_socket, error_message, strlen(error_message));
        close(client_socket);
        free(key);
        free(method);
        return;
    }

    // Functionality to parse the 'fu' parameter from the original request
    char fu[10] = {0};

    // Search for "fu=" in the original request
    char *fu_start = strstr(request, "fu=");
    if (fu_start != NULL)
    {
        // Locate the start of the value
        fu_start += 3; // Move past "fu="

        // Copy the value until we reach a delimiter (either & or a space)
        char *fu_end = strpbrk(fu_start, "& ");
        size_t fu_length = (fu_end != NULL) ? (size_t)(fu_end - fu_start) : strlen(fu_start);

        // Copy the value to the output variable
        strncpy(fu, fu_start, fu_length);
        fu[fu_length] = '\0'; // Null-terminate the string
    }
    // Functionality to parse the 'ty' parameter from the original request
    char ty[10] = {0};

    // Search for "ty=" in the original request string
    char *ty_start = strstr(request, "ty=");
    if (ty_start != NULL)
    {
        // Move pointer past "ty="
        ty_start += 3;

        // Find the end of the value (next '&' or space or end of string)
        char *ty_end = strpbrk(ty_start, "& ");
        size_t ty_length = (ty_end != NULL) ? (size_t)(ty_end - ty_start) : strlen(ty_start);

        // Copy the value into the buffer, null-terminate
        strncpy(ty, ty_start, ty_length);
        ty[ty_length] = '\0';
    }

    // Tokenize the request path
    token = strtok(NULL, " ");
    if (token != NULL)
    {
        // Tokenize the URL path segment using forward slashes as delimiters
        token = strtok(token, "/");
        if (token != NULL)
        {
            // The first token after "GET /" is the csebase_name
            csebase_name = strdup(token);
            // Move to the next token
            token = strtok(NULL, "/");
            if (token != NULL)
            {
                // The second token is the app_name
                app_name = strdup(token);
                // Move to the next token
                token = strtok(NULL, "/");
                if (token != NULL)
                {
                    // The third token is the container_name
                    container_name = strdup(token);
                    // Move to the next token
                    token = strtok(NULL, "/");
                    if (token != NULL)
                    {
                        // The fourth token is the content_name
                        //if (key)

                        if (strcmp(ty, "23") == 0) {
                            char *query_start = strchr(token, '?');
                            if (query_start != NULL) {
                                *query_start = '\0';
                            }
                            subscription_name = strdup(token);
                        } else if (strcmp(ty, "4") == 0) {
                            char *query_start = strchr(token, '?');
                            if (query_start != NULL) {
                                *query_start = '\0';
                            }
                            content_name = strdup(token);
                        } else {
                            strcpy(ty, "-1"); // User has put a content name, but it did not put the ty. 
                        }
                    }
                }
            }
        }
    }

    // Print the parsed values
    printf("[HTTP] Parsed fu value: %s\n", fu);
    printf("[HTTP] Parsed ty value: %s\n", ty);
    printf("[HTTP] Method:    %s\nCSEBase:   %s\nAE:        %s\nContainer: %s\nContent:   %s\nSubscription:   %s\n", method, csebase_name, app_name, container_name, content_name, subscription_name);

    if (strcmp(method, "GET") == 0)
    {
        // Handle GET request
        printf("[HTTP] GET request received\n");
        if (content_name != NULL || subscription_name != NULL || ty[0] != '\0')
        {
            if (strcmp(ty, "23") == 0)
            {
                char resource_uri[512] = {0};
                // Build complete resource URI
                snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s",
                        csebase_name, app_name, container_name);
                
                bool getSubscription = handle_get_subscription(client_socket, subscription_name, resource_uri);
                //printf("%u\n", getSubscription);
            }
            else if (strcmp(ty, "4") == 0)
            {
                char *jsonBody = handle_request_cin_get(&http_params, csebase_name, app_name, container_name, content_name, "GET");
                if (jsonBody != NULL)
                {
                    // Process jsonBody if needed
                    free(jsonBody);
                }
            }
            else
            {
                snprintf(response, BUFFER_SIZE, "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n%s", strlen("Missing or invalid 'ty' parameter"), "Missing or invalid 'ty' parameter\n");
                write(client_socket, response, strlen(response));
            }
        }
        else if (container_name != NULL)
        {
            char *jsonBody = handle_request_container_get(&http_params, csebase_name, app_name, container_name, "GET");
            if (jsonBody != NULL)
            {
                free(jsonBody);
            }
        }
        else if (app_name != NULL)
        {
            // Discover Functions
            if (strcmp(fu, "1") == 0)
            {
                get_resource_names_json(&http_params, csebase_name, app_name);
            }
            else
            {
                char *jsonBody = handle_request_ae_get(&http_params, csebase_name, app_name, "GET");
                if (jsonBody != NULL)
                {
                    free(jsonBody);
                }
            }
        }
        else
        {
            // Discover Functions
            if (strcmp(fu, "1") == 0)
            {
                get_resource_names_json(&http_params, csebase_name, NULL);
            }
            else
            {
                char *ri = handle_request_csebase_get(&http_params, csebase_name, "GET");
                if (ri != NULL)
                {
                    free(ri);
                }
            }
        }
    }
    else if (strcmp(method, "POST") == 0)
    {
        char *body_start = strstr(request, "\r\n\r\n");
        char *body = NULL;

        printf("[HTTP] POST request received\n");
        if (body_start == NULL || *(body_start + 4) == '\0')
        {
            // No body found in request or empty body
            printf("[HTTP] No body provided: %s\n", method);
            const char *error_message = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: 28\r\n\r\n{\"error\":\"No body provided\"}";
            write(client_socket, error_message, strlen(error_message));
            close(client_socket);
            free(key);
            free(method);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            free(subscription_name);
            return;
        }

        body = body_start + 4; // Body starts after the double newline sequence

        if (content_name != NULL)
        {
            // Unsupported parameter scenario: content_name
            printf("[HTTP] Unsupported parameter scenario: content_name\n");
            const char *error_message = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            write(client_socket, error_message, strlen(error_message));

            close(client_socket);
            free(key);
            free(method);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            free(subscription_name);
            return;
        }
        else if (container_name != NULL)
        {
            // Check if the request is for a subscription
            char *sub_key = get_json_key_from_request(request);
            if (sub_key && strcmp(sub_key, "m2m:sub") == 0) { // Check if the request is "m2m:sub"
                
                // VALIDATION: Check if csebase_name, ae_name and container exist
                struct response_params validation_params = {0};
                validation_params.protocol = "HTTP";  // Set protocol for validation
                validation_params.http_socket = client_socket;
                
                char *jsonBody = handle_request_container_get(&validation_params, csebase_name, app_name, container_name, "INTERNAL");
                
                if (!jsonBody || jsonBody[0] == '\0') {
                    // Container doesn't exist - send only JSON error
                    const char *error_json = "{\"error\":\"Container not found for subscription\"}";
                    write(client_socket, error_json, strlen(error_json));

                    if (jsonBody) {
                        free(jsonBody);
                    }
                    free(sub_key);
                    close(client_socket);
                    free(key);
                    free(method);
                    free(csebase_name);
                    free(app_name);
                    free(container_name);
                    free(content_name);
                    free(subscription_name);
                    return;
                }
                // Container exists, proceed with subscription handling
                free(jsonBody); // Clean up validation result
                
                char response[2048] = {0};
                char resource_uri[512] = {0};
                
                // Build complete resource URI
                snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s",
                        csebase_name, app_name, container_name);
                        
                if (handle_subscription_request(body, resource_uri, "CAdmin",
                                            response, sizeof(response), client_socket)) {
                    const char *success = "HTTP/1.1 201 Created\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Content-Length: %zu\r\n"
                                        "\r\n%s";
                    char final_response[8192];
                    snprintf(final_response, sizeof(final_response),
                            success, strlen(response), response);
                    write(client_socket, final_response, strlen(final_response));
                } else {
                    //Verificar se a resposta HTTP já está completa
                    if (strlen(response) > 0 && strncmp(response, "HTTP/1.1", 8) == 0) {
                        //Resposta HTTP já formatada (422, etc.) - enviar diretamente
                        write(client_socket, response, strlen(response));
                    } else if (strlen(response) > 0) {
                        char full_response[8192];
                        snprintf(full_response, sizeof(full_response),
                                "HTTP/1.1 400 Bad Request\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: %zu\r\n"
                                "\r\n%s",
                                strlen(response), response);
                        write(client_socket, full_response, strlen(full_response));
                    } else {
                        const char *error = "HTTP/1.1 400 Bad Request\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Content-Length: 27\r\n"
                                        "\r\n{\"error\":\"Invalid request\"}";
                        write(client_socket, error, strlen(error));
                    }
                }
                free(sub_key);
            } else if (key && strcmp(key, "m2m:cin") == 0) { // Check if the request is "m2m:cin"
                free(sub_key);
                handle_request_cin_post(&http_params, csebase_name, app_name, container_name, request, body);
            } else {
                free(sub_key);
                // Unsupported request type
                const char *error_message = "HTTP/1.1 400 Bad Request\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Content-Length: 32\r\n"
                                        "\r\n{\"error\":\"Unsupported request\"}";
                write(client_socket, error_message, strlen(error_message));
            }
        }
        else if (app_name != NULL)
        {
            handle_request_container_post(&http_params, csebase_name, app_name, request, body);
        }
        else if (csebase_name != NULL)
        {
            handle_request_ae_post(&http_params, csebase_name, request, body);
        }
        else
        {
            // Unsupported parameter scenario: csebase_name
            printf("[HTTP] Unsupported parameter scenario: csebase_name\n");
            const char *error_message = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            write(client_socket, error_message, strlen(error_message));
            close(client_socket);
            free(key);
            free(method);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            return;
        }
    }
    else if (strcmp(method, "PUT") == 0)
    {
        char *body_start = strstr(request, "\r\n\r\n");
        char *body = NULL;

        if (body_start != NULL)
        {
            body = body_start + 4; // Body starts after the double newline sequence
            printf("[HTTP] Body:\n%s\n", body);
            
            if (*body == '\0')
            {
                // No body provided
                printf("[HTTP] No body provided: %s\n", method);
                const char *error_message = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: 28\r\n\r\n{\"error\":\"No body provided\"}";
                write(client_socket, error_message, strlen(error_message));
                close(client_socket);
                free(key);
                free(method);
                free(csebase_name);
                free(app_name);
                free(container_name);
                free(content_name);
                return;
            }
        }

        // Handle PUT request
        printf("[HTTP] PUT request received\n");
        if (content_name != NULL)
        {
            // handle_request_content_put(&http_params, csebase_name, app_name, container_name, content_name, body);
        }
        else if (container_name != NULL)
        {
            handle_request_container_put(&http_params, csebase_name, app_name, container_name, request, body);
        }
        else if (app_name != NULL)
        {
            handle_request_ae_put(&http_params, csebase_name, app_name, request, body);
        }
        else
        {
            // Unsupported parameter scenario: csebase_name
            printf("[HTTP] Unsupported parameter scenario: %s\n", method);
            const char *error_message = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            write(client_socket, error_message, strlen(error_message));
            close(client_socket);
            free(key);
            free(method);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            return;
        }
    }
    else if (strcmp(method, "DELETE") == 0)
    {
        // Handle DELETE request
        printf("[HTTP] DELETE request received\n");
        if (subscription_name == NULL && content_name == NULL && container_name != NULL && (strcmp(ty, "-1") != 0))
        {
            handle_request_container_delete(&http_params, csebase_name, app_name, container_name, "DELETE", NULL);
        }
        else if (subscription_name == NULL && content_name == NULL && app_name != NULL && (strcmp(ty, "-1") != 0))
        {
            handle_request_ae_delete(&http_params, csebase_name, app_name, "DELETE", NULL);
        }
        else if (subscription_name != NULL && strcmp(ty, "23") == 0) // Verify if it is a subscription to delete
        {
            //printf("Subscription name exists in the url, %s\n", subscription_name);

            // Delete subscription
            bool getSubscription = handle_delete_subscription(client_socket, subscription_name);

            //printf("Ola1000000\n");
        }
        else
        {
            // Unsupported parameter scenario: csebase_name
            printf("[HTTP] Unsupported parameter scenario: %s\n", method);
            const char *error_message = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";
            write(client_socket, error_message, strlen(error_message));
            close(client_socket);
            free(key);
            free(method);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            free(subscription_name);
            return;
        }
    }

    close(client_socket);
    free(method);
    free(csebase_name);
    free(app_name);
    free(container_name);
    free(content_name);

    free(subscription_name);
    free(key);
}

void handle_coap_request(coap_resource_t *resource,coap_session_t *session,const coap_pdu_t *request,const coap_string_t *query,coap_pdu_t *response)
{
    struct response_params coap_params = {
    .http_socket = -1,
    .coap_session = session,
    .coap_response = response,
    .protocol = "COAP"};
    const char *method = NULL;
    char *csebase_name = NULL;
    char *app_name = NULL;
    char *container_name = NULL;
    char *content_name = NULL;

    // Handle malformed messages (RE)
    if (!request) {
        coap_send_rst(session, request);
        return;
    }
    
    /// Get message type and message ID
    coap_pdu_type_t msg_type = coap_pdu_get_type(request);
    unsigned int msg_id = coap_pdu_get_mid(request);
    
    printf("\n[CoAP] CoAP request received: \nMID=%u", coap_pdu_get_mid(request));
    
    if (msg_type == COAP_MESSAGE_CON) {
        coap_pdu_set_type(response, COAP_MESSAGE_ACK);
        coap_pdu_set_mid(response, msg_id);  // Match message ID
        printf("\n[CoAP] Type=CON\n");
    } 
    else if (msg_type == COAP_MESSAGE_NON) {
        // Non-confirmable message - send NON response
        coap_pdu_set_type(response, COAP_MESSAGE_NON);
        printf("\n[CoAP] Type=NON\n");
    }
    else if (msg_type == COAP_MESSAGE_ACK) {
        printf("\n[CoAP] Type=ACK\n");
        return;
    }

    
    
    // Extract the CoAP method 
    coap_pdu_code_t method_code = coap_pdu_get_code(request);
    switch (method_code) {
        case COAP_REQUEST_GET:
            method = "GET";
            break;
        case COAP_REQUEST_POST:
            method = "POST";
            break;
        case COAP_REQUEST_PUT:
            method = "PUT";
            break;
        case COAP_REQUEST_DELETE:
            method = "DELETE";
            break;
        default:
            // Send RESET response for malformed request
            coap_send_rst(session, request);
            return;
    }

    if (method == NULL) 
    {
        printf("[CoAP] Failed to map CoAP method\n");
        coap_pdu_set_code(response, COAP_RESPONSE_CODE(500));
        const char *msg = "Internal Server Error";
        coap_add_data(response, strlen(msg), (const uint8_t *)msg);
        coap_send(session, response);
        return;
    }

    // Extract URI path from the CoAP pdu
    coap_string_t *uri_path = coap_get_uri_path(request);
    if (uri_path) {
        printf("[CoAP] URI Path: %.*s\n", (int)uri_path->length, uri_path->s);
        // Tokenize the URI path using '/' as a delimiter
        char *path_copy = strndup((const char *)uri_path->s, uri_path->length);
        char *token = strtok(path_copy, "/");
        if (token != NULL) {
            //Get csebase_name
            csebase_name = strdup(token);
            token = strtok(NULL, "/");
            if (token != NULL) {
                //Get app_name
                app_name = strdup(token);
                token = strtok(NULL, "/");
                if (token != NULL) {
                    //Get container_name
                    container_name = strdup(token);
                    token = strtok(NULL, "/");
                    if (token != NULL) {
                        //Get content_name
                        content_name = strdup(token);
                    }
                }
            }
        }
        coap_delete_string((coap_string_t *)uri_path);
        free(path_copy);
    }

    const uint8_t *data;
    size_t data_len;
    
    char fu[10] = {0};
    // Get query from request
    if (query && query->length > 0) {
        char *query_str = strndup((char *)query->s, query->length);
        char *fu_start = strstr(query_str, "fu=");
        if (fu_start != NULL) {
            // Move past "fu="
            fu_start += 3;
            
            // Copy the value until delimiter
            char *fu_end = strpbrk(fu_start, "& ");
            size_t fu_length = (fu_end != NULL) ? 
                            (size_t)(fu_end - fu_start) : 
                            strlen(fu_start);
            
            // Copy value to output
            strncpy(fu, fu_start, fu_length);
            fu[fu_length] = '\0';
        }
        free(query_str);
    }

    // Print the parsed values
    printf("[CoAP] Method:    %s\nCSEBase:   %s\nAE:        %s\nContainer: %s\nContent:   %s\n", method, csebase_name, app_name, container_name, content_name);

    // Handle the CoAP request based on the method
    if (strcmp(method, "GET") == 0) {
        printf("[CoAP] Handling GET request\n");
        if (content_name != NULL)
        {
            char *jsonBody = handle_request_cin_get(&coap_params, csebase_name, app_name, container_name, content_name, "GET");
            if (jsonBody != NULL)
            {
                free(jsonBody);
            }
        } else if (container_name != NULL) {
            char *jsonBody = handle_request_container_get(&coap_params, csebase_name, app_name, container_name, "GET");
            if (jsonBody != NULL)
            {
                free(jsonBody);
            }
        } else if (app_name != NULL) {
            // Discover Functions
            if (strcmp(fu, "1") == 0)
            {
                get_resource_names_json(&coap_params, csebase_name, app_name);
            } else {
                char *jsonBody = handle_request_ae_get(&coap_params, csebase_name, app_name, "GET");
                if (jsonBody != NULL)
                {
                    free(jsonBody);
                }
            }
        } else {
            // Discover Functions
            if (strcmp(fu, "1") == 0)
            {
                get_resource_names_json(&coap_params, csebase_name, NULL);
            } else {
                char *ri = handle_request_csebase_get(&coap_params, csebase_name, "GET");
                if (ri != NULL)
                {
                    free(ri);
                }
            }
        }
    } else if (strcmp(method, "POST") == 0) 
    {
        printf("[CoAP] Handling POST request\n");
        // Get payload data from request
        const uint8_t *payload;
        size_t payload_len;
        coap_get_data(request, &payload_len, &payload);
        char *body = NULL;

        if (payload_len > 0) {
            body = strndup((char *)payload, payload_len);
        }

        if (!body || strlen(body) == 0) {
            // No body provided
            printf("[CoAP] No body provided: %s\n", method);
            coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
            const char *error_msg = "{\"error\":\"No body provided\"}";
            coap_add_data(response, strlen(error_msg), (const uint8_t *)error_msg);
            free(body);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            return;
        }

        if (content_name != NULL) {
            // Unsupported parameter scenario: content_name
            printf("[CoAP] Unsupported parameter scenario: content_name\n");
            coap_pdu_set_code(response, COAP_RESPONSE_CODE_NOT_ALLOWED);
            free(body);
            return;
        } else if (container_name != NULL) {

            char *key = extract_json_key(body);

            if (key && strcmp(key, "m2m:sub") == 0) {
                char response_buf[2048] = {0};
                char resource_uri[512] = {0};
                snprintf(resource_uri, sizeof(resource_uri), "/%s/%s/%s",csebase_name, app_name, container_name);
            } else if (key && strcmp(key, "m2m:cin") == 0) {
                handle_request_cin_post(&coap_params, csebase_name, app_name, container_name, body, body);
            } else {
                coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
            }
            free(key);
        } else if (app_name != NULL) {
            handle_request_container_post(&coap_params, csebase_name, app_name,body, body);
        } else if (csebase_name != NULL) {
            handle_request_ae_post(&coap_params, csebase_name, body, body);
        } else {
            printf("[CoAP] Unsupported parameter scenario: csebase_name\n");
            coap_pdu_set_code(response, COAP_RESPONSE_CODE_NOT_ALLOWED);
        }

        free(body);
    } else if (strcmp(method, "PUT") == 0) {
        printf("[CoAP] Handling PUT request\n");
        // Get payload data from request
        const uint8_t *payload;
        size_t payload_len;
        coap_get_data(request, &payload_len, &payload);
        char *body = NULL;

        if (payload_len > 0) {
            body = strndup((char *)payload, payload_len);
            printf("[CoAP] Body:\n%s\n", body);
        }

        if (!body || strlen(body) == 0) {
            // No body provided
            printf("[CoAP] No body provided: %s\n", method);
            coap_pdu_set_code(response, COAP_RESPONSE_CODE_BAD_REQUEST);
            const char *error_msg = "{\"error\":\"No body provided\"}";
            coap_add_data(response, strlen(error_msg), (const uint8_t *)error_msg);
            free(body);
            free(csebase_name);
            free(app_name);
            free(container_name);
            free(content_name);
            return;
        }
        // Handle PUT request
        if (content_name != NULL) {
            //Não existe handle_request_cin_put, por alguma razão não explorada
            // handle_request_content_put(&coap_params, csebase_name, app_name, container_name, content_name, body);
        }
        else if (container_name != NULL) {
            handle_request_container_put(&coap_params, csebase_name, app_name,container_name, body, body);
        }
        else if (app_name != NULL) {
            handle_request_ae_put(&coap_params, csebase_name, app_name, body, body);
        }
        else {
            // Unsupported parameter scenario
            printf("[CoAP] Unsupported parameter scenario: %s\n", method);
            coap_pdu_set_code(response, COAP_RESPONSE_CODE_NOT_ALLOWED);
        }
        free(body);
    } else if (strcmp(method, "DELETE") == 0) {
        printf("[CoAP] Handling DELETE request\n");
        if (content_name == NULL && container_name != NULL) {
            handle_request_container_delete(&coap_params, csebase_name, app_name, 
                                        container_name, "DELETE", NULL);
        }
        else if (content_name == NULL && app_name != NULL) {
            handle_request_ae_delete(&coap_params, csebase_name, app_name, 
                                "DELETE", NULL);
        }
        else {
            // Unsupported parameter scenario
            printf("[CoAP] Unsupported parameter scenario: %s\n", method);
            coap_pdu_set_code(response, COAP_RESPONSE_CODE_NOT_ALLOWED);
        }
    }
    // Free allocated memory
    free(csebase_name);
    free(app_name);
    free(container_name);
    free(content_name);
}

// Function to delete expired resources
void *check_and_delete_expired_resources(void *arg)
{
    struct response_params http_params = {
    .http_socket = -1,
    .coap_session = NULL,
    .coap_response = NULL,
    .protocol = "HTTP"};
    sqlite3 *db;
    char *err_msg = 0;
    int rc;
    const char *sql_query = 
        "SELECT r_cb.rn AS csebase_rn, "
            "r_ae.rn AS application_entity_rn, "
            "r_c.rn AS container_rn, "
            "r_ci.rn AS content_instance_rn "
        "FROM content_instances ci "
        "JOIN resources r_ci ON ci.ri = r_ci.ri "
        "JOIN containers c ON r_ci.pi = c.ri "
        "JOIN resources r_c ON c.ri = r_c.ri "
        "JOIN application_entities ae ON r_c.pi = ae.ri "
        "JOIN resources r_ae ON ae.ri = r_ae.ri "
        "JOIN cse_bases cb ON r_ae.pi = cb.ri "
        "JOIN resources r_cb ON cb.ri = r_cb.ri "
        "WHERE ci.et < ? "
        "UNION ALL "
        "SELECT r_cb.rn AS csebase_rn, "
            "r_ae.rn AS application_entity_rn, "
            "r_c.rn AS container_rn, "
            "NULL AS content_instance_rn "
        "FROM containers c "
        "JOIN resources r_c ON c.ri = r_c.ri "
        "JOIN application_entities ae ON r_c.pi = ae.ri "
        "JOIN resources r_ae ON ae.ri = r_ae.ri "
        "JOIN cse_bases cb ON r_ae.pi = cb.ri "
        "JOIN resources r_cb ON cb.ri = r_cb.ri "
        "WHERE c.et < ? "
        "UNION ALL "
        "SELECT r_cb.rn AS csebase_rn, "
            "r_ae.rn AS application_entity_rn, "
            "NULL AS container_rn, "
            "NULL AS content_instance_rn "
        "FROM application_entities ae "
        "JOIN resources r_ae ON ae.ri = r_ae.ri "
        "JOIN cse_bases cb ON r_ae.pi = cb.ri "
        "JOIN resources r_cb ON cb.ri = r_cb.ri "
        "WHERE ae.et < ? ;";

    while (!stop)
    {
        // Open database connection
        rc = sqlite3_open(DB_PATH, &db);
        if (rc)
        {
            fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
            return NULL;
        }

        // Prepare SQL statement
        sqlite3_stmt *stmt;
        rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, 0);

        // Get current time
        time_t c_time = time(NULL);
        struct tm *tm_target = localtime(&c_time);
        char current_time[20];
        strftime(current_time, sizeof(current_time), "%Y-%m-%d %H:%M:%S", tm_target);
        //printf("Current UTC timestamp: %s\n", current_time);

        sqlite3_bind_text(stmt, 1, current_time, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, current_time, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, current_time, -1, SQLITE_STATIC);

        // Execute the SQL statement
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            char *csebase_rn = (char *)sqlite3_column_text(stmt, 0);
            char *application_entity_rn = (char *)sqlite3_column_text(stmt, 1);
            char *container_rn = (char *)sqlite3_column_text(stmt, 2);
            char *content_instance_rn = (char *)sqlite3_column_text(stmt, 3);

            printf("[CoAP] csebase_rn: %s\n", csebase_rn != NULL ? csebase_rn : "NULL");
            printf("[CoAP] application_entity_rn: %s\n", application_entity_rn != NULL ? application_entity_rn : "NULL");
            printf("[CoAP] container_rn: %s\n", container_rn != NULL ? container_rn : "NULL");
            printf("[CoAP] content_instance_rn: %s\n", content_instance_rn != NULL ? content_instance_rn : "NULL");

            // Determine which delete function to call
            if (content_instance_rn != NULL)
            {
                handle_request_cin_delete(&http_params, csebase_rn, application_entity_rn, container_rn, content_instance_rn, "INTERNAL", db);
            }
            else if (container_rn != NULL)
            {
                handle_request_container_delete(&http_params, csebase_rn, application_entity_rn, container_rn, "INTERNAL", db);
            }
            else if (application_entity_rn != NULL)
            {
                handle_request_ae_delete(&http_params, csebase_rn, application_entity_rn, "INTERNAL", db);
            }
        }

        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "Execution failed: %s\n", sqlite3_errmsg(db));
        }

        // Clean up
        sqlite3_finalize(stmt);
        sqlite3_close(db);

        // Sleep for 1 second
        sleep(1);
    }
    return NULL;
}


char *get_json_key_from_request(const char *request) {
    const char *body_start = strstr(request, "\r\n\r\n");
    if (!body_start) return NULL;
    body_start += 4;

    return extract_json_key(body_start);
}

char *extract_json_key(const char *json) {
    while (*json == ' ' || *json == '\n' || *json == '\r' || *json == '\t') {
        json++;
    }

    if (*json != '{') return NULL;

    const char *key_start = strchr(json, '"');
    if (!key_start) return NULL;
    key_start++;

    const char *key_end = strchr(key_start, '"');
    if (!key_end) return NULL;

    size_t key_len = key_end - key_start;
    char *key = malloc(key_len + 1);
    if (!key) return NULL;

    strncpy(key, key_start, key_len);
    key[key_len] = '\0';

    return key;
}

//MQTT callback functions
void on_connect_callback(void* context) {
    printf("[MQTT] Connected to MQTT broker\n");
}

void on_message_callback(void* context, const char* topic, const char* payload) {
    printf("[MQTT] Received MQTT message:\nTopic: %s\nPayload: %s\n", topic, payload);
}

void on_disconnect_callback(void* context) {
    printf("[MQTT] Disconnected from MQTT broker\n");
}

void on_error_callback(void* context, int rc) {
    printf("[MQTT] Error occurred: %d\n", rc);
    if (rc == 5) { // Authorization error
        printf("[MQTT] Authorization error - shutting down\n");
        stop = 1;
    }
}