#ifndef TINY_CU_LOG_H
#define TINY_CU_LOG_H

#include <stdio.h>

// Standard Error Logging (Always enabled)
#define TCU_ERR(fmt, ...) \
    fprintf(stderr, "[TCU_ERR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

// Standard Info Logging (Always enabled)
#define TCU_INFO(fmt, ...) \
    fprintf(stdout, "[TCU_INFO] " fmt "\n", ##__VA_ARGS__)

// Debug Logging (Enabled only if we compile with -DTINY_CU_DEBUG)
#ifdef TINY_CU_DEBUG
    #define TCU_DEBUG(fmt, ...) \
        fprintf(stdout, "[TCU_DEBUG] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
    #define TCU_DEBUG(fmt, ...) do {} while (0)
#endif

#endif // TINY_CU_LOG_H
