#include "mc_api_http.h"
#include "mc_log.h"
#include "mc_models.h"

#include <microhttpd.h>
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

struct mc_api_http {
    struct MHD_Daemon *daemon;
    mc_scheduler_t    *sched;
    mc_db_t           *db;
    time_t             started_at;
};

static cJSON *entry_to_json(const mc_data_entry_t *e)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)e->id);
    cJSON_AddStringToObject(obj, "source", e->source_name);
    cJSON_AddStringToObject(obj, "source_type", mc_source_type_str(e->source_type));
    cJSON_AddStringToObject(obj, "category", mc_category_str(e->category));
    cJSON_AddStringToObject(obj, "symbol", e->symbol);
    cJSON_AddStringToObject(obj, "display_name", e->display_name);
    cJSON_AddNumberToObject(obj, "value", e->value);
    cJSON_AddStringToObject(obj, "currency", e->currency);
    cJSON_AddNumberToObject(obj, "change_pct", e->change_pct);
    cJSON_AddNumberToObject(obj, "volume", e->volume);
    cJSON_AddNumberToObject(obj, "timestamp", (double)e->timestamp);
    cJSON_AddNumberToObject(obj, "fetched_at", (double)e->fetched_at);
    return obj;
}

static cJSON *news_to_json(const mc_news_item_t *n)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", (double)n->id);
    cJSON_AddStringToObject(obj, "title", n->title);
    cJSON_AddStringToObject(obj, "source", n->source);
    cJSON_AddStringToObject(obj, "url", n->url);
    cJSON_AddStringToObject(obj, "summary", n->summary);
    cJSON_AddStringToObject(obj, "category", mc_category_str(n->category));
    cJSON_AddNumberToObject(obj, "published_at", (double)n->published_at);
    cJSON_AddNumberToObject(obj, "fetched_at", (double)n->fetched_at);
    return obj;
}

static enum MHD_Result send_json(struct MHD_Connection *conn, int status, cJSON *json)
{
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(body), body, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(resp, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");

    enum MHD_Result ret = MHD_queue_response(conn, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

static enum MHD_Result handle_entries(mc_api_http_t *api,
                                       struct MHD_Connection *conn)
{
    mc_data_entry_t entries[512];
    int n = mc_scheduler_get_entries(api->sched, entries, 512);

    /* Filter by query params */
    const char *cat_filter = MHD_lookup_connection_value(
        conn, MHD_GET_ARGUMENT_KIND, "category");
    const char *sym_filter = MHD_lookup_connection_value(
        conn, MHD_GET_ARGUMENT_KIND, "symbol");

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (cat_filter && strcmp(mc_category_str(entries[i].category), cat_filter) != 0)
            continue;
        if (sym_filter && strstr(entries[i].symbol, sym_filter) == NULL)
            continue;
        cJSON_AddItemToArray(arr, entry_to_json(&entries[i]));
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", arr);
    cJSON_AddNumberToObject(root, "count", cJSON_GetArraySize(arr));

    return send_json(conn, MHD_HTTP_OK, root);
}

static enum MHD_Result handle_news(mc_api_http_t *api,
                                    struct MHD_Connection *conn)
{
    mc_news_item_t news[512];
    int n = mc_scheduler_get_news(api->sched, news, 512);

    const char *cat_filter = MHD_lookup_connection_value(
        conn, MHD_GET_ARGUMENT_KIND, "category");

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (cat_filter && strcmp(mc_category_str(news[i].category), cat_filter) != 0)
            continue;
        cJSON_AddItemToArray(arr, news_to_json(&news[i]));
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", arr);
    cJSON_AddNumberToObject(root, "count", cJSON_GetArraySize(arr));

    return send_json(conn, MHD_HTTP_OK, root);
}

static enum MHD_Result handle_status(mc_api_http_t *api,
                                      struct MHD_Connection *conn)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddStringToObject(root, "version", "0.1.0");
    cJSON_AddNumberToObject(root, "uptime_sec",
                            (double)(time(NULL) - api->started_at));
    cJSON_AddNumberToObject(root, "entries_count",
                            mc_db_count_entries(api->db));
    cJSON_AddNumberToObject(root, "news_count",
                            mc_db_count_news(api->db));

    return send_json(conn, MHD_HTTP_OK, root);
}

static enum MHD_Result handle_refresh(mc_api_http_t *api,
                                       struct MHD_Connection *conn)
{
    mc_scheduler_force_refresh(api->sched);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "refresh_triggered");

    return send_json(conn, MHD_HTTP_OK, root);
}

static enum MHD_Result handle_sources(mc_api_http_t *api,
                                       struct MHD_Connection *conn)
{
    mc_source_status_t statuses[64];
    int n = mc_db_get_source_statuses(api->db, statuses, 64);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", statuses[i].source_name);
        cJSON_AddStringToObject(obj, "type",
            mc_source_type_str(statuses[i].source_type));
        cJSON_AddNumberToObject(obj, "last_fetched",
            (double)statuses[i].last_fetched);

        time_t ago = time(NULL) - statuses[i].last_fetched;
        cJSON_AddNumberToObject(obj, "seconds_ago", (double)ago);

        if (statuses[i].last_error[0])
            cJSON_AddStringToObject(obj, "last_error", statuses[i].last_error);
        else
            cJSON_AddNullToObject(obj, "last_error");

        cJSON_AddNumberToObject(obj, "error_count", statuses[i].error_count);

        const char *health = statuses[i].error_count == 0 ? "healthy" :
                             statuses[i].error_count < 3 ? "degraded" : "failing";
        cJSON_AddStringToObject(obj, "health", health);

        cJSON_AddItemToArray(arr, obj);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "sources", arr);
    cJSON_AddNumberToObject(root, "count", n);

    return send_json(conn, MHD_HTTP_OK, root);
}

static enum MHD_Result handle_history(mc_api_http_t *api,
                                       struct MHD_Connection *conn,
                                       const char *symbol)
{
    mc_data_entry_t entries[100];
    int n = mc_db_get_entry_history(api->db, symbol, entries, 100);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++)
        cJSON_AddItemToArray(arr, entry_to_json(&entries[i]));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "symbol", symbol);
    cJSON_AddItemToObject(root, "data", arr);
    cJSON_AddNumberToObject(root, "count", n);

    return send_json(conn, MHD_HTTP_OK, root);
}

static enum MHD_Result request_handler(void *cls,
                                        struct MHD_Connection *conn,
                                        const char *url,
                                        const char *method,
                                        const char *version,
                                        const char *upload_data,
                                        size_t *upload_data_size,
                                        void **con_cls)
{
    (void)version; (void)upload_data; (void)upload_data_size;
    mc_api_http_t *api = cls;

    /* Handle CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        cJSON *root = cJSON_CreateObject();
        return send_json(conn, MHD_HTTP_OK, root);
    }

    /* First call for this request — set up */
    if (*con_cls == NULL) {
        *con_cls = (void *)1;
        return MHD_YES;
    }

    MC_LOG_DEBUG("HTTP %s %s", method, url);

    /* Route */
    if (strcmp(url, "/api/v1/entries") == 0 && strcmp(method, "GET") == 0)
        return handle_entries(api, conn);

    if (strcmp(url, "/api/v1/news") == 0 && strcmp(method, "GET") == 0)
        return handle_news(api, conn);

    if (strcmp(url, "/api/v1/status") == 0 && strcmp(method, "GET") == 0)
        return handle_status(api, conn);

    if (strcmp(url, "/api/v1/sources") == 0 && strcmp(method, "GET") == 0)
        return handle_sources(api, conn);

    if (strcmp(url, "/api/v1/refresh") == 0 && strcmp(method, "POST") == 0)
        return handle_refresh(api, conn);

    /* /api/v1/entries/<symbol>/history */
    const char *hist_prefix = "/api/v1/entries/";
    const char *hist_suffix = "/history";
    if (strncmp(url, hist_prefix, strlen(hist_prefix)) == 0 &&
        strcmp(method, "GET") == 0) {
        const char *sym_start = url + strlen(hist_prefix);
        const char *sym_end = strstr(sym_start, hist_suffix);
        if (sym_end) {
            char symbol[MC_MAX_SYMBOL] = {0};
            size_t len = (size_t)(sym_end - sym_start);
            if (len >= MC_MAX_SYMBOL) len = MC_MAX_SYMBOL - 1;
            strncpy(symbol, sym_start, len);
            return handle_history(api, conn, symbol);
        }
    }

    /* 404 */
    cJSON *err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "error", "not_found");
    cJSON_AddStringToObject(err, "path", url);
    return send_json(conn, MHD_HTTP_NOT_FOUND, err);
}

mc_api_http_t *mc_api_http_start(int port, mc_scheduler_t *sched, mc_db_t *db)
{
    mc_api_http_t *api = calloc(1, sizeof(*api));
    if (!api) return NULL;

    api->sched = sched;
    api->db = db;
    api->started_at = time(NULL);

    api->daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_AUTO,
        (uint16_t)port,
        NULL, NULL,
        request_handler, api,
        MHD_OPTION_END);

    if (!api->daemon) {
        MC_LOG_ERROR("Failed to start HTTP API on port %d", port);
        free(api);
        return NULL;
    }

    MC_LOG_INFO("HTTP API listening on port %d", port);
    return api;
}

void mc_api_http_stop(mc_api_http_t *api)
{
    if (!api) return;
    MHD_stop_daemon(api->daemon);
    free(api);
    MC_LOG_INFO("HTTP API stopped");
}
