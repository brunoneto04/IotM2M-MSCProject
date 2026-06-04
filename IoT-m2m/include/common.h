#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <arpa/inet.h>
#include <time.h>
#include <regex.h>
#include <ctype.h>
#include <stdbool.h>
#include "response_params.h"

#include "logger.h"

#define DB_PATH "./database/iotm2m.db"
#define BUFFER_SIZE 1024
#define LABELS_NUMBER 5
#define POAS_NUMBER 5
#define SRVS_NUMBER 5

char random_char();
char *generate_random_sequence();
char *generate_random_sequence_C();
char *generate_random_sequence_N();
int is_valid_datetime_format(const char *datetime);
int is_valid_datetime(const char *datetime);
int is_future_datetime(const char *datetime);
int is_datetime_less_than_current_plus_mia(const char *datetime, int mia);
char *get_current_time_plus_mia(int mia);
bool is_valid_string_plus_three_extra_chars(const char *str);

void send_response(struct response_params *params, int status_code, const char *response);
#endif