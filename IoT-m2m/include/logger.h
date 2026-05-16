#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#ifdef ENABLE_LOGGING
  #define LOG(fmt, ...)   fprintf(stderr, "[LOG] " fmt "\n", ##__VA_ARGS__)
  #define LOG_ERR(fmt, ...) fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(fmt, ...)
  #define LOG_ERR(fmt, ...)
#endif

#endif