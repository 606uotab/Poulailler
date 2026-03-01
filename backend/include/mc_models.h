#ifndef MC_MODELS_H
#define MC_MODELS_H

#include <stdint.h>
#include <time.h>

#define MC_MAX_TITLE     256
#define MC_MAX_URL       512
#define MC_MAX_SYMBOL    32
#define MC_MAX_NAME      128
#define MC_MAX_SOURCE    64
#define MC_MAX_CATEGORY  64
#define MC_MAX_SUMMARY   3334
#define MC_MAX_SOURCES   256
#define MC_MAX_REGION    32
#define MC_MAX_COUNTRY   8

typedef enum {
    MC_SOURCE_RSS,
    MC_SOURCE_REST,
    MC_SOURCE_WEBSOCKET
} mc_source_type_t;

typedef enum {
    MC_CAT_CRYPTO,
    MC_CAT_STOCK_INDEX,
    MC_CAT_COMMODITY,
    MC_CAT_FOREX,
    MC_CAT_NEWS,
    MC_CAT_CUSTOM,
    MC_CAT_CRYPTO_EXCHANGE,
    MC_CAT_FINANCIAL_NEWS,
    MC_CAT_OFFICIAL_PUB
} mc_category_t;

typedef struct {
    int64_t          id;
    char             source_name[MC_MAX_SOURCE];
    mc_source_type_t source_type;
    mc_category_t    category;
    char             symbol[MC_MAX_SYMBOL];
    char             display_name[MC_MAX_NAME];
    double           value;
    char             currency[MC_MAX_SYMBOL];
    double           change_pct;
    double           volume;
    time_t           timestamp;
    time_t           fetched_at;
} mc_data_entry_t;

typedef struct {
    int64_t       id;
    char          title[MC_MAX_TITLE];
    char          source[MC_MAX_SOURCE];
    char          url[MC_MAX_URL];
    char          summary[MC_MAX_SUMMARY];
    mc_category_t category;
    time_t        published_at;
    time_t        fetched_at;
    double        score;
    char          region[MC_MAX_REGION];
    char          country[MC_MAX_COUNTRY];
} mc_news_item_t;

const char *mc_source_type_str(mc_source_type_t t);
const char *mc_category_str(mc_category_t c);
mc_category_t mc_category_from_str(const char *s);

#endif
