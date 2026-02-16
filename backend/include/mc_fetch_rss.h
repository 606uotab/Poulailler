#ifndef MC_FETCH_RSS_H
#define MC_FETCH_RSS_H

#include "mc_config.h"
#include "mc_models.h"

int mc_fetch_rss(const mc_rss_source_cfg_t *cfg,
                 mc_news_item_t *items_out,
                 int max_items);

#endif
