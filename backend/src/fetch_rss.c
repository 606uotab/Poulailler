#define _GNU_SOURCE
#include "mc_fetch_rss.h"
#include "mc_log.h"

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

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

static void strip_html(char *dst, const char *src, size_t max)
{
    size_t j = 0;
    int in_tag = 0;
    for (size_t i = 0; src[i] && j < max - 1; i++) {
        if (src[i] == '<') { in_tag = 1; continue; }
        if (src[i] == '>') { in_tag = 0; continue; }
        if (!in_tag) dst[j++] = src[i];
    }
    dst[j] = '\0';
}

static time_t parse_rfc822(const char *s)
{
    if (!s) return 0;
    struct tm tm = {0};
    /* Try RFC 822: "Mon, 01 Jan 2024 12:00:00 GMT" */
    if (strptime(s, "%a, %d %b %Y %H:%M:%S", &tm))
        return mktime(&tm);
    /* Try ISO 8601: "2024-01-01T12:00:00Z" */
    if (strptime(s, "%Y-%m-%dT%H:%M:%S", &tm))
        return mktime(&tm);
    return 0;
}

static const char *xml_node_content(xmlNodePtr node)
{
    if (!node || !node->children) return NULL;
    return (const char *)node->children->content;
}

static int parse_feed(const char *xml_data, size_t xml_len,
                      const mc_rss_source_cfg_t *cfg,
                      mc_news_item_t *items, int max_items)
{
    xmlDocPtr doc = xmlReadMemory(xml_data, (int)xml_len, "feed.xml", NULL,
                                  XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) {
        MC_LOG_ERROR("Failed to parse XML from %s", cfg->name);
        return 0;
    }

    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    if (!ctx) { xmlFreeDoc(doc); return 0; }

    int count = 0;

    /* Try RSS 2.0 first: //item */
    xmlXPathObjectPtr result = xmlXPathEvalExpression(
        (const xmlChar *)"//item", ctx);

    if (!result || !result->nodesetval || result->nodesetval->nodeNr == 0) {
        if (result) xmlXPathFreeObject(result);
        /* Try Atom: //entry */
        result = xmlXPathEvalExpression(
            (const xmlChar *)"//entry", ctx);
    }

    if (!result || !result->nodesetval) {
        xmlXPathFreeContext(ctx);
        xmlFreeDoc(doc);
        return 0;
    }

    time_t now = time(NULL);
    xmlNodeSetPtr nodes = result->nodesetval;

    for (int i = 0; i < nodes->nodeNr && count < max_items; i++) {
        xmlNodePtr item_node = nodes->nodeTab[i];
        mc_news_item_t *n = &items[count];
        memset(n, 0, sizeof(*n));

        strncpy(n->source, cfg->name, MC_MAX_SOURCE - 1);
        n->category = cfg->category;
        n->fetched_at = now;
        n->score = (cfg->tier == 1) ? 100.0 : (cfg->tier == 2) ? 75.0 : 50.0;

        strncpy(n->region, cfg->region, MC_MAX_REGION - 1);
        strncpy(n->country, cfg->country, MC_MAX_COUNTRY - 1);

        for (xmlNodePtr child = item_node->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;
            const char *name = (const char *)child->name;
            const char *text = xml_node_content(child);
            if (!text) continue;

            if (strcmp(name, "title") == 0) {
                strncpy(n->title, text, MC_MAX_TITLE - 1);
            } else if (strcmp(name, "link") == 0) {
                /* Atom uses href attribute */
                xmlChar *href = xmlGetProp(child, (const xmlChar *)"href");
                if (href) {
                    strncpy(n->url, (const char *)href, MC_MAX_URL - 1);
                    xmlFree(href);
                } else {
                    strncpy(n->url, text, MC_MAX_URL - 1);
                }
            } else if (strcmp(name, "description") == 0 ||
                       strcmp(name, "summary") == 0 ||
                       strcmp(name, "content") == 0) {
                strip_html(n->summary, text, MC_MAX_SUMMARY);
            } else if (strcmp(name, "pubDate") == 0 ||
                       strcmp(name, "published") == 0 ||
                       strcmp(name, "updated") == 0) {
                n->published_at = parse_rfc822(text);
            }
        }

        if (n->title[0]) count++;
    }

    xmlXPathFreeObject(result);
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    return count;
}

int mc_fetch_rss(const mc_rss_source_cfg_t *cfg,
                 mc_news_item_t *items_out, int max_items)
{
    MC_LOG_DEBUG("Fetching RSS: %s (%s)", cfg->name, cfg->url);

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    mem_buf_t buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, cfg->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MonitorCrebirth/0.1");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        MC_LOG_ERROR("RSS fetch failed for %s: %s", cfg->name,
                     curl_easy_strerror(res));
        free(buf.data);
        return -1;
    }

    int count = parse_feed(buf.data, buf.size, cfg, items_out, max_items);
    free(buf.data);

    MC_LOG_INFO("RSS %s: got %d items", cfg->name, count);
    return count;
}
