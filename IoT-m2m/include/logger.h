#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#ifdef ENABLE_LOGGING
  #define LOG(fmt, ...) \
    do { \
      fprintf(stderr, "[LOG] "); \
      fprintf(stderr, fmt, ##__VA_ARGS__); \
      fprintf(stderr, "\n"); \
    } while (0)
  #define LOG_ERROR(fmt, ...) \
    do { \
      fprintf(stderr, "[ERR] "); \
      fprintf(stderr, fmt, ##__VA_ARGS__); \
      fprintf(stderr, "\n"); \
    } while (0)
#else
  #define LOG(fmt, ...)       do {} while (0)
  #define LOG_ERROR(fmt, ...) do {} while (0)
#endif

#endif
