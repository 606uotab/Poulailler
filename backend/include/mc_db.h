#ifndef MC_DB_H
#define MC_DB_H

#include "mc_models.h"
#include "mc_error.h"

typedef struct mc_db mc_db_t;

mc_db_t    *mc_db_open(const char *path);
void        mc_db_close(mc_db_t *db);
mc_error_t  mc_db_migrate(mc_db_t *db);

mc_error_t  mc_db_insert_entry(mc_db_t *db, const mc_data_entry_t *entry);
mc_error_t  mc_db_insert_news(mc_db_t *db, const mc_news_item_t *item);

int mc_db_get_latest_entries(mc_db_t *db, mc_category_t cat,
                             mc_data_entry_t *out, int max_count);
int mc_db_get_latest_news(mc_db_t *db, mc_category_t cat,
                          mc_news_item_t *out, int max_count);
int mc_db_get_all_latest_news(mc_db_t *db,
                               mc_news_item_t *out, int max_count);
int mc_db_get_entry_history(mc_db_t *db, const char *symbol,
                            mc_data_entry_t *out, int max_count);

mc_error_t mc_db_update_source_status(mc_db_t *db, const char *source_name,
                                      mc_source_type_t type, const char *error);
mc_error_t mc_db_prune_old(mc_db_t *db, int max_age_sec);

int mc_db_count_entries(mc_db_t *db);
int mc_db_count_news(mc_db_t *db);

/* Source status info */
typedef struct {
    char             source_name[MC_MAX_SOURCE];
    mc_source_type_t source_type;
    time_t           last_fetched;
    char             last_error[256];
    int              error_count;
} mc_source_status_t;

int mc_db_get_source_statuses(mc_db_t *db, mc_source_status_t *out, int max_count);

#endif
