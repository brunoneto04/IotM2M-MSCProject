//This file contains the parameters necessary to handle CoAP and HTTP responses using only one method for both
#ifndef RESPONSE_PARAMS_H
#define RESPONSE_PARAMS_H

#include <coap3/coap.h>

struct response_params {
    int http_socket;
    coap_session_t *coap_session;
    coap_pdu_t *coap_response;
    const char *protocol;
};

#endif