#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <sqlite3.h>
#include <stdbool.h>

/**
 * @brief Data Structure for the Schedule Resource (TS-0001)
 * 
 * Contains the mandatory fields from the oneM2M specification for 
 * resource scheduling, often interacting with Actions.
 */
typedef struct {
    char *ri;   // Resource ID (Unique identifier)
    char *rn;   // Resource Name (Readable name of the resource)
    char *pi;   // Parent ID (ID of the parent resource)
    char *ct;   // Creation Time
    char *lt;   // Last Modified Time
    char *et;   // Expiration Time
    char *sce;  // Schedule Element (Scheduling pattern, e.g., cron)
} Schedule;

void init_schedule_table(void);

#endif