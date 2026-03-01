#include "mc_models.h"
#include <string.h>

const char *mc_source_type_str(mc_source_type_t t)
{
    switch (t) {
    case MC_SOURCE_RSS:       return "rss";
    case MC_SOURCE_REST:      return "rest";
    case MC_SOURCE_WEBSOCKET: return "websocket";
    }
    return "unknown";
}

const char *mc_category_str(mc_category_t c)
{
    switch (c) {
    case MC_CAT_CRYPTO:      return "crypto";
    case MC_CAT_STOCK_INDEX: return "stock_index";
    case MC_CAT_COMMODITY:   return "commodity";
    case MC_CAT_FOREX:       return "forex";
    case MC_CAT_NEWS:        return "news";
    case MC_CAT_CUSTOM:          return "custom";
    case MC_CAT_CRYPTO_EXCHANGE: return "crypto_exchange";
    case MC_CAT_FINANCIAL_NEWS:  return "financial_news";
    case MC_CAT_OFFICIAL_PUB:    return "official_pub";
    }
    return "custom";
}

mc_category_t mc_category_from_str(const char *s)
{
    if (!s) return MC_CAT_CUSTOM;
    if (strcmp(s, "crypto") == 0)           return MC_CAT_CRYPTO;
    if (strcmp(s, "stock_index") == 0)      return MC_CAT_STOCK_INDEX;
    if (strcmp(s, "commodity") == 0)        return MC_CAT_COMMODITY;
    if (strcmp(s, "forex") == 0)            return MC_CAT_FOREX;
    if (strcmp(s, "news") == 0)             return MC_CAT_NEWS;
    if (strcmp(s, "crypto_exchange") == 0)  return MC_CAT_CRYPTO_EXCHANGE;
    if (strcmp(s, "financial_news") == 0)  return MC_CAT_FINANCIAL_NEWS;
    if (strcmp(s, "official_pub") == 0)    return MC_CAT_OFFICIAL_PUB;
    return MC_CAT_CUSTOM;
}
