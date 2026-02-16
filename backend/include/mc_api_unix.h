#ifndef MC_API_UNIX_H
#define MC_API_UNIX_H

#include "mc_scheduler.h"
#include "mc_db.h"

typedef struct mc_api_unix mc_api_unix_t;

mc_api_unix_t *mc_api_unix_start(const char *socket_path,
                                  mc_scheduler_t *sched, mc_db_t *db);
void           mc_api_unix_stop(mc_api_unix_t *api);

#endif
