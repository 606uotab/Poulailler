#ifndef MC_ERROR_H
#define MC_ERROR_H

typedef enum {
    MC_OK = 0,
    MC_ERR_CONFIG,
    MC_ERR_DB,
    MC_ERR_HTTP,
    MC_ERR_PARSE,
    MC_ERR_WS,
    MC_ERR_THREAD,
    MC_ERR_IO,
    MC_ERR_OOM,
    MC_ERR_RATE_LIMIT,
    MC_ERR_API_KEY,
    MC_ERR_NOT_FOUND
} mc_error_t;

const char *mc_error_str(mc_error_t err);

#endif
