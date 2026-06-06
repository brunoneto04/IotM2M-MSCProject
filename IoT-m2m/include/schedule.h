#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <sqlite3.h>
#include <stdbool.h>
#include "response_params.h"

/**
 * @brief Data Structure for the Schedule Resource (TS-0001)
 * 
 * Contains the mandatory fields from the oneM2M specification for 
 * resource scheduling, often interacting with Actions.
 */
typedef struct {
    char *ri;   // Resource ID — FK to resources.ri
    char *rn;   // Resource Name — from resources.rn
    char *pi;   // Parent ID — from resources.pi
    char *ct;   // Creation Time — from resources.ct
    char *lt;   // Last Modified Time — from resources.lt
    char *et;   // Expiration Time — stored in schedules.et
    char *sce;  // Schedule Element (cron pattern) — stored in schedules.sce
} Schedule;

void init_schedule_table(void);

void handle_schedule_create(struct response_params *params, const char *pi, const char *body);
void handle_schedule_retrieve(struct response_params *params, const char *identifier);
void handle_schedule_update(struct response_params *params, const char *identifier, const char *body);
void handle_schedule_delete(struct response_params *params, const char *identifier);

/* Evaluates a cron-style sce field against the current local time.
 * Format: "minute hour day-of-month month day-of-week"
 * Supports: *, exact numbers, comma lists (1,3), ranges (1-5), steps (*\/2).
 * Returns true when the current time matches all five fields. */
bool evaluate_schedule(const char *sce);

/* Start/stop the background scheduler pthread. */
void start_scheduler_thread(void);
void stop_scheduler_thread(void);

#endif