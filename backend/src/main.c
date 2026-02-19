#include "mc_config.h"
#include "mc_db.h"
#include "mc_scheduler.h"
#include "mc_api_http.h"
#include "mc_api_unix.h"
#include "mc_log.h"
#include "mc_error.h"

#include <curl/curl.h>
#include <libxml/parser.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "MonitorCrebirth Daemon v0.1.0\n"
        "Usage: %s [OPTIONS]\n\n"
        "Options:\n"
        "  --config PATH   Config file (default: ~/.monitorcrebirth/config.toml)\n"
        "  --port PORT     HTTP API port (overrides config)\n"
        "  --no-http       Disable HTTP API\n"
        "  --no-unix       Disable Unix socket API\n"
        "  --version       Print version and exit\n"
        "  --help          Print this help\n",
        prog);
}

static void ensure_dir(const char *path)
{
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);

    /* Find last / and create parent dir */
    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        mkdir(tmp, 0755);
    }
}

static mc_log_level_t parse_log_level(const char *s)
{
    if (strcmp(s, "debug") == 0) return MC_LOG_LVL_DEBUG;
    if (strcmp(s, "warn") == 0)  return MC_LOG_LVL_WARN;
    if (strcmp(s, "error") == 0) return MC_LOG_LVL_ERROR;
    return MC_LOG_LVL_INFO;
}

int main(int argc, char **argv)
{
    const char *config_path = NULL;
    int port_override = 0;
    int no_http = 0;
    int no_unix = 0;

    /* Parse CLI args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port_override = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-http") == 0) {
            no_http = 1;
        } else if (strcmp(argv[i], "--no-unix") == 0) {
            no_unix = 1;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("MonitorCrebirth Daemon v0.1.0\n");
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Default config path */
    char default_config[512];
    if (!config_path) {
        const char *home = getenv("HOME");
        snprintf(default_config, sizeof(default_config),
                 "%s/.monitorcrebirth/config.toml", home ? home : ".");
        config_path = default_config;
    }

    /* Init global libs */
    curl_global_init(CURL_GLOBAL_ALL);
    xmlInitParser();

    /* Load config */
    mc_config_t cfg;
    mc_config_defaults(&cfg);

    if (mc_config_load(config_path, &cfg) != 0) {
        fprintf(stderr, "Warning: Could not load config from %s, using defaults\n",
                config_path);
    }

    if (port_override > 0)
        cfg.http_port = port_override;

    /* Init logging */
    mc_log_init(parse_log_level(cfg.log_level), NULL);

    MC_LOG_INFO("MonitorCrebirth Daemon v0.1.0 starting");
    MC_LOG_INFO("Config: %s", config_path);
    MC_LOG_INFO("Sources: %d RSS, %d REST, %d WebSocket",
                cfg.rss_count, cfg.rest_count, cfg.ws_count);

    /* Ensure data directory exists */
    ensure_dir(cfg.db_path);
    ensure_dir(cfg.unix_socket_path);

    /* Open database */
    mc_db_t *db = mc_db_open(cfg.db_path);
    if (!db) {
        MC_LOG_ERROR("Failed to open database");
        return 1;
    }

    if (mc_db_migrate(db) != MC_OK) {
        MC_LOG_ERROR("Database migration failed");
        mc_db_close(db);
        return 1;
    }

    /* Create and start scheduler */
    mc_scheduler_t *sched = mc_scheduler_create(&cfg, db);
    if (!sched) {
        MC_LOG_ERROR("Failed to create scheduler");
        mc_db_close(db);
        return 1;
    }
    mc_scheduler_start(sched);

    /* Start HTTP API */
    mc_api_http_t *http_api = NULL;
    if (!no_http) {
        http_api = mc_api_http_start(cfg.http_port, sched, db);
        if (!http_api)
            MC_LOG_WARN("HTTP API failed to start, continuing without it");
    }

    /* Start Unix socket API */
    mc_api_unix_t *unix_api = NULL;
    if (!no_unix) {
        unix_api = mc_api_unix_start(cfg.unix_socket_path, sched, db);
        if (!unix_api)
            MC_LOG_WARN("Unix socket API failed to start, continuing without it");
    }

    /* Setup signal handlers (sigaction is thread-safe, signal() is not) */
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    MC_LOG_INFO("Daemon ready. Press Ctrl+C to stop.");

    /* Main loop — just wait for signals */
    while (g_running)
        sleep(1);

    MC_LOG_INFO("Shutting down...");

    /* Cleanup in reverse order */
    if (unix_api) mc_api_unix_stop(unix_api);
    if (http_api) mc_api_http_stop(http_api);
    mc_scheduler_stop(sched);
    mc_scheduler_destroy(sched);
    mc_db_close(db);

    xmlCleanupParser();
    curl_global_cleanup();
    mc_log_shutdown();

    return 0;
}
