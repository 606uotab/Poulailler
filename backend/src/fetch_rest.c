#include "mc_fetch_rest.h"
#include "mc_log.h"

#include <curl/curl.h>
#include <cJSON.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* ── Human-readable names for stock index ticker symbols ── */
static const struct { const char *sym; const char *name; } g_index_names[] = {
    /* US */
    {"^GSPC",      "S&P 500"},
    {"^DJI",       "Dow Jones"},
    {"^IXIC",      "NASDAQ"},
    {"^NDX",       "NASDAQ-100"},
    {"^NYA",       "NYSE Comp."},
    {"^RUT",       "Russell 2000"},
    {"^SP400",     "S&P MidCap 400"},
    {"^SP600",     "S&P SmallCap"},
    {"^VIX",       "CBOE VIX"},
    {"^SOX",       "PHLX Semi."},
    /* US Extended */
    {"^OEX",       "S&P 100"},
    {"^RUI",       "Russell 1000"},
    {"^RUA",       "Russell 3000"},
    {"^DJT",       "DJ Transport"},
    {"^DJU",       "DJ Utilities"},
    {"^W1DOW",     "DJ Global"},
    /* Americas */
    {"^GSPTSE",    "S&P/TSX"},
    {"^BVSP",      "Bovespa"},
    {"^MXX",       "IPC Mexico"},
    {"^MERV",      "MERVAL"},
    {"^IPSA",      "IPSA Chile"},
    {"^SPCOSLCP",  "Colombia"},
    /* Europe West */
    {"^FTSE",      "FTSE 100"},
    {"^FTAS",      "FTSE All-Share"},
    {"^FTMC",      "FTSE 250"},
    {"^GDAXI",     "DAX"},
    {"^FCHI",      "CAC 40"},
    {"^STOXX50E",  "Euro Stoxx 50"},
    {"^AEX",       "AEX"},
    {"^IBEX",      "IBEX 35"},
    {"^SSMI",      "SMI"},
    {"FTSEMIB.MI", "FTSE MIB"},
    /* Europe North */
    {"^STOXX",     "STOXX 600"},
    {"^N100",      "Euronext 100"},
    {"^BFX",       "BEL 20"},
    {"PSI20.LS",   "PSI"},
    {"^ISEQ",      "ISEQ"},
    {"^ATX",       "ATX"},
    {"^OMXS30",    "OMX Stockh."},
    {"^OMXC25",    "OMX Copenh."},
    {"^OMXH25",    "OMX Helsinki"},
    {"^OMXN40",    "OMX Nordic"},
    /* Europe East */
    {"XU100.IS",   "BIST 100"},
    {"WIG20.WA",   "WIG 20"},
    {"^BUX.BD",    "BUX"},
    {"FPXAA.PR",   "PX Prague"},
    {"^BET.RO",    "BET"},
    {"GD.AT",      "Athens Gen."},
    /* Europe Extra */
    {"^MDAXI",     "MDAX"},
    {"^TECDAX",    "TecDAX"},
    {"^CN20",      "CAC Next 20"},
    {"OSEBX.OL",   "Oslo Bors"},
    {"^OMXI15",    "OMX Iceland"},
    {"^OMXRGI",    "OMX Riga"},
    {"^OMXVGI",    "OMX Vilnius"},
    /* East Asia */
    {"^N225",      "Nikkei 225"},
    {"^HSI",       "Hang Seng"},
    {"^HSCE",      "HS China Ent."},
    {"HSTECH.HK",  "HS TECH"},
    {"000001.SS",  "Shanghai"},
    {"000300.SS",  "CSI 300"},
    {"399001.SZ",  "Shenzhen"},
    {"399006.SZ",  "ChiNext"},
    {"^KS11",      "KOSPI"},
    {"^TWII",      "TAIEX"},
    {"^KQ11",      "KOSDAQ"},
    /* South & SE Asia */
    {"^BSESN",     "Sensex"},
    {"^NSEI",      "Nifty 50"},
    {"^NSEBANK",   "Nifty Bank"},
    {"^STI",       "STI"},
    {"^JKSE",      "IDX Comp."},
    {"^KLSE",      "KLCI"},
    {"^SET.BK",    "SET"},
    {"PSEI.PS",    "PSEi"},
    /* Oceania */
    {"^AXJO",      "ASX 200"},
    {"^AORD",      "All Ords"},
    {"^NZ50",      "NZX 50"},
    /* Middle East */
    {"^TA125.TA",  "TA-125"},
    {"^TASI.SR",   "Tadawul"},
    {"FADGI.FGI",  "ADX Abu Dhabi"},
    {"DFMGI.AE",   "DFM Dubai"},
    {"^BKA.KW",    "Kuwait"},
    {"^GNRI.QA",   "QE Qatar"},
    /* Africa */
    {"^J203.JO",   "JSE All Share"},
    {"^J200.JO",   "JSE Top 40"},
    {"^CASE30",    "EGX 30"},
    {"^NQMA",      "Morocco"},
    /* Yahoo chart-only indices */
    {"^VNINDEX.VN","VN-Index"},
    {"^SPBLPGPT",  "Peru General"},
    {"^DJBH",      "DJ Bahrain"},
    {"^DWJOD",     "DJ Jordan"},
    {"IMOEX.ME",   "MOEX Russia"},
    {NULL, NULL}
};

static const char *lookup_index_name(const char *symbol)
{
    for (int i = 0; g_index_names[i].sym; i++) {
        if (strcmp(g_index_names[i].sym, symbol) == 0)
            return g_index_names[i].name;
    }
    return NULL;
}

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

/* Navigate a JSON object by dot-separated path, e.g. "data.items" or "chart.result.0.meta"
 * Numeric segments are treated as array indices. */
static cJSON *json_navigate(cJSON *root, const char *path)
{
    if (!path || !path[0]) return root;

    char buf[128];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    cJSON *current = root;
    char *saveptr = NULL;
    char *tok = strtok_r(buf, ".", &saveptr);
    while (tok && current) {
        /* Numeric segment → array index */
        if (tok[0] >= '0' && tok[0] <= '9' && cJSON_IsArray(current))
            current = cJSON_GetArrayItem(current, atoi(tok));
        else
            current = cJSON_GetObjectItemCaseSensitive(current, tok);
        tok = strtok_r(NULL, ".", &saveptr);
    }
    return current;
}

/* Resolve a JSON value by key; supports dot-separated nested paths */
static cJSON *json_resolve(cJSON *obj, const char *key)
{
    if (!key || !key[0]) return NULL;
    if (strchr(key, '.'))
        return json_navigate(obj, key);
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

/* Extract a double from a JSON value (handles number, string, and array[0]) */
static double json_get_double(cJSON *obj, const char *key)
{
    cJSON *v = json_resolve(obj, key);
    if (!v) return NAN;
    if (cJSON_IsNumber(v)) return v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) return atof(v->valuestring);
    if (cJSON_IsArray(v)) {
        cJSON *first = cJSON_GetArrayItem(v, 0);
        if (first && cJSON_IsNumber(first)) return first->valuedouble;
        if (first && cJSON_IsString(first) && first->valuestring)
            return atof(first->valuestring);
    }
    return NAN;
}

static const char *json_get_string(cJSON *obj, const char *key)
{
    cJSON *v = json_resolve(obj, key);
    if (v && cJSON_IsString(v)) return v->valuestring;
    return NULL;
}

static int parse_generic_response(const char *json,
                                   const mc_rest_source_cfg_t *cfg,
                                   mc_data_entry_t *out, int max_entries)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;

    /* Navigate to data array if data_path is specified */
    cJSON *data = root;
    if (cfg->data_path[0])
        data = json_navigate(root, cfg->data_path);

    if (!data) { cJSON_Delete(root); return 0; }

    int count = 0;

    /* If data is an array, iterate items */
    if (cJSON_IsArray(data)) {
        int n = cJSON_GetArraySize(data);
        for (int i = 0; i < n && count < max_entries; i++) {
            cJSON *item = cJSON_GetArrayItem(data, i);
            if (!item) continue;

            mc_data_entry_t *e = &out[count];
            memset(e, 0, sizeof(*e));

            strncpy(e->source_name, cfg->name, MC_MAX_SOURCE - 1);
            e->source_type = MC_SOURCE_REST;
            e->category = cfg->category;

            /* Map fields */
            const char *sym = json_get_string(item,
                cfg->field_symbol[0] ? cfg->field_symbol : "symbol");
            if (sym) strncpy(e->symbol, sym, MC_MAX_SYMBOL - 1);

            const char *name = json_get_string(item,
                cfg->field_name[0] ? cfg->field_name : "name");
            if (name) strncpy(e->display_name, name, MC_MAX_NAME - 1);

            e->value = json_get_double(item,
                cfg->field_price[0] ? cfg->field_price : "price");

            e->change_pct = json_get_double(item,
                cfg->field_change[0] ? cfg->field_change : "change_percent");

            /* Auto-compute change_pct from previous close if configured */
            if (isnan(e->change_pct) && !isnan(e->value) && cfg->field_prev_close[0]) {
                double prev = json_get_double(item, cfg->field_prev_close);
                if (!isnan(prev) && prev > 0)
                    e->change_pct = ((e->value - prev) / prev) * 100.0;
            }

            e->volume = json_get_double(item,
                cfg->field_volume[0] ? cfg->field_volume : "volume");

            strncpy(e->currency, "USD", MC_MAX_SYMBOL - 1);
            e->timestamp = time(NULL);
            e->fetched_at = time(NULL);

            /* Only count if we got at least a symbol or price */
            if ((e->symbol[0] || e->display_name[0]) && !isnan(e->value))
                count++;
        }
    }
    /* If data is an object, check if it's a single flat entry or object-of-objects */
    else if (cJSON_IsObject(data)) {
        const char *price_key = cfg->field_price[0] ? cfg->field_price : "price";
        cJSON *direct_price = cJSON_GetObjectItemCaseSensitive(data, price_key);

        if (direct_price && !cJSON_IsObject(direct_price)) {
            /* Single flat object (e.g., LNmarkets ticker) */
            mc_data_entry_t *e = &out[0];
            memset(e, 0, sizeof(*e));

            strncpy(e->source_name, cfg->name, MC_MAX_SOURCE - 1);
            e->source_type = MC_SOURCE_REST;
            e->category = cfg->category;

            const char *sym = json_get_string(data,
                cfg->field_symbol[0] ? cfg->field_symbol : "symbol");
            if (sym)
                strncpy(e->symbol, sym, MC_MAX_SYMBOL - 1);
            else if (cfg->symbol_count > 0)
                strncpy(e->symbol, cfg->symbols[0], MC_MAX_SYMBOL - 1);
            else
                strncpy(e->symbol, cfg->name, MC_MAX_SYMBOL - 1);

            const char *name = json_get_string(data,
                cfg->field_name[0] ? cfg->field_name : "name");
            if (name) strncpy(e->display_name, name, MC_MAX_NAME - 1);

            e->value = json_get_double(data, price_key);
            e->change_pct = json_get_double(data,
                cfg->field_change[0] ? cfg->field_change : "change_percent");

            /* Auto-compute change_pct from previous close if configured */
            if (isnan(e->change_pct) && !isnan(e->value) && cfg->field_prev_close[0]) {
                double prev = json_get_double(data, cfg->field_prev_close);
                if (!isnan(prev) && prev > 0)
                    e->change_pct = ((e->value - prev) / prev) * 100.0;
            }

            e->volume = json_get_double(data,
                cfg->field_volume[0] ? cfg->field_volume : "volume");

            strncpy(e->currency, "USD", MC_MAX_SYMBOL - 1);
            e->timestamp = time(NULL);
            e->fetched_at = time(NULL);

            if (e->symbol[0] && !isnan(e->value))
                count = 1;
        } else {
            /* Object-of-objects: each key is a symbol (CoinGecko-style) */
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, data) {
                if (count >= max_entries) break;
                if (!item->string) continue;

                mc_data_entry_t *e = &out[count];
                memset(e, 0, sizeof(*e));

                strncpy(e->source_name, cfg->name, MC_MAX_SOURCE - 1);
                e->source_type = MC_SOURCE_REST;
                e->category = cfg->category;
                strncpy(e->symbol, item->string, MC_MAX_SYMBOL - 1);

                if (cJSON_IsObject(item)) {
                    const char *sym = json_get_string(item,
                        cfg->field_symbol[0] ? cfg->field_symbol : NULL);
                    if (sym) strncpy(e->symbol, sym, MC_MAX_SYMBOL - 1);

                    const char *name = json_get_string(item,
                        cfg->field_name[0] ? cfg->field_name : NULL);
                    if (name) strncpy(e->display_name, name, MC_MAX_NAME - 1);

                    e->value = json_get_double(item,
                        cfg->field_price[0] ? cfg->field_price : "usd");
                    e->change_pct = json_get_double(item,
                        cfg->field_change[0] ? cfg->field_change : "usd_24h_change");

                    if (isnan(e->change_pct) && !isnan(e->value) && cfg->field_prev_close[0]) {
                        double prev = json_get_double(item, cfg->field_prev_close);
                        if (!isnan(prev) && prev > 0)
                            e->change_pct = ((e->value - prev) / prev) * 100.0;
                    }

                    e->volume = json_get_double(item,
                        cfg->field_volume[0] ? cfg->field_volume : "usd_24h_vol");
                } else if (cJSON_IsNumber(item)) {
                    e->value = item->valuedouble;
                }

                strncpy(e->currency, "USD", MC_MAX_SYMBOL - 1);
                e->timestamp = time(NULL);
                e->fetched_at = time(NULL);

                if (e->symbol[0] && !isnan(e->value) && e->value != 0)
                    count++;
            }
        }
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

    /* Apply HTTP method from config */
    if (strcasecmp(cfg->method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (cfg->post_body[0]) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, cfg->post_body);
            headers = curl_slist_append(headers, "Content-Type: application/json");
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        }
    }

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

    /* Route to the correct parser */
    int count = 0;

    if (cfg->field_price[0]) {
        /* Generic parser: use field mappings from config */
        count = parse_generic_response(buf.data, cfg, entries_out, max_entries);
    } else if (strstr(cfg->name, "Binance") || strstr(cfg->name, "binance")) {
        count = parse_binance_response(buf.data, cfg->name, cfg, entries_out, max_entries);
    } else if ((strstr(cfg->name, "CoinGecko") || strstr(cfg->name, "coingecko"))
               && strcmp(cfg->response_format, "json_object") == 0) {
        count = parse_coingecko_response(buf.data, cfg->name, entries_out, max_entries);
    } else {
        /* Fallback: try generic with common field names */
        count = parse_generic_response(buf.data, cfg, entries_out, max_entries);
    }

    free(buf.data);

    /* Post-process: fill display_name from lookup table for known indices */
    if (cfg->category == MC_CAT_STOCK_INDEX) {
        for (int i = 0; i < count; i++) {
            if (!entries_out[i].display_name[0]) {
                const char *name = lookup_index_name(entries_out[i].symbol);
                if (name)
                    strncpy(entries_out[i].display_name, name, MC_MAX_NAME - 1);
            }
        }
    }

    MC_LOG_INFO("REST %s: got %d entries", cfg->name, count);
    return count;
}
