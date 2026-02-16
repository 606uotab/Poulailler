#ifndef MC_SCHEDULER_H
#define MC_SCHEDULER_H

#include "mc_config.h"
#include "mc_db.h"
#include "mc_models.h"

typedef struct mc_scheduler mc_scheduler_t;

mc_scheduler_t *mc_scheduler_create(const mc_config_t *cfg, mc_db_t *db);
int             mc_scheduler_start(mc_scheduler_t *sched);
void            mc_scheduler_stop(mc_scheduler_t *sched);
void            mc_scheduler_destroy(mc_scheduler_t *sched);
void            mc_scheduler_force_refresh(mc_scheduler_t *sched);

/* Thread-safe snapshot access for API layer */
int mc_scheduler_get_entries(mc_scheduler_t *sched,
                             mc_data_entry_t *out, int max_count);
int mc_scheduler_get_news(mc_scheduler_t *sched,
                          mc_news_item_t *out, int max_count);

#endif
