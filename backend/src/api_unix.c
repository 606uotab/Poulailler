#include "mc_api_unix.h"
#include "mc_log.h"
#include "mc_models.h"

#include <cJSON.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#define UNIX_BUF_SIZE 65536

struct mc_api_unix {
    char            socket_path[512];
    int             listen_fd;
    pthread_t       thread;
    volatile int    running;
    mc_scheduler_t *sched;
    mc_db_t        *db;
    time_t          started_at;
};

static cJSON *entry_to_json(const mc_data_entry_t *e)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)e->id);
    cJSON_AddStringToObject(obj, "source", e->source_name);
    cJSON_AddStringToObject(obj, "source_type", mc_source_type_str(e->source_type));
    cJSON_AddStringToObject(obj, "category", mc_category_str(e->category));
    cJSON_AddStringToObject(obj, "symbol", e->symbol);
    cJSON_AddNumberToObject(obj, "value", e->value);
    cJSON_AddStringToObject(obj, "currency", e->currency);
    cJSON_AddNumberToObject(obj, "change_pct", e->change_pct);
    cJSON_AddNumberToObject(obj, "volume", e->volume);
    cJSON_AddNumberToObject(obj, "timestamp", (double)e->timestamp);
    return obj;
}

static cJSON *news_to_json(const mc_news_item_t *n)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)n->id);
    cJSON_AddStringToObject(obj, "title", n->title);
    cJSON_AddStringToObject(obj, "source", n->source);
    cJSON_AddStringToObject(obj, "url", n->url);
    cJSON_AddStringToObject(obj, "category", mc_category_str(n->category));
    cJSON_AddNumberToObject(obj, "published_at", (double)n->published_at);
    return obj;
}

static cJSON *handle_request(mc_api_unix_t *api, cJSON *req)
{
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(req, "path"));
    if (!path) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "missing path");
        return err;
    }

    if (strcmp(path, "/api/v1/entries") == 0) {
        mc_data_entry_t *entries = malloc(2048 * sizeof(mc_data_entry_t));
        if (!entries) return NULL;
        int n = mc_scheduler_get_entries(api->sched, entries, 2048);

        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < n; i++)
            cJSON_AddItemToArray(arr, entry_to_json(&entries[i]));

        free(entries);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "data", arr);
        cJSON_AddNumberToObject(root, "count", n);
        return root;
    }

    if (strcmp(path, "/api/v1/news") == 0) {
        mc_news_item_t news[256];
        int n = mc_scheduler_get_news(api->sched, news, 256);

        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < n; i++)
            cJSON_AddItemToArray(arr, news_to_json(&news[i]));

        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "data", arr);
        cJSON_AddNumberToObject(root, "count", n);
        return root;
    }

    if (strcmp(path, "/api/v1/status") == 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "running");
        cJSON_AddNumberToObject(root, "uptime_sec",
                                (double)(time(NULL) - api->started_at));
        cJSON_AddNumberToObject(root, "entries_count",
                                mc_db_count_entries(api->db));
        cJSON_AddNumberToObject(root, "news_count",
                                mc_db_count_news(api->db));
        return root;
    }

    if (strcmp(path, "/api/v1/refresh") == 0) {
        mc_scheduler_force_refresh(api->sched);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "refresh_triggered");
        return root;
    }

    cJSON *err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "not_found");
    return err;
}

static void handle_client(mc_api_unix_t *api, int client_fd)
{
    char buf[UNIX_BUF_SIZE];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    cJSON *req = cJSON_Parse(buf);
    if (!req) {
        const char *err = "{\"error\":\"invalid JSON\"}\n";
        write(client_fd, err, strlen(err));
        return;
    }

    cJSON *resp = handle_request(api, req);
    cJSON_Delete(req);

    char *out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (out) {
        write(client_fd, out, strlen(out));
        write(client_fd, "\n", 1);
        free(out);
    }
}

static void *unix_thread_func(void *arg)
{
    mc_api_unix_t *api = arg;

    while (api->running) {
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(api->listen_fd, &fds);

        int ret = select(api->listen_fd + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        int client_fd = accept(api->listen_fd, NULL, NULL);
        if (client_fd < 0) continue;

        /* Set read timeout on client */
        struct timeval client_tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &client_tv, sizeof(client_tv));

        handle_client(api, client_fd);
        close(client_fd);
    }

    return NULL;
}

mc_api_unix_t *mc_api_unix_start(const char *socket_path,
                                  mc_scheduler_t *sched, mc_db_t *db)
{
    mc_api_unix_t *api = calloc(1, sizeof(*api));
    if (!api) return NULL;

    strncpy(api->socket_path, socket_path, sizeof(api->socket_path) - 1);
    api->sched = sched;
    api->db = db;
    api->started_at = time(NULL);

    /* Remove stale socket file */
    unlink(socket_path);

    api->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (api->listen_fd < 0) {
        MC_LOG_ERROR("Failed to create unix socket: %s", strerror(errno));
        free(api);
        return NULL;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(api->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MC_LOG_ERROR("Failed to bind unix socket %s: %s",
                     socket_path, strerror(errno));
        close(api->listen_fd);
        free(api);
        return NULL;
    }

    if (listen(api->listen_fd, 5) < 0) {
        MC_LOG_ERROR("Failed to listen on unix socket: %s", strerror(errno));
        close(api->listen_fd);
        unlink(socket_path);
        free(api);
        return NULL;
    }

    api->running = 1;
    if (pthread_create(&api->thread, NULL, unix_thread_func, api) != 0) {
        MC_LOG_ERROR("Failed to create unix socket thread");
        close(api->listen_fd);
        unlink(socket_path);
        free(api);
        return NULL;
    }

    MC_LOG_INFO("Unix socket API listening on %s", socket_path);
    return api;
}

void mc_api_unix_stop(mc_api_unix_t *api)
{
    if (!api) return;
    api->running = 0;
    pthread_join(api->thread, NULL);
    close(api->listen_fd);
    unlink(api->socket_path);
    free(api);
    MC_LOG_INFO("Unix socket API stopped");
}
