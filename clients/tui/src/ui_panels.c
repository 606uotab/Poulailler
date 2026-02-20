#include "ui_panels.h"

#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>

/* Color pairs (defined in ui.c) */
#define CP_UP      1
#define CP_DOWN    2
#define CP_HEADER  3
#define CP_ACTIVE  4
#define CP_NORMAL  5
#define CP_DIM     6
#define CP_SEARCH  7

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

static const char *time_ago(time_t t, char *buf, size_t sz)
{
    if (t == 0) { snprintf(buf, sz, "never"); return buf; }
    time_t diff = time(NULL) - t;
    if (diff < 60)        snprintf(buf, sz, "%lds ago", (long)diff);
    else if (diff < 3600) snprintf(buf, sz, "%ldm ago", (long)(diff / 60));
    else                  snprintf(buf, sz, "%ldh ago", (long)(diff / 3600));
    return buf;
}

/* Case-insensitive substring match */
static int str_match(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    if (!haystack) return 0;

    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* Check if an entry matches the search filter (symbol, display_name, source) */
static int entry_matches(const mc_data_entry_t *e, const char *filter)
{
    if (!filter || !filter[0]) return 1;
    return str_match(e->symbol, filter) ||
           str_match(e->display_name, filter) ||
           str_match(e->source_name, filter);
}

/* Check if a news item matches the search filter (title, source) */
static int news_matches(const mc_news_item_t *n, const char *filter)
{
    if (!filter || !filter[0]) return 1;
    return str_match(n->title, filter) ||
           str_match(n->source, filter);
}

int panel_draw_entries(WINDOW *win, mc_data_entry_t *entries, int count,
                       mc_category_t cat_filter, int scroll_pos,
                       const char *search_filter, int cursor_pos)
{
    int h, w;
    getmaxyx(win, h, w);
    werase(win);

    /* Header row - adapt volume column label by category */
    const char *vol_label = "Volume";
    if (cat_filter == MC_CAT_CRYPTO) vol_label = "MCap";
    else if (cat_filter == MC_CAT_CRYPTO_EXCHANGE) vol_label = "Vol/BTC";

    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, " %-14s %12s %8s %8s  %-12s  %s",
              "Symbol", "Price", "Chg%", vol_label, "Source", "Updated");
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

    wattron(win, COLOR_PAIR(CP_DIM));
    mvwhline(win, 1, 0, ACS_HLINE, w);
    wattroff(win, COLOR_PAIR(CP_DIM));

    int row = 2;
    int displayed = 0;
    int filtered_count = 0;

    /* Count filtered items first */
    for (int i = 0; i < count; i++) {
        if (entries[i].category != cat_filter) continue;
        if (!entry_matches(&entries[i], search_filter)) continue;
        filtered_count++;
    }

    int vis_idx = 0;
    char last_source[MC_MAX_SOURCE] = "";
    for (int i = 0; i < count && row < h - 1; i++) {
        mc_data_entry_t *e = &entries[i];
        if (e->category != cat_filter) continue;
        if (!entry_matches(e, search_filter)) continue;

        if (vis_idx < scroll_pos) {
            strncpy(last_source, e->source_name, MC_MAX_SOURCE - 1);
            vis_idx++;
            continue;
        }

        /* Region separator header for grouped categories */
        if ((cat_filter == MC_CAT_STOCK_INDEX || cat_filter == MC_CAT_FOREX) &&
            strcmp(e->source_name, last_source) != 0 && row < h - 2) {
            wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
            mvwprintw(win, row, 1, " %s ", e->source_name);
            int name_len = (int)strlen(e->source_name) + 3;
            wattron(win, COLOR_PAIR(CP_DIM));
            mvwhline(win, row, name_len + 1, ACS_HLINE, w - name_len - 1);
            wattroff(win, COLOR_PAIR(CP_DIM));
            wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
            row++;
        }
        strncpy(last_source, e->source_name, MC_MAX_SOURCE - 1);

        if (row >= h - 1) break;

        int cp = e->change_pct >= 0 ? CP_UP : CP_DOWN;
        const char *arrow = e->change_pct >= 0 ? "+" : "";
        const char *indicator = e->change_pct >= 0 ? "\u25B2" : "\u25BC";

        char vol_str[16], time_str[32];
        format_volume(e->volume, vol_str, sizeof(vol_str));
        time_ago(e->fetched_at, time_str, sizeof(time_str));

        /* Cursor highlight */
        int is_cursor = (vis_idx == cursor_pos);
        if (is_cursor) {
            wattron(win, A_REVERSE);
        } else if ((displayed) % 2 == 1) {
            wattron(win, A_DIM);
        }

        const char *label = (e->display_name[0]) ? e->display_name : e->symbol;

        wattron(win, COLOR_PAIR(cp));
        mvwprintw(win, row, 1, " %-14s $%11.2f %s%6.2f%% %s %8s  %-12s  %s",
                  label, e->value, arrow, e->change_pct,
                  indicator, vol_str, e->source_name, time_str);
        wattroff(win, COLOR_PAIR(cp));

        if (is_cursor) {
            wattroff(win, A_REVERSE);
        } else if ((displayed) % 2 == 1) {
            wattroff(win, A_DIM);
        }

        row++;
        displayed++;
        vis_idx++;
    }

    if (filtered_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        if (search_filter && search_filter[0])
            mvwprintw(win, 3, 2, "No results for \"%s\"", search_filter);
        else
            mvwprintw(win, 3, 2, "No data available. Waiting for fetch...");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    /* Scroll indicator */
    if (filtered_count > h - 3) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, h - 1, w - 20, "[%d/%d]", scroll_pos + 1, filtered_count);
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    wnoutrefresh(win);
    return filtered_count;
}

int panel_draw_news(WINDOW *win, mc_news_item_t *news, int count,
                    int scroll_pos, const char *search_filter,
                    int cursor_pos)
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
    int filtered_count = 0;

    for (int i = 0; i < count; i++) {
        if (!news_matches(&news[i], search_filter)) continue;
        filtered_count++;
    }

    int vis_idx = 0;
    for (int i = 0; i < count && row < h - 1; i++) {
        mc_news_item_t *n = &news[i];
        if (!news_matches(n, search_filter)) continue;

        if (vis_idx < scroll_pos) { vis_idx++; continue; }

        char time_str[20] = "Unknown";
        if (n->published_at > 0) {
            struct tm tm;
            localtime_r(&n->published_at, &tm);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &tm);
        }

        int is_cursor = (vis_idx == cursor_pos);
        if (is_cursor) {
            wattron(win, A_REVERSE);
        } else if ((displayed) % 2 == 1) {
            wattron(win, A_DIM);
        }

        /* Source */
        wattron(win, COLOR_PAIR(CP_NORMAL));
        mvwprintw(win, row, 1, " %-*.*s", src_w, src_w, n->source);
        wattroff(win, COLOR_PAIR(CP_NORMAL));

        /* Title */
        waddstr(win, "  ");
        wattron(win, COLOR_PAIR(CP_ACTIVE));
        wprintw(win, "%-*.*s", title_w, title_w, n->title);
        wattroff(win, COLOR_PAIR(CP_ACTIVE));

        /* Time */
        waddstr(win, "  ");
        wattron(win, COLOR_PAIR(CP_DIM));
        wprintw(win, "%*s", time_w, time_str);
        wattroff(win, COLOR_PAIR(CP_DIM));

        if (is_cursor) {
            wattroff(win, A_REVERSE);
        } else if ((displayed) % 2 == 1) {
            wattroff(win, A_DIM);
        }

        row++;
        displayed++;
        vis_idx++;
    }

    if (filtered_count == 0) {
        wattron(win, COLOR_PAIR(CP_DIM));
        if (search_filter && search_filter[0])
            mvwprintw(win, 3, 2, "No results for \"%s\"", search_filter);
        else
            mvwprintw(win, 3, 2, "No news available. Waiting for RSS feeds...");
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    /* Scroll indicator */
    if (filtered_count > h - 3) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, h - 1, w - 20, "[%d/%d]", scroll_pos + 1, filtered_count);
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    wnoutrefresh(win);
    return filtered_count;
}

/* ── Detail popup ─────────────────────────────────────────────── */

static void draw_box(WINDOW *win, int y, int x, int h, int w, const char *title)
{
    /* Draw border */
    wattron(win, COLOR_PAIR(CP_HEADER));
    for (int i = 0; i < h; i++) {
        mvwhline(win, y + i, x, ' ', w);
    }
    /* Top border */
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwhline(win, y, x + 1, ACS_HLINE, w - 2);
    mvwaddch(win, y, x + w - 1, ACS_URCORNER);
    /* Sides */
    for (int i = 1; i < h - 1; i++) {
        mvwaddch(win, y + i, x, ACS_VLINE);
        mvwaddch(win, y + i, x + w - 1, ACS_VLINE);
    }
    /* Bottom border */
    mvwaddch(win, y + h - 1, x, ACS_LLCORNER);
    mvwhline(win, y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvwaddch(win, y + h - 1, x + w - 1, ACS_LRCORNER);

    /* Title */
    if (title) {
        wattron(win, A_BOLD);
        mvwprintw(win, y, x + 2, " %s ", title);
        wattroff(win, A_BOLD);
    }
    wattroff(win, COLOR_PAIR(CP_HEADER));
}

static void detail_label(WINDOW *win, int y, int x, const char *label, const char *value)
{
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, y, x, "%-14s", label);
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    wattron(win, COLOR_PAIR(CP_NORMAL));
    wprintw(win, " %s", value);
    wattroff(win, COLOR_PAIR(CP_NORMAL));
}

void panel_draw_detail_entry(WINDOW *win, const mc_data_entry_t *entry)
{
    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Popup size */
    int bw = ww > 70 ? 60 : ww - 6;
    int bh = 16;
    int bx = (ww - bw) / 2;
    int by = (wh - bh) / 2;
    if (by < 1) by = 1;

    const char *title = entry->display_name[0] ? entry->display_name : entry->symbol;
    draw_box(win, by, bx, bh, bw, title);

    int row = by + 2;
    int lx = bx + 2;
    int vw = bw - 4;

    char buf[128];

    detail_label(win, row++, lx, "Symbol:", entry->symbol);
    if (entry->display_name[0])
        detail_label(win, row++, lx, "Name:", entry->display_name);

    snprintf(buf, sizeof(buf), "$%.8g %s", entry->value, entry->currency);
    detail_label(win, row++, lx, "Price:", buf);

    /* Change with color */
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, row, lx, "%-14s", "Change:");
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    int cp = entry->change_pct >= 0 ? CP_UP : CP_DOWN;
    const char *arrow = entry->change_pct >= 0 ? "\u25B2" : "\u25BC";
    wattron(win, COLOR_PAIR(cp) | A_BOLD);
    wprintw(win, " %s %+.2f%%", arrow, entry->change_pct);
    wattroff(win, COLOR_PAIR(cp) | A_BOLD);
    row++;

    char vol_str[16];
    format_volume(entry->volume, vol_str, sizeof(vol_str));
    detail_label(win, row++, lx, "Volume:", vol_str);

    detail_label(win, row++, lx, "Source:", entry->source_name);

    const char *st = "Unknown";
    switch (entry->source_type) {
    case MC_SOURCE_RSS: st = "RSS"; break;
    case MC_SOURCE_REST: st = "REST API"; break;
    case MC_SOURCE_WEBSOCKET: st = "WebSocket"; break;
    }
    detail_label(win, row++, lx, "Source type:", st);

    char time_str[32];
    if (entry->fetched_at > 0) {
        struct tm tm;
        localtime_r(&entry->fetched_at, &tm);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);
    } else {
        snprintf(time_str, sizeof(time_str), "N/A");
    }
    detail_label(win, row++, lx, "Last update:", time_str);

    row++;
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, row, lx, "Press ESC or q to close");
    wattroff(win, COLOR_PAIR(CP_DIM));

    (void)vw;
    wnoutrefresh(win);
}

void panel_draw_detail_news(WINDOW *win, const mc_news_item_t *news)
{
    int wh, ww;
    getmaxyx(win, wh, ww);

    /* Popup size */
    int bw = ww > 80 ? 74 : ww - 6;
    int bh = wh > 20 ? 18 : wh - 4;
    int bx = (ww - bw) / 2;
    int by = (wh - bh) / 2;
    if (by < 1) by = 1;

    draw_box(win, by, bx, bh, bw, "News Detail");

    int row = by + 2;
    int lx = bx + 2;
    int tw = bw - 4; /* text width inside box */

    /* Title - may wrap */
    wattron(win, COLOR_PAIR(CP_ACTIVE) | A_BOLD);
    int tlen = (int)strlen(news->title);
    int off = 0;
    while (off < tlen && row < by + bh - 5) {
        int chunk = tw;
        if (off + chunk > tlen) chunk = tlen - off;
        mvwprintw(win, row, lx, "%-*.*s", tw, chunk, news->title + off);
        off += chunk;
        row++;
    }
    wattroff(win, COLOR_PAIR(CP_ACTIVE) | A_BOLD);

    row++;

    detail_label(win, row++, lx, "Source:", news->source);

    /* URL - truncated to fit */
    if (news->url[0]) {
        wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
        mvwprintw(win, row, lx, "%-14s", "URL:");
        wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
        wattron(win, COLOR_PAIR(CP_NORMAL) | A_UNDERLINE);
        wprintw(win, " %-*.*s", tw - 15, tw - 15, news->url);
        wattroff(win, COLOR_PAIR(CP_NORMAL) | A_UNDERLINE);
        row++;
    }

    char time_str[32] = "Unknown";
    if (news->published_at > 0) {
        struct tm tm;
        localtime_r(&news->published_at, &tm);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm);
    }
    detail_label(win, row++, lx, "Published:", time_str);

    /* Summary (if available) */
    if (news->summary[0] && row < by + bh - 3) {
        row++;
        wattron(win, COLOR_PAIR(CP_DIM));
        int slen = (int)strlen(news->summary);
        int soff = 0;
        while (soff < slen && row < by + bh - 2) {
            int chunk = tw;
            if (soff + chunk > slen) chunk = slen - soff;
            mvwprintw(win, row, lx, "%-*.*s", tw, chunk, news->summary + soff);
            soff += chunk;
            row++;
        }
        wattroff(win, COLOR_PAIR(CP_DIM));
    }

    /* Close hint */
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, by + bh - 2, lx, "Press ESC or q to close");
    wattroff(win, COLOR_PAIR(CP_DIM));

    wnoutrefresh(win);
}

/* ── Search bar ───────────────────────────────────────────────── */

void panel_draw_search_bar(WINDOW *win, const char *query, int active)
{
    werase(win);

    if (active) {
        wattron(win, COLOR_PAIR(CP_SEARCH) | A_BOLD);
        mvwprintw(win, 0, 1, "/");
        wattroff(win, A_BOLD);
        wprintw(win, "%s", query);
        /* Blinking cursor */
        wattron(win, A_BLINK);
        waddch(win, '_');
        wattroff(win, A_BLINK);
        wattroff(win, COLOR_PAIR(CP_SEARCH));
    } else if (query[0]) {
        wattron(win, COLOR_PAIR(CP_SEARCH));
        mvwprintw(win, 0, 1, "Filter: %s", query);
        wattron(win, COLOR_PAIR(CP_DIM));
        wprintw(win, "  (/ to edit, ESC to clear)");
        wattroff(win, COLOR_PAIR(CP_DIM));
        wattroff(win, COLOR_PAIR(CP_SEARCH));
    }

    wnoutrefresh(win);
}
