#include "mc_fetch_rest.h"
#include "mc_log.h"

#include <curl/curl.h>
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

typedef struct {
    char  *data;
    size_t size;
} mem_buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    mem_buf_t *buf = userdata;
    size_t total = size * nmemb;
    char *tmp = realloc(buf->data, buf->size + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void parse_binance_ticker(cJSON *item, mc_data_entry_t *e,
                                  const char *source_name)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->source_name, source_name, MC_MAX_SOURCE - 1);
    e->source_type = MC_SOURCE_REST;
    e->category = MC_CAT_CRYPTO;

    cJSON *sym = cJSON_GetObjectItem(item, "symbol");
    if (sym && sym->valuestring)
        strncpy(e->symbol, sym->valuestring, MC_MAX_SYMBOL - 1);

    cJSON *price = cJSON_GetObjectItem(item, "lastPrice");
    if (price && price->valuestring)
        e->value = atof(price->valuestring);

    cJSON *change = cJSON_GetObjectItem(item, "priceChangePercent");
    if (change && change->valuestring)
        e->change_pct = atof(change->valuestring);

    cJSON *vol = cJSON_GetObjectItem(item, "volume");
    if (vol && vol->valuestring)
        e->volume = atof(vol->valuestring);

    strncpy(e->currency, "USDT", MC_MAX_SYMBOL - 1);
    snprintf(e->display_name, MC_MAX_NAME, "%s", e->symbol);

    e->timestamp = time(NULL);
    e->fetched_at = time(NULL);
}

static int parse_binance_response(const char *json, const char *source_name,
                                   const mc_rest_source_cfg_t *cfg,
                                   mc_data_entry_t *out, int max_entries)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;

    int count = 0;

    if (cJSON_IsArray(root)) {
        int n = cJSON_GetArraySize(root);
        for (int i = 0; i < n && count < max_entries; i++) {
            cJSON *item = cJSON_GetArrayItem(root, i);

            /* Filter by symbols if configured */
            if (cfg->symbol_count > 0) {
                cJSON *sym = cJSON_GetObjectItem(item, "symbol");
                if (!sym || !sym->valuestring) continue;

                int found = 0;
                for (int j = 0; j < cfg->symbol_count; j++) {
                    if (strcmp(cfg->symbols[j], sym->valuestring) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) continue;
            }

            parse_binance_ticker(item, &out[count], source_name);
            count++;
        }
    }

    cJSON_Delete(root);
    return count;
}

static int parse_coingecko_response(const char *json, const char *source_name,
                                     mc_data_entry_t *out, int max_entries)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;

    int count = 0;
    cJSON *coin = NULL;
    cJSON_ArrayForEach(coin, root) {
        if (count >= max_entries) break;
        if (!coin->string) continue;

        mc_data_entry_t *e = &out[count];
        memset(e, 0, sizeof(*e));

        strncpy(e->source_name, source_name, MC_MAX_SOURCE - 1);
        e->source_type = MC_SOURCE_REST;
        e->category = MC_CAT_CRYPTO;
        strncpy(e->symbol, coin->string, MC_MAX_SYMBOL - 1);
        snprintf(e->display_name, MC_MAX_NAME, "%s", coin->string);
        strncpy(e->currency, "USD", MC_MAX_SYMBOL - 1);

        cJSON *usd = cJSON_GetObjectItem(coin, "usd");
        if (usd) e->value = usd->valuedouble;

        cJSON *change = cJSON_GetObjectItem(coin, "usd_24h_change");
        if (change) e->change_pct = change->valuedouble;

        e->volume = NAN;
        e->timestamp = time(NULL);
        e->fetched_at = time(NULL);
        count++;
    }

    cJSON_Delete(root);
    return count;
}

int mc_fetch_rest(const mc_rest_source_cfg_t *cfg,
                  mc_data_entry_t *entries_out, int max_entries)
{
    MC_LOG_DEBUG("Fetching REST: %s", cfg->name);

    /* Build URL */
    char url[1024];
    if (cfg->params[0])
        snprintf(url, sizeof(url), "%s%s?%s", cfg->base_url, cfg->endpoint, cfg->params);
    else
        snprintf(url, sizeof(url), "%s%s", cfg->base_url, cfg->endpoint);

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    mem_buf_t buf = {0};
    struct curl_slist *headers = NULL;

    /* Set API key header if configured */
    if (cfg->api_key_header[0] && cfg->api_key[0]) {
        char hdr[256];
        snprintf(hdr, sizeof(hdr), "%s: %s", cfg->api_key_header, cfg->api_key);
        headers = curl_slist_append(headers, hdr);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MonitorCrebirth/0.1");
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        MC_LOG_ERROR("REST fetch failed for %s: %s", cfg->name,
                     curl_easy_strerror(res));
        free(buf.data);
        return -1;
    }

    /* Route to the correct parser based on source name */
    int count = 0;
    if (strstr(cfg->name, "Binance") || strstr(cfg->name, "binance")) {
        count = parse_binance_response(buf.data, cfg->name, cfg, entries_out, max_entries);
    } else if (strstr(cfg->name, "CoinGecko") || strstr(cfg->name, "coingecko")) {
        count = parse_coingecko_response(buf.data, cfg->name, entries_out, max_entries);
    } else {
        /* Generic: try to parse as array of objects with "price"/"value" field */
        cJSON *root = cJSON_Parse(buf.data);
        if (root && cJSON_IsArray(root)) {
            count = parse_binance_response(buf.data, cfg->name, cfg,
                                            entries_out, max_entries);
        }
        if (root) cJSON_Delete(root);
    }

    free(buf.data);
    MC_LOG_INFO("REST %s: got %d entries", cfg->name, count);
    return count;
}
