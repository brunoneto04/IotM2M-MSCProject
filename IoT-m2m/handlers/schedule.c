#include "../include/schedule.h"
#include "../include/logger.h"
#include <common.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Initializes the 'schedules' table in the SQLite database.
 * 
 * This function creates the table if it does not exist. 'ri' is the primary key,
 * and 'rn' is marked as UNIQUE to prevent duplicates under the same root
 * (although ideally in oneM2M it should be unique by 'pi' and 'rn', we keep 
 * global UNIQUE simplified according to the requirements).
 */
void init_schedule_table(void) {
    sqlite3 *db;
    char *err_msg = 0;
    
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc != SQLITE_OK) {
        LOG_ERR("Error: Could not open DB to initialize Schedules: %s", sqlite3_errmsg(db));
        return;
    }

    const char *sql = 
        "CREATE TABLE IF NOT EXISTS schedules ("
        "ri TEXT PRIMARY KEY, "
        "rn TEXT UNIQUE NOT NULL, "
        "pi TEXT NOT NULL, "
        "ct TEXT NOT NULL, "
        "lt TEXT NOT NULL, "
        "et TEXT, "
        "sce TEXT NOT NULL"
        ");";

    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERR("SQL Error creating schedules table: %s", err_msg);
        sqlite3_free(err_msg);
    } else {
        LOG("[DB] 'schedules' table verified/initialized successfully.");
    }

    sqlite3_close(db);
}

// --- Skeleton for future CRUD routes (Phase 7.4) ---

// --- Skeleton for Background Scheduling logic (Phase 7.5) ---