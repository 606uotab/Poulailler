#ifndef MC_TUI_PANELS_H
#define MC_TUI_PANELS_H

#include <ncurses.h>
#include "mc_models.h"

void panel_draw_entries(WINDOW *win, mc_data_entry_t *entries, int count,
                        mc_category_t filter, int scroll_pos);
void panel_draw_news(WINDOW *win, mc_news_item_t *news, int count,
                     int scroll_pos);

#endif
