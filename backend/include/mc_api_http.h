#ifndef MC_API_HTTP_H
#define MC_API_HTTP_H

#include "mc_scheduler.h"
#include "mc_db.h"

typedef struct mc_api_http mc_api_http_t;

mc_api_http_t *mc_api_http_start(int port, mc_scheduler_t *sched, mc_db_t *db);
void           mc_api_http_stop(mc_api_http_t *api);

#endif
