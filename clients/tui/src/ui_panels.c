#include "ui_panels.h"

#include <string.h>
#include <time.h>
#include <math.h>

/* Color pairs (defined in ui.c) */
#define CP_UP      1
#define CP_DOWN    2
#define CP_HEADER  3
#define CP_ACTIVE  4
#define CP_NORMAL  5

void panel_draw_entries(WINDOW *win, mc_data_entry_t *entries, int count,
                        mc_category_t filter, int scroll_pos)
{
    int h, w;
    getmaxyx(win, h, w);
    werase(win);

    /* Header row */
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, "%-12s %12s %8s %14s  %-10s",
              "Symbol", "Price", "Chg%", "Volume", "Source");
    whline(win, ACS_HLINE, w);
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(win, 1, 0, ACS_HLINE, w);

    int row = 2;
    int displayed = 0;

    for (int i = 0; i < count && row < h; i++) {
        mc_data_entry_t *e = &entries[i];

        if (e->category != filter) continue;
        if (displayed < scroll_pos) { displayed++; continue; }

        int cp = e->change_pct >= 0 ? CP_UP : CP_DOWN;
        const char *arrow = e->change_pct >= 0 ? "+" : "";

        wattron(win, COLOR_PAIR(cp));

        /* Format volume nicely */
        char vol_str[16] = "N/A";
        if (!isnan(e->volume) && e->volume > 0) {
            if (e->volume >= 1e9)
                snprintf(vol_str, sizeof(vol_str), "%.1fB", e->volume / 1e9);
            else if (e->volume >= 1e6)
                snprintf(vol_str, sizeof(vol_str), "%.1fM", e->volume / 1e6);
            else if (e->volume >= 1e3)
                snprintf(vol_str, sizeof(vol_str), "%.1fK", e->volume / 1e3);
            else
                snprintf(vol_str, sizeof(vol_str), "%.0f", e->volume);
        }

        mvwprintw(win, row, 1, "%-12s %12.2f %s%-6.2f%% %14s  %-10s",
                  e->symbol, e->value, arrow, e->change_pct,
                  vol_str, e->source_name);

        wattroff(win, COLOR_PAIR(cp));
        row++;
        displayed++;
    }

    if (row == 2) {
        wattron(win, COLOR_PAIR(CP_NORMAL));
        mvwprintw(win, 3, 2, "No data available. Waiting for fetch...");
        wattroff(win, COLOR_PAIR(CP_NORMAL));
    }

    wrefresh(win);
}

void panel_draw_news(WINDOW *win, mc_news_item_t *news, int count,
                     int scroll_pos)
{
    int h, w;
    getmaxyx(win, h, w);
    werase(win);

    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, "%-20s %-*s %16s",
              "Source", w - 42, "Title", "Published");
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(win, 1, 0, ACS_HLINE, w);

    int row = 2;
    int displayed = 0;

    for (int i = 0; i < count && row < h; i++) {
        if (displayed < scroll_pos) { displayed++; continue; }

        mc_news_item_t *n = &news[i];

        char time_str[20] = "Unknown";
        if (n->published_at > 0) {
            struct tm tm;
            localtime_r(&n->published_at, &tm);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &tm);
        }

        int title_width = w - 42;
        if (title_width < 10) title_width = 10;

        wattron(win, COLOR_PAIR(CP_NORMAL));
        mvwprintw(win, row, 1, "%-20s ", n->source);
        wattroff(win, COLOR_PAIR(CP_NORMAL));

        wattron(win, COLOR_PAIR(CP_ACTIVE));
        wprintw(win, "%-*.*s", title_width, title_width, n->title);
        wattroff(win, COLOR_PAIR(CP_ACTIVE));

        wattron(win, COLOR_PAIR(CP_NORMAL));
        wprintw(win, " %16s", time_str);
        wattroff(win, COLOR_PAIR(CP_NORMAL));

        row++;
        displayed++;
    }

    if (row == 2) {
        wattron(win, COLOR_PAIR(CP_NORMAL));
        mvwprintw(win, 3, 2, "No news available. Waiting for RSS feeds...");
        wattroff(win, COLOR_PAIR(CP_NORMAL));
    }

    wrefresh(win);
}
