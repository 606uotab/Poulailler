#ifndef MC_TUI_PANELS_H
#define MC_TUI_PANELS_H

#include <ncurses.h>
#include "mc_models.h"

/* Draw entry list with optional search filter and cursor highlight.
 * filter may be NULL or "" for no filtering.
 * cursor_pos is the index (within filtered results) of the selected row.
 * Returns the number of visible (filtered) items. */
int panel_draw_entries(WINDOW *win, mc_data_entry_t *entries, int count,
                       mc_category_t cat_filter, int scroll_pos,
                       const char *search_filter, int cursor_pos);

/* Draw news list with optional search filter and cursor highlight.
 * Returns the number of visible (filtered) items. */
int panel_draw_news(WINDOW *win, mc_news_item_t *news, int count,
                    int scroll_pos, const char *search_filter,
                    int cursor_pos);

/* Draw detail popup for a data entry. */
void panel_draw_detail_entry(WINDOW *win, const mc_data_entry_t *entry);

/* Draw detail popup for a news item. */
void panel_draw_detail_news(WINDOW *win, const mc_news_item_t *news);

/* Draw search bar at the bottom of the given window. */
void panel_draw_search_bar(WINDOW *win, const char *query, int active);

#endif
