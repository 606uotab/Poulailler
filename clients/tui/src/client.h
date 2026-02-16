#ifndef MC_CLIENT_H
#define MC_CLIENT_H

#include "mc_models.h"

typedef struct mc_client mc_client_t;

mc_client_t *mc_client_create(const char *host, int port);
void         mc_client_destroy(mc_client_t *c);

int mc_client_get_entries(mc_client_t *c, mc_data_entry_t *out, int max);
int mc_client_get_news(mc_client_t *c, mc_news_item_t *out, int max);
int mc_client_refresh(mc_client_t *c);

#endif
