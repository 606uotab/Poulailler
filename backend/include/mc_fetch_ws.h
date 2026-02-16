#ifndef MC_FETCH_WS_H
#define MC_FETCH_WS_H

#include "mc_config.h"
#include "mc_db.h"

typedef struct mc_ws_conn mc_ws_conn_t;

mc_ws_conn_t *mc_ws_connect(const mc_ws_source_cfg_t *cfg, mc_db_t *db);
void          mc_ws_disconnect(mc_ws_conn_t *conn);
int           mc_ws_is_connected(mc_ws_conn_t *conn);

#endif
