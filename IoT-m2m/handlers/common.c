/*
AUTHORSHIP -------------------------------------------------

 * File:    common.c
 * Authors: Bernardo Melo, Pedro Durães, Bruno Correia
 * Date:    May 2025
 * Description: Contains functions for server configurations.

*/

#include "../include/common.h"

char random_char()
{
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._"; // Unreserved characters from RFC 3986
    const size_t charset_length = sizeof(charset) - 1;
    return charset[rand() % charset_length];
}

// Function to generate a random sequence of 8 characters
char *generate_random_sequence()
{
    char *sequence = (char *)malloc(9 * sizeof(char)); // Allocate memory for 8 characters plus null terminator
    if (sequence == NULL)
    {
        // Memory allocation failed
        return NULL;
    }

    for (int i = 0; i < 8; i++)
    {
        sequence[i] = random_char(); // Generate random characters
    }
    sequence[8] = '\0'; // Null-terminate the string

    return sequence;
}

// Function to generate a random sequence of 8 characters starting with 'C'
char *generate_random_sequence_C()
{
    char *sequence = (char *)malloc(9 * sizeof(char)); // Allocate memory for 8 characters plus null terminator
    if (sequence == NULL)
    {
        // Memory allocation failed
        return NULL;
    }

    sequence[0] = 'C'; // Start with 'C'
    for (int i = 1; i < 8; i++)
    {
        sequence[i] = random_char(); // Generate random characters
    }
    sequence[8] = '\0'; // Null-terminate the string

    return sequence;
}

// Function to generate a random sequence of 8 characters starting with 'N'
char *generate_random_sequence_N()
{
    char *sequence = (char *)malloc(9 * sizeof(char)); // Allocate memory for 8 characters plus null terminator
    if (sequence == NULL)
    {
        // Memory allocation failed
        return NULL;
    }

    sequence[0] = 'N'; // Start with 'N'
    for (int i = 1; i < 8; i++)
    {
        sequence[i] = random_char(); // Generate random characters
    }
    sequence[8] = '\0'; // Null-terminate the string

    return sequence;
}

int is_valid_datetime_format(const char *datetime)
{
    // Check length
    if (strlen(datetime) != 19)
    {
        return 0;
    }

    // Check format
    for (int i = 0; i < 19; i++)
    {
        if (i == 4 || i == 7)
        {
            if (datetime[i] != '-')
            {
                return 0;
            }
        }
        else if (i == 10)
        {
            if (datetime[i] != ' ')
            {
                return 0;
            }
        }
        else if (i == 13 || i == 16)
        {
            if (datetime[i] != ':')
            {
                return 0;
            }
        }
        else
        {
            if (!isdigit(datetime[i]))
            {
                return 0;
            }
        }
    }
    return 1;
}

int is_valid_datetime(const char *datetime)
{
    int year, month, day, hour, min, sec;
    struct tm tm;

    // Parse the datetime string
    if (sscanf(datetime, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6)
    {
        return 0; // Failed to parse datetime
    }

    // Validate year, month, and day
    if (month < 1 || month > 12)
        return 0; // Month out of range
    if (day < 1 || day > 31)
        return 0; // Day out of range

    // Check for valid day in the month
    switch (month)
    {
    case 2: // February
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
        {
            if (day > 29)
                return 0;
        }
        else
        {
            if (day > 28)
                return 0;
        }
        break;
    case 4:
    case 6:
    case 9:
    case 11: // April, June, September, November
        if (day > 30)
            return 0;
        break;
    default: // All other months
        break;
    }

    // Validate hour, minute, and second
    if (hour < 0 || hour > 23)
        return 0; // Hour out of range
    if (min < 0 || min > 59)
        return 0; // Minute out of range
    if (sec < 0 || sec > 59)
        return 0; // Second out of range

    return 1; // Date and time are valid
}

// Function to check if a datetime string is in the future
int is_future_datetime(const char *datetime)
{
    int year, month, day, hour, min, sec;
    struct tm tm;

    memset(&tm, 0, sizeof(struct tm)); // Initialize tm to all zeros

    // Parse the datetime string
    if (sscanf(datetime, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6)
    {
        return 0; // Failed to parse datetime
    }

    // Fill in the tm struct
    tm.tm_year = year - 1900; // Years since 1900
    tm.tm_mon = month - 1;    // Months since January (0-based)
    tm.tm_mday = day;         // Day of the month (1–31)
    tm.tm_hour = hour;        // Hours since midnight (0–23)
    tm.tm_min = min;          // Minutes after the hour (0–59)
    tm.tm_sec = sec;          // Seconds after the minute (0–60)
    tm.tm_isdst = -1;         // Automatically determine daylight saving time

    // Convert to time_t
    time_t input_time = mktime(&tm);
    if (input_time == -1)
    {
        return 0; // Failed to convert time
    }

    // Get current time
    time_t current_time = time(NULL);

    // Debugging statements
    printf("Parsed input time: %s", ctime(&input_time));
    printf("Current time: %s", ctime(&current_time));

    // Check if the input time is in the future
    return difftime(input_time, current_time) > 0.0;
}

// Function to check if a datetime string is less than the current time plus mia seconds
int is_datetime_less_than_current_plus_mia(const char *datetime, int mia)
{
    int year, month, day, hour, min, sec;
    struct tm tm;

    memset(&tm, 0, sizeof(struct tm)); // Initialize tm to all zeros

    // Parse the datetime string
    if (sscanf(datetime, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6)
    {
        return 0; // Failed to parse datetime
    }

    // Fill in the tm struct
    tm.tm_year = year - 1900; // Years since 1900
    tm.tm_mon = month - 1;    // Months since January (0-based)
    tm.tm_mday = day;         // Day of the month (1–31)
    tm.tm_hour = hour;        // Hours since midnight (0–23)
    tm.tm_min = min;          // Minutes after the hour (0–59)
    tm.tm_sec = sec;          // Seconds after the minute (0–60)
    tm.tm_isdst = -1;         // Automatically determine daylight saving time

    // Convert to time_t
    time_t input_time = mktime(&tm);
    if (input_time == -1)
    {
        return 0; // Failed to convert time
    }

    // Get current time
    time_t current_time = time(NULL);

    // Add mia seconds to the current time
    time_t target_time = current_time + mia;

    // Debugging statements
    printf("Parsed input time: %s", ctime(&input_time));
    printf("Current time: %s", ctime(&current_time));
    printf("Target time (current time + mia): %s", ctime(&target_time));

    // Check if the input time is less than the target time
    return difftime(input_time, target_time) < 0.0;
}

// Function to get the current time plus max_instance_age in seconds as a string
char *get_current_time_plus_mia(int mia)
{
    // Get current time
    time_t current_time = time(NULL);

    // Add mia seconds to the current time
    time_t target_time = current_time + mia;

    // Convert target_time to struct tm
    struct tm *tm_target = localtime(&target_time);

    // Allocate memory for the resulting string
    char *result = (char *)malloc(20); // "YYYY-MM-DD HH:MM:SS" is 19 characters + null terminator

    if (result == NULL)
    {
        // Handle memory allocation failure
        fprintf(stderr, "Failed to allocate memory for result string\n");
        exit(EXIT_FAILURE);
    }

    // Format the target_time into a string
    if (strftime(result, 20, "%Y-%m-%d %H:%M:%S", tm_target) == 0)
    {
        // Handle strftime failure
        fprintf(stderr, "Failed to format time string\n");
        free(result);
        exit(EXIT_FAILURE);
    }

    return result;
}

bool is_valid_string_plus_three_extra_chars(const char *str)
{
    while (*str) {
        if (!isalnum((unsigned char)*str) && *str != '_' && *str != '.' && *str != '-') {
            return false;
        }
        str++;
    }
    return true;
}

void send_response(int client_socket, int status_code, const char* response) {
    char http_response[4096];
    const char* status_text = "OK";
    
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        default: status_text = "Internal Server Error";
    }

    snprintf(http_response, sizeof(http_response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             status_code, status_text,
             strlen(response), response);

    send(client_socket, http_response, strlen(http_response), 0);
}