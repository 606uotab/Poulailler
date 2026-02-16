#include "mc_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

static mc_log_level_t g_level = MC_LOG_LVL_INFO;
static FILE          *g_logfile = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_names[] = {
    [MC_LOG_LVL_DEBUG] = "DEBUG",
    [MC_LOG_LVL_INFO]  = "INFO",
    [MC_LOG_LVL_WARN]  = "WARN",
    [MC_LOG_LVL_ERROR] = "ERROR",
};

void mc_log_init(mc_log_level_t level, const char *logfile)
{
    g_level = level;
    if (logfile && logfile[0]) {
        g_logfile = fopen(logfile, "a");
        if (!g_logfile)
            fprintf(stderr, "[WARN] Failed to open log file: %s\n", logfile);
    }
}

void mc_log_shutdown(void)
{
    if (g_logfile) {
        fclose(g_logfile);
        g_logfile = NULL;
    }
}

void mc_log_write(mc_log_level_t level, const char *file, int line,
                  const char *fmt, ...)
{
    if (level < g_level)
        return;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    /* Extract basename from file path */
    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    pthread_mutex_lock(&g_log_mutex);

    FILE *out = g_logfile ? g_logfile : stderr;

    fprintf(out, "[%s] [%-5s] %s:%d: ", timebuf, level_names[level],
            basename, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);

    pthread_mutex_unlock(&g_log_mutex);
}
