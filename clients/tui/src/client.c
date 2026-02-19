#include "client.h"

#include <curl/curl.h>
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

struct mc_client {
    char base_url[256];
};

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

static char *http_get(const char *url)
{
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    mem_buf_t buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

mc_client_t *mc_client_create(const char *host, int port)
{
    mc_client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    snprintf(c->base_url, sizeof(c->base_url), "http://%s:%d", host, port);
    return c;
}

void mc_client_destroy(mc_client_t *c)
{
    free(c);
}

static mc_category_t cat_from_str(const char *s)
{
    if (!s) return MC_CAT_CUSTOM;
    if (strcmp(s, "crypto") == 0)           return MC_CAT_CRYPTO;
    if (strcmp(s, "stock_index") == 0)      return MC_CAT_STOCK_INDEX;
    if (strcmp(s, "commodity") == 0)        return MC_CAT_COMMODITY;
    if (strcmp(s, "forex") == 0)            return MC_CAT_FOREX;
    if (strcmp(s, "news") == 0)             return MC_CAT_NEWS;
    if (strcmp(s, "crypto_exchange") == 0)  return MC_CAT_CRYPTO_EXCHANGE;
    return MC_CAT_CUSTOM;
}

int mc_client_get_entries(mc_client_t *c, mc_data_entry_t *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/entries", c->base_url);

    char *json = http_get(url);
    if (!json) return 0;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return 0;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return 0;
    }

    int count = 0;
    int n = cJSON_GetArraySize(data);
    for (int i = 0; i < n && count < max; i++) {
        cJSON *item = cJSON_GetArrayItem(data, i);
        mc_data_entry_t *e = &out[count];
        memset(e, 0, sizeof(*e));

        cJSON *v;
        v = cJSON_GetObjectItem(item, "id");
        if (v) e->id = (int64_t)v->valuedouble;

        v = cJSON_GetObjectItem(item, "source");
        if (v && v->valuestring) strncpy(e->source_name, v->valuestring, MC_MAX_SOURCE - 1);

        v = cJSON_GetObjectItem(item, "category");
        if (v && v->valuestring) e->category = cat_from_str(v->valuestring);

        v = cJSON_GetObjectItem(item, "symbol");
        if (v && v->valuestring) strncpy(e->symbol, v->valuestring, MC_MAX_SYMBOL - 1);

        v = cJSON_GetObjectItem(item, "display_name");
        if (v && v->valuestring) strncpy(e->display_name, v->valuestring, MC_MAX_NAME - 1);

        v = cJSON_GetObjectItem(item, "value");
        if (v) e->value = v->valuedouble;

        v = cJSON_GetObjectItem(item, "currency");
        if (v && v->valuestring) strncpy(e->currency, v->valuestring, MC_MAX_SYMBOL - 1);

        v = cJSON_GetObjectItem(item, "change_pct");
        if (v) e->change_pct = v->valuedouble;

        v = cJSON_GetObjectItem(item, "volume");
        if (v) e->volume = v->valuedouble;

        v = cJSON_GetObjectItem(item, "timestamp");
        if (v) e->timestamp = (time_t)v->valuedouble;

        v = cJSON_GetObjectItem(item, "fetched_at");
        if (v) e->fetched_at = (time_t)v->valuedouble;

        count++;
    }

    cJSON_Delete(root);
    return count;
}

int mc_client_get_news(mc_client_t *c, mc_news_item_t *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/news", c->base_url);

    char *json = http_get(url);
    if (!json) return 0;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return 0;

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return 0;
    }

    int count = 0;
    int n = cJSON_GetArraySize(data);
    for (int i = 0; i < n && count < max; i++) {
        cJSON *item = cJSON_GetArrayItem(data, i);
        mc_news_item_t *news = &out[count];
        memset(news, 0, sizeof(*news));

        cJSON *v;
        v = cJSON_GetObjectItem(item, "id");
        if (v) news->id = (int64_t)v->valuedouble;

        v = cJSON_GetObjectItem(item, "title");
        if (v && v->valuestring) strncpy(news->title, v->valuestring, MC_MAX_TITLE - 1);

        v = cJSON_GetObjectItem(item, "source");
        if (v && v->valuestring) strncpy(news->source, v->valuestring, MC_MAX_SOURCE - 1);

        v = cJSON_GetObjectItem(item, "url");
        if (v && v->valuestring) strncpy(news->url, v->valuestring, MC_MAX_URL - 1);

        v = cJSON_GetObjectItem(item, "category");
        if (v && v->valuestring) news->category = cat_from_str(v->valuestring);

        v = cJSON_GetObjectItem(item, "summary");
        if (v && v->valuestring) strncpy(news->summary, v->valuestring, MC_MAX_SUMMARY - 1);

        v = cJSON_GetObjectItem(item, "published_at");
        if (v) news->published_at = (time_t)v->valuedouble;

        v = cJSON_GetObjectItem(item, "fetched_at");
        if (v) news->fetched_at = (time_t)v->valuedouble;

        count++;
    }

    cJSON_Delete(root);
    return count;
}

int mc_client_refresh(mc_client_t *c)
{
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v1/refresh", c->base_url);

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    /* Discard response body */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    mem_buf_t buf = {0};
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    free(buf.data);

    return (res == CURLE_OK) ? 0 : -1;
}
