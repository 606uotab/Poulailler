#include "ui_panels.h"

#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

/* Color pairs (defined in ui.c) */
#define CP_UP      1
#define CP_DOWN    2
#define CP_HEADER  3
#define CP_ACTIVE  4
#define CP_NORMAL  5
#define CP_DIM     6

/* Unicode block characters for sparklines */
static const char *spark_chars[] = {
    "\u2581", "\u2582", "\u2583", "\u2584",
    "\u2585", "\u2586", "\u2587", "\u2588"
};

static const char *format_volume(double vol, char *buf, size_t sz)
{
    if (isnan(vol) || vol <= 0) { snprintf(buf, sz, "   N/A"); return buf; }
    if (vol >= 1e12) snprintf(buf, sz, "%6.1fT", vol / 1e12);
    else if (vol >= 1e9) snprintf(buf, sz, "%6.1fB", vol / 1e9);
    else if (vol >= 1e6) snprintf(buf, sz, "%6.1fM", vol / 1e6);
    else if (vol >= 1e3) snprintf(buf, sz, "%6.1fK", vol / 1e3);
    else snprintf(buf, sz, "%6.0f", vol);
    return buf;
}

static const char *format_price(double price, char *buf, size_t sz)
{
    if (price >= 10000)     snprintf(buf, sz, "%'12.2f", price);
    else if (price >= 1)    snprintf(buf, sz, "%12.4f", price);
    else if (price >= 0.01) snprintf(buf, sz, "%12.6f", price);
    else                    snprintf(buf, sz, "%12.8f", price);
    return buf;
}

static const char *time_ago(time_t t, char *buf, size_t sz)
{
    if (t == 0) { snprintf(buf, sz, "never"); return buf; }
    time_t diff = time(NULL) - t;
    if (diff < 60)        snprintf(buf, sz, "%lds ago", (long)diff);
    else if (diff < 3600) snprintf(buf, sz, "%ldm ago", (long)(diff / 60));
    else                  snprintf(buf, sz, "%ldh ago", (long)(diff / 3600));
    return buf;
}

void panel_draw_entries(WINDOW *win, mc_data_entry_t *entries, int count,
                        mc_category_t filter, int scroll_pos)
{
    int h, w;
    getmaxyx(win, h, w);
    werase(win);

    /* Header row */
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, " %-14s %12s %8s %8s  %-12s  %s",
              "Symbol", "Price", "Chg%", "Volume", "Source", "Updated");
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

    wattron(win, COLOR_PAIR(CP_DIM));
    mvwhline(win, 1, 0, ACS_HLINE, w);
    wattroff(win, COLOR_PAIR(CP_DIM));

    int row = 2;
    int displayed = 0;
    int filtered_count = 0;

    /* Count filtered items first */
    for (int i = 0; i < count; i++)
        if (entries[i].category == filter) filtered_count++;

    for (int i = 0; i < count && row < h - 1; i++) {
        mc_data_entry_t *e = &entries[i];
        if (e->category != filter) continue;
        if (displayed < scroll_pos) { displayed++; continue; }

        int cp = e->change_pct >= 0 ? CP_UP : CP_DOWN;
        const char *arrow = e->change_pct >= 0 ? "+" : "";
        const char *indicator = e->change_pct >= 0 ? "\u25B2" : "\u25BC";

        char vol_str[16], time_str[16];
        format_volume(e->volume, vol_str, sizeof(vol_str));
        time_ago(e->fetched_at, time_str, sizeof(time_str));

        /* Alternate row background subtly */
        if ((displayed - scroll_pos) % 2 == 1)
            wattron(win, A_DIM);

        wattron(win, COLOR_PAIR(cp));
        mvwprintw(win, row, 1, " %-14s $%11.2f %s%6.2f%% %s %8s  %-12s  %s",
                  e->symbol, e->value, arrow, e->change_pct,
                  indicator, vol_str, e->source_name, time_str);
        wattroff(win, COLOR_PAIR(cp));

        if ((displayed - scroll_pos) % 2 == 1)
            wattroff(win, A_DIM);

        row++;
        displayed++;
    }

    if (filtered_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, 3, 2, "No data available. Waiting for fetch...");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    /* Scroll indicator */
    if (filtered_count > h - 3) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, h - 1, w - 20, "[%d/%d]", scroll_pos + 1, filtered_count);
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    wrefresh(win);
}

void panel_draw_news(WINDOW *win, mc_news_item_t *news, int count,
                     int scroll_pos)
{
    int h, w;
    getmaxyx(win, h, w);
    werase(win);

    int src_w = 16;
    int time_w = 18;
    int title_w = w - src_w - time_w - 6;
    if (title_w < 20) title_w = 20;

    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, " %-*s  %-*s  %*s",
              src_w, "Source", title_w, "Title", time_w, "Published");
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

    wattron(win, COLOR_PAIR(CP_DIM));
    mvwhline(win, 1, 0, ACS_HLINE, w);
    wattroff(win, COLOR_PAIR(CP_DIM));

    int row = 2;
    int displayed = 0;

    for (int i = 0; i < count && row < h - 1; i++) {
        if (displayed < scroll_pos) { displayed++; continue; }

        mc_news_item_t *n = &news[i];

        char time_str[20] = "Unknown";
        if (n->published_at > 0) {
            struct tm tm;
            localtime_r(&n->published_at, &tm);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &tm);
        }

        /* Alternate rows */
        if ((displayed - scroll_pos) % 2 == 1)
            wattron(win, A_DIM);

        /* Source */
        wattron(win, COLOR_PAIR(CP_NORMAL));
        mvwprintw(win, row, 1, " %-*.*s", src_w, src_w, n->source);
        wattroff(win, COLOR_PAIR(CP_NORMAL));

        /* Title - highlighted */
        waddstr(win, "  ");
        wattron(win, COLOR_PAIR(CP_ACTIVE));
        wprintw(win, "%-*.*s", title_w, title_w, n->title);
        wattroff(win, COLOR_PAIR(CP_ACTIVE));

        /* Time */
        waddstr(win, "  ");
        wattron(win, COLOR_PAIR(CP_DIM));
        wprintw(win, "%*s", time_w, time_str);
        wattroff(win, COLOR_PAIR(CP_DIM));

        if ((displayed - scroll_pos) % 2 == 1)
            wattroff(win, A_DIM);

        row++;
        displayed++;
    }

    if (count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, 3, 2, "No news available. Waiting for RSS feeds...");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    /* Scroll indicator */
    if (count > h - 3) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, h - 1, w - 20, "[%d/%d]", scroll_pos + 1, count);
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    wrefresh(win);
}
