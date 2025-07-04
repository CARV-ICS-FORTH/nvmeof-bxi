#ifndef PTL_LOG_H
#define PTL_LOG_H
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifndef SPDK_PTL_ENABLE_DEBUG
#define SPDK_PTL_ENABLE_DEBUG 1
#endif

#if SPDK_PTL_ENABLE_DEBUG
#define SPDK_PTL_DEBUG(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "\x1b[32m[PTL_DEBUG][%s][%s:%s:%d] " fmt "\x1b[0m\n", \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
    } while (0)
#else
#define SPDK_PTL_DEBUG(fmt, ...) do {} while (0)
#endif

#define SPDK_PTL_INFO(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "[PTL_INFO][%s][%s:%s:%d] " fmt "\n",         \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
    } while (0)

#define SPDK_PTL_WARN(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "[PTL_WARN][%s][%s:%s:%d] " fmt "\n",         \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
    } while (0)

#define SPDK_PTL_FATAL(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "\x1b[31m[PTL_FATAL][%s][%s:%s:%d] " fmt "\x1b[0m\n", \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
        _exit(EXIT_FAILURE);                                                    \
    } while (0)

#if SPDK_PTL_ENABLE_DEBUG
#define SPDK_PTL_CORE(fmt, ...)                                             \
    do {                                                                      \
        time_t t = time(NULL);                                                \
        struct tm *tm = localtime(&t);                                        \
        char timestamp[32];                                                   \
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);      \
        fprintf(stderr, "\x1b[34m[PTL_CORE][%s][%s:%s:%d] GESALOUSTRA " fmt "\x1b[0m\n", \
                timestamp, __FILE__, __func__, __LINE__, ##__VA_ARGS__);      \
    } while (0)
#else
#define SPDK_PTL_CORE(fmt, ...) do {} while (0)
#endif

#endif
