#ifndef MC_CONFIG_H
#define MC_CONFIG_H

#include "mc_models.h"

#define MC_MAX_PATH      512
#define MC_MAX_HEADER    128
#define MC_MAX_PARAMS    512
#define MC_MAX_SYMBOLS   32
#define MC_MAX_TAB_NAME  32
#define MC_MAX_TABS      10

typedef struct {
    char          name[MC_MAX_SOURCE];
    char          url[MC_MAX_URL];
    mc_category_t category;
    int           refresh_interval_sec;
    int           tier;              /* 1=high, 2=mid, 3=low (default) */
} mc_rss_source_cfg_t;

typedef struct {
    char          name[MC_MAX_SOURCE];
    char          base_url[MC_MAX_URL];
    char          endpoint[MC_MAX_URL];
    char          method[8];            /* GET or POST */
    mc_category_t category;
    char          api_key_header[MC_MAX_HEADER];
    char          api_key[MC_MAX_HEADER];
    char          params[MC_MAX_PARAMS];
    char          symbols[MC_MAX_SYMBOLS][MC_MAX_SYMBOL];
    int           symbol_count;
    int           refresh_interval_sec;
    char          response_format[32];  /* json_object, json_array */

    /* Generic field mapping (JSONPath-like, simple dot notation) */
    char          field_symbol[64];     /* e.g. "symbol" or "s" */
    char          field_price[64];      /* e.g. "lastPrice" or "price" */
    char          field_change[64];     /* e.g. "priceChangePercent" */
    char          field_volume[64];     /* e.g. "volume" */
    char          field_name[64];       /* e.g. "name" for display */
    char          field_prev_close[64]; /* e.g. "chartPreviousClose" - auto-compute change */
    char          data_path[64];        /* e.g. "data.items" - path to array */
    char          post_body[MC_MAX_PARAMS]; /* JSON body for POST requests */
    char          currency[MC_MAX_SYMBOL]; /* base currency for forex, e.g. "USD" */
} mc_rest_source_cfg_t;

typedef struct {
    char          name[MC_MAX_SOURCE];
    char          url[MC_MAX_URL];
    mc_category_t category;
    char          subscribe_message[MC_MAX_PARAMS];
    int           reconnect_interval_sec;
} mc_ws_source_cfg_t;

typedef struct {
    /* General */
    int  refresh_interval_sec;
    char db_path[MC_MAX_PATH];
    char log_level[16];
    int  max_items_per_source;

    /* API */
    int  http_port;
    char unix_socket_path[MC_MAX_PATH];

    /* UI hints */
    int  default_tab;
    int  show_borders;
    char tab_names[MC_MAX_TABS][MC_MAX_TAB_NAME];
    int  tab_count;

    /* Sources */
    mc_rss_source_cfg_t  rss_sources[MC_MAX_SOURCES];
    int                  rss_count;
    mc_rest_source_cfg_t rest_sources[MC_MAX_SOURCES];
    int                  rest_count;
    mc_ws_source_cfg_t   ws_sources[MC_MAX_SOURCES];
    int                  ws_count;
} mc_config_t;

int  mc_config_load(const char *path, mc_config_t *cfg);
void mc_config_defaults(mc_config_t *cfg);

#endif
