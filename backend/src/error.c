#include "mc_error.h"

static const char *error_strings[] = {
    [MC_OK]             = "OK",
    [MC_ERR_CONFIG]     = "Configuration error",
    [MC_ERR_DB]         = "Database error",
    [MC_ERR_HTTP]       = "HTTP request error",
    [MC_ERR_PARSE]      = "Parse error",
    [MC_ERR_WS]         = "WebSocket error",
    [MC_ERR_THREAD]     = "Thread error",
    [MC_ERR_IO]         = "I/O error",
    [MC_ERR_OOM]        = "Out of memory",
    [MC_ERR_RATE_LIMIT] = "Rate limited",
    [MC_ERR_API_KEY]    = "Invalid API key",
    [MC_ERR_NOT_FOUND]  = "Not found",
};

const char *mc_error_str(mc_error_t err)
{
    if ((int)err < 0 || err > MC_ERR_NOT_FOUND)
        return "Unknown error";
    return error_strings[err];
}
