#ifndef LOG_H
#define LOG_H

#include "general.h"

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_SUCCESS,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LOG_LEVEL;

VOID LogInit(VOID);
VOID LogSetLevel(LOG_LEVEL level);
VOID LogPrint(LOG_LEVEL level, CONST CHAR16 *fmt, ...);
VOID LogWaitKeyOrTimeout(UINTN seconds);

#define LOG_DEBUG(...)   LogPrint(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)    LogPrint(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_SUCCESS(...) LogPrint(LOG_LEVEL_SUCCESS, __VA_ARGS__)
#define LOG_WARN(...)    LogPrint(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...)   LogPrint(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif
