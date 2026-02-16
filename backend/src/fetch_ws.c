#include "mc_fetch_ws.h"
#include "mc_log.h"
#include "mc_models.h"

#include <libwebsockets.h>
#include <cJSON.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

struct mc_ws_conn {
    mc_ws_source_cfg_t  cfg;
    mc_db_t            *db;
    pthread_t           thread;
    volatile int        running;
    volatile int        connected;
    struct lws_context *lws_ctx;
};

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                       void *user, void *in, size_t len)
{
    mc_ws_conn_t *conn = (mc_ws_conn_t *)lws_context_user(lws_get_context(wsi));
    if (!conn) return 0;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        MC_LOG_INFO("WS connected: %s", conn->cfg.name);
        conn->connected = 1;

        /* Send subscribe message if configured */
        if (conn->cfg.subscribe_message[0]) {
            size_t msglen = strlen(conn->cfg.subscribe_message);
            unsigned char *buf = malloc(LWS_PRE + msglen);
            if (buf) {
                memcpy(buf + LWS_PRE, conn->cfg.subscribe_message, msglen);
                lws_write(wsi, buf + LWS_PRE, msglen, LWS_WRITE_TEXT);
                free(buf);
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        if (!in || len == 0) break;

        /* Parse incoming JSON message */
        char *msg = strndup((const char *)in, len);
        if (!msg) break;

        cJSON *root = cJSON_Parse(msg);
        free(msg);
        if (!root) break;

        /* Try to extract ticker-like data (Binance WS format) */
        mc_data_entry_t entry = {0};
        strncpy(entry.source_name, conn->cfg.name, MC_MAX_SOURCE - 1);
        entry.source_type = MC_SOURCE_WEBSOCKET;
        entry.category = conn->cfg.category;

        cJSON *sym = cJSON_GetObjectItem(root, "s");
        if (sym && sym->valuestring)
            strncpy(entry.symbol, sym->valuestring, MC_MAX_SYMBOL - 1);

        cJSON *price = cJSON_GetObjectItem(root, "c"); /* last price */
        if (!price) price = cJSON_GetObjectItem(root, "p");
        if (price && price->valuestring)
            entry.value = atof(price->valuestring);

        cJSON *change = cJSON_GetObjectItem(root, "P"); /* price change % */
        if (change && change->valuestring)
            entry.change_pct = atof(change->valuestring);

        cJSON *vol = cJSON_GetObjectItem(root, "v");
        if (vol && vol->valuestring)
            entry.volume = atof(vol->valuestring);

        strncpy(entry.currency, "USDT", MC_MAX_SYMBOL - 1);
        entry.timestamp = time(NULL);
        entry.fetched_at = time(NULL);

        if (entry.symbol[0] && entry.value > 0)
            mc_db_insert_entry(conn->db, &entry);

        cJSON_Delete(root);
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        MC_LOG_WARN("WS connection error: %s", conn->cfg.name);
        conn->connected = 0;
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        MC_LOG_INFO("WS disconnected: %s", conn->cfg.name);
        conn->connected = 0;
        break;

    default:
        break;
    }

    return 0;
}

static const struct lws_protocols protocols[] = {
    { "mc-ws", ws_callback, 0, 4096 },
    { NULL, NULL, 0, 0 }
};

static void *ws_thread_func(void *arg)
{
    mc_ws_conn_t *conn = arg;

    while (conn->running) {
        struct lws_context_creation_info info = {0};
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.protocols = protocols;
        info.user = conn;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

        conn->lws_ctx = lws_create_context(&info);
        if (!conn->lws_ctx) {
            MC_LOG_ERROR("WS: failed to create context for %s", conn->cfg.name);
            sleep(conn->cfg.reconnect_interval_sec);
            continue;
        }

        /* Parse URL to extract host, path, port, ssl */
        const char *url = conn->cfg.url;
        int ssl = 0;
        const char *host_start;
        if (strncmp(url, "wss://", 6) == 0) {
            ssl = LCCSCF_USE_SSL;
            host_start = url + 6;
        } else if (strncmp(url, "ws://", 5) == 0) {
            host_start = url + 5;
        } else {
            host_start = url;
        }

        char host[256] = {0};
        char path[512] = "/";
        int port = ssl ? 443 : 80;

        const char *port_start = strchr(host_start, ':');
        const char *path_start = strchr(host_start, '/');

        if (port_start && (!path_start || port_start < path_start)) {
            strncpy(host, host_start, (size_t)(port_start - host_start));
            port = atoi(port_start + 1);
            if (path_start)
                strncpy(path, path_start, sizeof(path) - 1);
        } else if (path_start) {
            strncpy(host, host_start, (size_t)(path_start - host_start));
            strncpy(path, path_start, sizeof(path) - 1);
        } else {
            strncpy(host, host_start, sizeof(host) - 1);
        }

        struct lws_client_connect_info cci = {0};
        cci.context = conn->lws_ctx;
        cci.address = host;
        cci.port = port;
        cci.path = path;
        cci.host = host;
        cci.origin = host;
        cci.protocol = protocols[0].name;
        cci.ssl_connection = ssl;

        MC_LOG_INFO("WS connecting: %s -> %s:%d%s", conn->cfg.name, host, port, path);

        struct lws *wsi = lws_client_connect_via_info(&cci);
        if (!wsi) {
            MC_LOG_ERROR("WS: failed to connect %s", conn->cfg.name);
            lws_context_destroy(conn->lws_ctx);
            conn->lws_ctx = NULL;
            sleep(conn->cfg.reconnect_interval_sec);
            continue;
        }

        /* Service loop */
        while (conn->running && conn->lws_ctx) {
            int n = lws_service(conn->lws_ctx, 100);
            if (n < 0) break;
            if (!conn->connected && !conn->running) break;
        }

        lws_context_destroy(conn->lws_ctx);
        conn->lws_ctx = NULL;
        conn->connected = 0;

        if (conn->running) {
            MC_LOG_INFO("WS reconnecting %s in %ds", conn->cfg.name,
                        conn->cfg.reconnect_interval_sec);
            sleep(conn->cfg.reconnect_interval_sec);
        }
    }

    return NULL;
}

mc_ws_conn_t *mc_ws_connect(const mc_ws_source_cfg_t *cfg, mc_db_t *db)
{
    mc_ws_conn_t *conn = calloc(1, sizeof(*conn));
    if (!conn) return NULL;

    conn->cfg = *cfg;
    conn->db = db;
    conn->running = 1;

    if (pthread_create(&conn->thread, NULL, ws_thread_func, conn) != 0) {
        MC_LOG_ERROR("Failed to create WS thread for %s", cfg->name);
        free(conn);
        return NULL;
    }

    return conn;
}

void mc_ws_disconnect(mc_ws_conn_t *conn)
{
    if (!conn) return;
    conn->running = 0;
    pthread_join(conn->thread, NULL);
    free(conn);
}

int mc_ws_is_connected(mc_ws_conn_t *conn)
{
    return conn ? conn->connected : 0;
}
