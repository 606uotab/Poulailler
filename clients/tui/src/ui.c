#include "ui.h"
#include "ui_panels.h"
#include "client.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#define CP_UP      1
#define CP_DOWN    2
#define CP_HEADER  3
#define CP_ACTIVE  4
#define CP_NORMAL  5
#define CP_DIM     6
#define CP_SEARCH  7

#define MAX_ENTRIES 2048
#define MAX_NEWS    512
#define MAX_SEARCH  64
#define NUM_TABS    7
#define PAGE_SIZE   25

typedef enum {
    MODE_NORMAL,
    MODE_SEARCH,
    MODE_DETAIL
} ui_mode_t;

static const char *tab_names[NUM_TABS] = {
    "Crypto", "Exchanges", "Forex", "Indices", "Commodities", "News", "Custom"
};

static const mc_category_t tab_categories[NUM_TABS] = {
    MC_CAT_CRYPTO, MC_CAT_CRYPTO_EXCHANGE, MC_CAT_FOREX,
    MC_CAT_STOCK_INDEX, MC_CAT_COMMODITY, MC_CAT_NEWS, MC_CAT_CUSTOM
};

static void apply_theme(tui_theme_t theme)
{
    if (theme == THEME_LIGHT) {
        /* Light background: use dark foreground colors */
        init_pair(CP_UP,     COLOR_GREEN,   -1);
        init_pair(CP_DOWN,   COLOR_RED,     -1);
        init_pair(CP_HEADER, COLOR_BLUE,    -1);
        init_pair(CP_ACTIVE, COLOR_RED,     -1);
        init_pair(CP_NORMAL, COLOR_BLACK,   -1);
        init_pair(CP_DIM,    COLOR_WHITE,   -1);
        init_pair(CP_SEARCH, COLOR_MAGENTA, -1);
    } else {
        /* Dark background: use bright foreground colors */
        init_pair(CP_UP,     COLOR_GREEN,   -1);
        init_pair(CP_DOWN,   COLOR_RED,     -1);
        init_pair(CP_HEADER, COLOR_CYAN,    -1);
        init_pair(CP_ACTIVE, COLOR_YELLOW,  -1);
        init_pair(CP_NORMAL, COLOR_WHITE,   -1);
        init_pair(CP_DIM,    COLOR_BLACK,   -1);
        init_pair(CP_SEARCH, COLOR_MAGENTA, -1);
    }
}

static void draw_header(WINDOW *win, tui_theme_t theme)
{
    int w = getmaxx(win);
    werase(win);
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, "MonitorCrebirth");

    /* Theme indicator */
    const char *ti = theme == THEME_LIGHT ? "[LIGHT]" : "";
    if (ti[0]) {
        wattron(win, COLOR_PAIR(CP_DIM));
        mvwprintw(win, 0, 17, " %s", ti);
        wattroff(win, COLOR_PAIR(CP_DIM));
        wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    }

    char time_str[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm);
    mvwprintw(win, 0, w - (int)strlen(time_str) - 1, "%s", time_str);

    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    wnoutrefresh(win);
}

static void draw_tabs(WINDOW *win, int active_tab)
{
    int w = getmaxx(win);
    werase(win);

    int x = 1;
    for (int i = 0; i < NUM_TABS; i++) {
        if (i == active_tab) {
            wattron(win, COLOR_PAIR(CP_ACTIVE) | A_BOLD | A_REVERSE);
        } else {
            wattron(win, COLOR_PAIR(CP_NORMAL));
        }

        mvwprintw(win, 0, x, " %d:%s ", i + 1, tab_names[i]);
        x += (int)strlen(tab_names[i]) + 5;

        if (i == active_tab) {
            wattroff(win, COLOR_PAIR(CP_ACTIVE) | A_BOLD | A_REVERSE);
        } else {
            wattroff(win, COLOR_PAIR(CP_NORMAL));
        }
    }

    (void)w;
    wnoutrefresh(win);
}

/* Build a web URL for a data entry and open it in the browser */
static void open_entry_url(const mc_data_entry_t *e)
{
    char url[512];

    switch (e->category) {
    case MC_CAT_CRYPTO:
    case MC_CAT_CRYPTO_EXCHANGE:
        /* CoinGecko search by symbol */
        snprintf(url, sizeof(url),
                 "https://www.coingecko.com/en/coins/%s",
                 e->display_name[0] ? e->display_name : e->symbol);
        /* lowercase the coin name part */
        for (char *p = url + 35; *p; p++) {
            if (*p == ' ') *p = '-';
            else *p = tolower((unsigned char)*p);
        }
        break;
    case MC_CAT_STOCK_INDEX:
    case MC_CAT_COMMODITY:
        /* Yahoo Finance */
        snprintf(url, sizeof(url),
                 "https://finance.yahoo.com/quote/%s", e->symbol);
        break;
    case MC_CAT_FOREX:
        /* Google Finance for forex pairs */
        snprintf(url, sizeof(url),
                 "https://www.google.com/finance/quote/%s-%s",
                 e->symbol, e->currency[0] ? e->currency : "USD");
        break;
    default:
        /* Generic Google search */
        snprintf(url, sizeof(url),
                 "https://www.google.com/search?q=%s", e->symbol);
        break;
    }

    /* Temporarily leave ncurses, open URL, return */
    def_prog_mode();
    endwin();
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
    system(cmd);
    reset_prog_mode();
    refresh();
}

static void open_news_url(const mc_news_item_t *n)
{
    if (!n->url[0]) return;
    def_prog_mode();
    endwin();
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", n->url);
    system(cmd);
    reset_prog_mode();
    refresh();
}

static void draw_status(WINDOW *win, ui_mode_t mode,
                         const char *search_query,
                         int cursor_pos, int filtered_total,
                         mc_category_t active_cat)
{
    int w = getmaxx(win);
    werase(win);
    wattron(win, COLOR_PAIR(CP_HEADER));
    mvwhline(win, 0, 0, ACS_HLINE, w);

    if (mode == MODE_SEARCH) {
        panel_draw_search_bar(win, search_query, 1);
    } else if (search_query[0]) {
        panel_draw_search_bar(win, search_query, 0);
    } else if (mode == MODE_DETAIL) {
        mvwprintw(win, 1, 1,
                  "Enter/o:open in browser  q/Esc:close");
    } else {
        int page = filtered_total > 0 ? (cursor_pos / PAGE_SIZE) + 1 : 0;
        int pages = filtered_total > 0 ? ((filtered_total - 1) / PAGE_SIZE) + 1 : 0;
        if (active_cat == MC_CAT_COMMODITY) {
            mvwprintw(win, 1, 1,
                      "=F:Futures  .L:London  |  1-7:tab  j/k:scroll  /:search  Enter:detail  q:quit  |  pg %d/%d  %d items",
                      page, pages, filtered_total);
        } else {
            mvwprintw(win, 1, 1,
                      "1-7:tab  j/k:scroll  n/p:page  /:search  Enter:detail  L:theme  r:refresh  q:quit  |  pg %d/%d  %d items",
                      page, pages, filtered_total);
        }
    }
    wattroff(win, COLOR_PAIR(CP_HEADER));
    wnoutrefresh(win);
}

/* Get the nth filtered entry (by category + search) */
static mc_data_entry_t *get_filtered_entry(mc_data_entry_t *entries, int count,
                                            mc_category_t cat, const char *filter,
                                            int idx)
{
    int vis = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].category != cat) continue;
        if (filter && filter[0]) {
            int match = 0;
            const char *fields[] = { entries[i].symbol, entries[i].display_name, entries[i].source_name };
            for (int f = 0; f < 3; f++) {
                if (!fields[f]) continue;
                size_t flen = strlen(filter);
                size_t hlen = strlen(fields[f]);
                if (flen > hlen) continue;
                for (size_t k = 0; k <= hlen - flen; k++) {
                    int m = 1;
                    for (size_t j = 0; j < flen; j++) {
                        if (tolower((unsigned char)fields[f][k + j]) !=
                            tolower((unsigned char)filter[j])) { m = 0; break; }
                    }
                    if (m) { match = 1; break; }
                }
                if (match) break;
            }
            if (!match) continue;
        }
        if (vis == idx) return &entries[i];
        vis++;
    }
    return NULL;
}

/* Get the nth filtered news item */
static mc_news_item_t *get_filtered_news(mc_news_item_t *news, int count,
                                          const char *filter, int idx)
{
    int vis = 0;
    for (int i = 0; i < count; i++) {
        if (filter && filter[0]) {
            int match = 0;
            const char *fields[] = { news[i].title, news[i].source };
            for (int f = 0; f < 2; f++) {
                if (!fields[f]) continue;
                size_t flen = strlen(filter);
                size_t hlen = strlen(fields[f]);
                if (flen > hlen) continue;
                for (size_t k = 0; k <= hlen - flen; k++) {
                    int m = 1;
                    for (size_t j = 0; j < flen; j++) {
                        if (tolower((unsigned char)fields[f][k + j]) !=
                            tolower((unsigned char)filter[j])) { m = 0; break; }
                    }
                    if (m) { match = 1; break; }
                }
                if (match) break;
            }
            if (!match) continue;
        }
        if (vis == idx) return &news[i];
        vis++;
    }
    return NULL;
}

/* News tab index - depends on tab order */
static int news_tab_index(void)
{
    for (int i = 0; i < NUM_TABS; i++)
        if (tab_categories[i] == MC_CAT_NEWS) return i;
    return 4;
}

int tui_run(mc_client_t *client, tui_theme_t theme)
{
    /* Init ncurses */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        apply_theme(theme);
    }

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    /* Minimum terminal size: 20 cols x 8 rows */
    if (max_y < 8 || max_x < 20) {
        endwin();
        fprintf(stderr, "Terminal too small (%dx%d). Minimum: 20x8\n", max_x, max_y);
        return 1;
    }

    WINDOW *header_win  = newwin(1, max_x, 0, 0);
    WINDOW *tab_win     = newwin(1, max_x, 1, 0);
    WINDOW *content_win = newwin(max_y - 4, max_x, 2, 0);
    WINDOW *status_win  = newwin(2, max_x, max_y - 2, 0);

    int active_tab = 0;
    int scroll_pos = 0;
    int cursor_pos = 0;
    int filtered_total = 0;
    int news_tab = news_tab_index();

    ui_mode_t mode = MODE_NORMAL;
    char search_query[MAX_SEARCH] = {0};
    int search_len = 0;

    mc_data_entry_t *entries = malloc(MAX_ENTRIES * sizeof(mc_data_entry_t));
    mc_news_item_t *news = malloc(MAX_NEWS * sizeof(mc_news_item_t));
    int entry_count = 0, news_count = 0;

    /* Non-blocking getch with 500ms timeout */
    wtimeout(stdscr, 500);

    int running = 1;
    int tick = 0;

    while (running) {
        /* Fetch data every 4 ticks (~2 seconds) — keep old data on failure */
        if (tick % 4 == 0) {
            int n = mc_client_get_entries(client, entries, MAX_ENTRIES);
            if (n > 0) entry_count = n;
            n = mc_client_get_news(client, news, MAX_NEWS);
            if (n > 0) news_count = n;
        }

        /* Draw UI */
        draw_header(header_win, theme);
        draw_tabs(tab_win, active_tab);

        if (mode == MODE_DETAIL) {
            if (active_tab == news_tab) {
                panel_draw_news(content_win, news, news_count,
                                scroll_pos, search_query, cursor_pos);
                mc_news_item_t *sel = get_filtered_news(news, news_count,
                                                         search_query, cursor_pos);
                if (sel) panel_draw_detail_news(content_win, sel);
            } else {
                panel_draw_entries(content_win, entries, entry_count,
                                   tab_categories[active_tab], scroll_pos,
                                   search_query, cursor_pos);
                mc_data_entry_t *sel = get_filtered_entry(entries, entry_count,
                                                           tab_categories[active_tab],
                                                           search_query, cursor_pos);
                if (sel) panel_draw_detail_entry(content_win, sel);
            }
        } else {
            if (active_tab == news_tab) {
                filtered_total = panel_draw_news(content_win, news, news_count,
                                                  scroll_pos, search_query, cursor_pos);
            } else {
                filtered_total = panel_draw_entries(content_win, entries, entry_count,
                                                     tab_categories[active_tab], scroll_pos,
                                                     search_query, cursor_pos);
            }
        }

        draw_status(status_win, mode, search_query, cursor_pos, filtered_total,
                    tab_categories[active_tab]);

        /* Flush all window updates in a single
           screen write to avoid flicker */
        doupdate();

        /* Handle input */
        int ch = getch();

        if (mode == MODE_SEARCH) {
            switch (ch) {
            case 27: /* ESC */
                search_query[0] = '\0';
                search_len = 0;
                mode = MODE_NORMAL;
                cursor_pos = 0;
                scroll_pos = 0;
                break;

            case '\n':
            case '\r':
                mode = MODE_NORMAL;
                cursor_pos = 0;
                scroll_pos = 0;
                break;

            case KEY_BACKSPACE:
            case 127:
            case 8:
                if (search_len > 0) {
                    search_len--;
                    search_query[search_len] = '\0';
                }
                break;

            default:
                if (ch >= 32 && ch < 127 && search_len < MAX_SEARCH - 1) {
                    search_query[search_len++] = (char)ch;
                    search_query[search_len] = '\0';
                }
                break;
            }
        } else if (mode == MODE_DETAIL) {
            switch (ch) {
            case 27:
            case 'q':
                mode = MODE_NORMAL;
                break;
            case 'o':
            case '\n':
            case '\r':
                /* Open in browser */
                if (active_tab == news_tab) {
                    mc_news_item_t *sel = get_filtered_news(news, news_count,
                                                             search_query, cursor_pos);
                    if (sel) open_news_url(sel);
                } else {
                    mc_data_entry_t *sel = get_filtered_entry(entries, entry_count,
                                                               tab_categories[active_tab],
                                                               search_query, cursor_pos);
                    if (sel) open_entry_url(sel);
                }
                break;
            }
        } else {
            /* Normal mode */
            switch (ch) {
            case 'q':
            case 'Q':
                running = 0;
                break;

            case '\t':
            case 't':
                active_tab = (active_tab + 1) % NUM_TABS;
                scroll_pos = 0;
                cursor_pos = 0;
                break;

            case 'T':
            case KEY_BTAB:
                active_tab = (active_tab - 1 + NUM_TABS) % NUM_TABS;
                scroll_pos = 0;
                cursor_pos = 0;
                break;

            case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                if (ch - '1' < NUM_TABS) {
                    active_tab = ch - '1';
                    scroll_pos = 0;
                    cursor_pos = 0;
                }
                break;

            case 'j':
            case KEY_DOWN:
                if (cursor_pos < filtered_total - 1) {
                    cursor_pos++;
                    int visible_rows = max_y - 4 - 3;
                    if (cursor_pos >= scroll_pos + visible_rows)
                        scroll_pos = cursor_pos - visible_rows + 1;
                }
                break;

            case 'k':
            case KEY_UP:
                if (cursor_pos > 0) {
                    cursor_pos--;
                    if (cursor_pos < scroll_pos)
                        scroll_pos = cursor_pos;
                }
                break;

            case KEY_NPAGE: /* Page Down */
            case 'n':
                if (filtered_total > 0) {
                    cursor_pos += PAGE_SIZE;
                    if (cursor_pos >= filtered_total)
                        cursor_pos = filtered_total - 1;
                    int visible_rows = max_y - 4 - 3;
                    if (cursor_pos >= scroll_pos + visible_rows)
                        scroll_pos = cursor_pos - visible_rows + 1;
                }
                break;

            case KEY_PPAGE: /* Page Up */
            case 'p':
                cursor_pos -= PAGE_SIZE;
                if (cursor_pos < 0) cursor_pos = 0;
                if (cursor_pos < scroll_pos)
                    scroll_pos = cursor_pos;
                break;

            case 'g':
                cursor_pos = 0;
                scroll_pos = 0;
                break;

            case 'G':
                if (filtered_total > 0) {
                    cursor_pos = filtered_total - 1;
                    int visible_rows = max_y - 4 - 3;
                    if (cursor_pos >= visible_rows)
                        scroll_pos = cursor_pos - visible_rows + 1;
                    else
                        scroll_pos = 0;
                }
                break;

            case '/':
                mode = MODE_SEARCH;
                search_len = (int)strlen(search_query);
                break;

            case 27: /* ESC */
                if (search_query[0]) {
                    search_query[0] = '\0';
                    search_len = 0;
                    cursor_pos = 0;
                    scroll_pos = 0;
                }
                break;

            case '\n':
            case '\r':
                if (filtered_total > 0)
                    mode = MODE_DETAIL;
                break;

            case 'L': /* Toggle light/dark theme */
                theme = (theme == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
                apply_theme(theme);
                /* Force full redraw */
                clearok(curscr, TRUE);
                break;

            case 'r':
                mc_client_refresh(client);
                tick = -1;
                break;

            case KEY_RESIZE:
                getmaxyx(stdscr, max_y, max_x);
                if (max_y >= 8 && max_x >= 20) {
                    wresize(header_win, 1, max_x);
                    wresize(tab_win, 1, max_x);
                    wresize(content_win, max_y - 4, max_x);
                    mvwin(status_win, max_y - 2, 0);
                    wresize(status_win, 2, max_x);
                }
                break;
            }
        }

        tick++;
    }

    free(entries);
    free(news);

    delwin(header_win);
    delwin(tab_win);
    delwin(content_win);
    delwin(status_win);
    endwin();

    return 0;
}
