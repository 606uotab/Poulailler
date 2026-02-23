#include "mc_config.h"
#include "mc_log.h"
#include "toml.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void safe_copy(char *dst, const char *src, size_t n)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

void mc_config_defaults(mc_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->refresh_interval_sec = 30;
    safe_copy(cfg->db_path, "~/.monitorcrebirth/monitorcrebirth.db", MC_MAX_PATH);
    safe_copy(cfg->log_level, "info", 16);
    cfg->max_items_per_source = 50;
    cfg->http_port = 8420;
    safe_copy(cfg->unix_socket_path, "~/.monitorcrebirth/mc.sock", MC_MAX_PATH);
    cfg->default_tab = 0;
    cfg->show_borders = 1;
    cfg->tab_count = 5;
    safe_copy(cfg->tab_names[0], "Crypto", MC_MAX_TAB_NAME);
    safe_copy(cfg->tab_names[1], "Indices", MC_MAX_TAB_NAME);
    safe_copy(cfg->tab_names[2], "Commodities", MC_MAX_TAB_NAME);
    safe_copy(cfg->tab_names[3], "News", MC_MAX_TAB_NAME);
    safe_copy(cfg->tab_names[4], "Custom", MC_MAX_TAB_NAME);
}

/* Expand ~ to $HOME */
static void expand_tilde(char *path, size_t n)
{
    if (path[0] != '~') return;
    const char *home = getenv("HOME");
    if (!home) return;

    char tmp[MC_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s%s", home, path + 1);
    strncpy(path, tmp, n - 1);
    path[n - 1] = '\0';
}

static void parse_rss_sources(toml_table_t *source_tbl, mc_config_t *cfg)
{
    toml_array_t *arr = toml_array_in(source_tbl, "rss");
    if (!arr) return;

    int n = toml_array_nelem(arr);
    for (int i = 0; i < n && cfg->rss_count < MC_MAX_SOURCES; i++) {
        toml_table_t *t = toml_table_at(arr, i);
        if (!t) continue;

        mc_rss_source_cfg_t *s = &cfg->rss_sources[cfg->rss_count];
        memset(s, 0, sizeof(*s));

        toml_datum_t d;
        d = toml_string_in(t, "name");
        if (d.ok) { safe_copy(s->name, d.u.s, MC_MAX_SOURCE); free(d.u.s); }

        d = toml_string_in(t, "url");
        if (d.ok) { safe_copy(s->url, d.u.s, MC_MAX_URL); free(d.u.s); }

        d = toml_string_in(t, "category");
        if (d.ok) { s->category = mc_category_from_str(d.u.s); free(d.u.s); }

        d = toml_int_in(t, "refresh_interval_sec");
        s->refresh_interval_sec = d.ok ? (int)d.u.i : cfg->refresh_interval_sec;

        cfg->rss_count++;
    }
}

static void parse_rest_sources(toml_table_t *source_tbl, mc_config_t *cfg)
{
    toml_array_t *arr = toml_array_in(source_tbl, "rest");
    if (!arr) return;

    int n = toml_array_nelem(arr);
    for (int i = 0; i < n && cfg->rest_count < MC_MAX_SOURCES; i++) {
        toml_table_t *t = toml_table_at(arr, i);
        if (!t) continue;

        mc_rest_source_cfg_t *s = &cfg->rest_sources[cfg->rest_count];
        memset(s, 0, sizeof(*s));

        toml_datum_t d;
        d = toml_string_in(t, "name");
        if (d.ok) { safe_copy(s->name, d.u.s, MC_MAX_SOURCE); free(d.u.s); }

        d = toml_string_in(t, "base_url");
        if (d.ok) { safe_copy(s->base_url, d.u.s, MC_MAX_URL); free(d.u.s); }

        d = toml_string_in(t, "endpoint");
        if (d.ok) { safe_copy(s->endpoint, d.u.s, MC_MAX_URL); free(d.u.s); }

        d = toml_string_in(t, "method");
        if (d.ok) { safe_copy(s->method, d.u.s, 8); free(d.u.s); }
        else { safe_copy(s->method, "GET", 8); }

        d = toml_string_in(t, "category");
        if (d.ok) { s->category = mc_category_from_str(d.u.s); free(d.u.s); }

        d = toml_string_in(t, "api_key_header");
        if (d.ok) { safe_copy(s->api_key_header, d.u.s, MC_MAX_HEADER); free(d.u.s); }

        d = toml_string_in(t, "api_key");
        if (d.ok) { safe_copy(s->api_key, d.u.s, MC_MAX_HEADER); free(d.u.s); }

        d = toml_string_in(t, "params");
        if (d.ok) { safe_copy(s->params, d.u.s, MC_MAX_PARAMS); free(d.u.s); }

        d = toml_string_in(t, "response_format");
        if (d.ok) { safe_copy(s->response_format, d.u.s, 32); free(d.u.s); }
        else { safe_copy(s->response_format, "json_object", 32); }

        d = toml_int_in(t, "refresh_interval_sec");
        s->refresh_interval_sec = d.ok ? (int)d.u.i : cfg->refresh_interval_sec;

        /* Parse symbols array */
        toml_array_t *syms = toml_array_in(t, "symbols");
        if (syms) {
            int ns = toml_array_nelem(syms);
            for (int j = 0; j < ns && s->symbol_count < MC_MAX_SYMBOLS; j++) {
                toml_datum_t sd = toml_string_at(syms, j);
                if (sd.ok) {
                    safe_copy(s->symbols[s->symbol_count], sd.u.s, MC_MAX_SYMBOL);
                    s->symbol_count++;
                    free(sd.u.s);
                }
            }
        }

        /* Generic field mapping */
        d = toml_string_in(t, "field_symbol");
        if (d.ok) { safe_copy(s->field_symbol, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "field_price");
        if (d.ok) { safe_copy(s->field_price, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "field_change");
        if (d.ok) { safe_copy(s->field_change, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "field_volume");
        if (d.ok) { safe_copy(s->field_volume, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "field_name");
        if (d.ok) { safe_copy(s->field_name, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "field_prev_close");
        if (d.ok) { safe_copy(s->field_prev_close, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "data_path");
        if (d.ok) { safe_copy(s->data_path, d.u.s, 64); free(d.u.s); }
        d = toml_string_in(t, "post_body");
        if (d.ok) { safe_copy(s->post_body, d.u.s, MC_MAX_PARAMS); free(d.u.s); }

        cfg->rest_count++;
    }
}

static void parse_ws_sources(toml_table_t *source_tbl, mc_config_t *cfg)
{
    toml_array_t *arr = toml_array_in(source_tbl, "websocket");
    if (!arr) return;

    int n = toml_array_nelem(arr);
    for (int i = 0; i < n && cfg->ws_count < MC_MAX_SOURCES; i++) {
        toml_table_t *t = toml_table_at(arr, i);
        if (!t) continue;

        mc_ws_source_cfg_t *s = &cfg->ws_sources[cfg->ws_count];
        memset(s, 0, sizeof(*s));

        toml_datum_t d;
        d = toml_string_in(t, "name");
        if (d.ok) { safe_copy(s->name, d.u.s, MC_MAX_SOURCE); free(d.u.s); }

        d = toml_string_in(t, "url");
        if (d.ok) { safe_copy(s->url, d.u.s, MC_MAX_URL); free(d.u.s); }

        d = toml_string_in(t, "category");
        if (d.ok) { s->category = mc_category_from_str(d.u.s); free(d.u.s); }

        d = toml_string_in(t, "subscribe_message");
        if (d.ok) { safe_copy(s->subscribe_message, d.u.s, MC_MAX_PARAMS); free(d.u.s); }

        d = toml_int_in(t, "reconnect_interval_sec");
        s->reconnect_interval_sec = d.ok ? (int)d.u.i : 5;

        cfg->ws_count++;
    }
}

int mc_config_load(const char *path, mc_config_t *cfg)
{
    mc_config_defaults(cfg);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        MC_LOG_ERROR("Cannot open config: %s", path);
        return -1;
    }

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        MC_LOG_ERROR("TOML parse error: %s", errbuf);
        return -1;
    }

    /* [general] */
    toml_table_t *gen = toml_table_in(root, "general");
    if (gen) {
        toml_datum_t d;
        d = toml_int_in(gen, "refresh_interval_sec");
        if (d.ok) cfg->refresh_interval_sec = (int)d.u.i;

        d = toml_string_in(gen, "db_path");
        if (d.ok) { safe_copy(cfg->db_path, d.u.s, MC_MAX_PATH); free(d.u.s); }

        d = toml_string_in(gen, "log_level");
        if (d.ok) { safe_copy(cfg->log_level, d.u.s, 16); free(d.u.s); }

        d = toml_int_in(gen, "max_items_per_source");
        if (d.ok) cfg->max_items_per_source = (int)d.u.i;
    }

    /* [api] */
    toml_table_t *api = toml_table_in(root, "api");
    if (api) {
        toml_datum_t d;
        d = toml_int_in(api, "http_port");
        if (d.ok) cfg->http_port = (int)d.u.i;

        d = toml_string_in(api, "unix_socket");
        if (d.ok) { safe_copy(cfg->unix_socket_path, d.u.s, MC_MAX_PATH); free(d.u.s); }
    }

    /* [ui] */
    toml_table_t *ui = toml_table_in(root, "ui");
    if (ui) {
        toml_datum_t d;
        d = toml_int_in(ui, "default_tab");
        if (d.ok) cfg->default_tab = (int)d.u.i;

        d = toml_bool_in(ui, "show_borders");
        if (d.ok) cfg->show_borders = d.u.b;

        toml_array_t *tabs = toml_array_in(ui, "tab_names");
        if (tabs) {
            cfg->tab_count = 0;
            int n = toml_array_nelem(tabs);
            for (int i = 0; i < n && i < MC_MAX_TABS; i++) {
                toml_datum_t td = toml_string_at(tabs, i);
                if (td.ok) {
                    safe_copy(cfg->tab_names[cfg->tab_count], td.u.s, MC_MAX_TAB_NAME);
                    cfg->tab_count++;
                    free(td.u.s);
                }
            }
        }
    }

    /* [source] */
    toml_table_t *source = toml_table_in(root, "source");
    if (source) {
        parse_rss_sources(source, cfg);
        parse_rest_sources(source, cfg);
        parse_ws_sources(source, cfg);
    }

    /* Expand tilde in paths */
    expand_tilde(cfg->db_path, MC_MAX_PATH);
    expand_tilde(cfg->unix_socket_path, MC_MAX_PATH);

    toml_free(root);

    MC_LOG_INFO("Config loaded: %d RSS, %d REST, %d WS sources",
                cfg->rss_count, cfg->rest_count, cfg->ws_count);
    return 0;
}
