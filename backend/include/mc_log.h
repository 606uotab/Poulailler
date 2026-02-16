#ifndef MC_LOG_H
#define MC_LOG_H

typedef enum {
    MC_LOG_LVL_DEBUG,
    MC_LOG_LVL_INFO,
    MC_LOG_LVL_WARN,
    MC_LOG_LVL_ERROR
} mc_log_level_t;

void mc_log_init(mc_log_level_t level, const char *logfile);
void mc_log_shutdown(void);
void mc_log_write(mc_log_level_t level, const char *file, int line,
                  const char *fmt, ...);

#define MC_LOG_DEBUG(...) mc_log_write(MC_LOG_LVL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define MC_LOG_INFO(...)  mc_log_write(MC_LOG_LVL_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define MC_LOG_WARN(...)  mc_log_write(MC_LOG_LVL_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define MC_LOG_ERROR(...) mc_log_write(MC_LOG_LVL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
