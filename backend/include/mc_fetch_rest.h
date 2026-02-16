#ifndef MC_FETCH_REST_H
#define MC_FETCH_REST_H

#include "mc_config.h"
#include "mc_models.h"

int mc_fetch_rest(const mc_rest_source_cfg_t *cfg,
                  mc_data_entry_t *entries_out,
                  int max_entries);

#endif
