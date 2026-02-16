#include "ui.h"
#include "ui_panels.h"
#include "client.h"

#include <ncurses.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>

#define CP_UP      1
#define CP_DOWN    2
#define CP_HEADER  3
#define CP_ACTIVE  4
#define CP_NORMAL  5
#define CP_DIM     6
#define CP_SEARCH  7

#define MAX_ENTRIES 256
#define MAX_NEWS    256
#define NUM_TABS    5
#define MAX_SEARCH  64

typedef enum {
    MODE_NORMAL,
    MODE_SEARCH,
    MODE_DETAIL
} ui_mode_t;

static const char *tab_names[NUM_TABS] = {
    "Crypto", "Indices", "Commodities", "News", "Custom"
};

static const mc_category_t tab_categories[NUM_TABS] = {
    MC_CAT_CRYPTO, MC_CAT_STOCK_INDEX, MC_CAT_COMMODITY,
    MC_CAT_NEWS, MC_CAT_CUSTOM
};

static void draw_header(WINDOW *win)
{
    int w = getmaxx(win);
    werase(win);
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwprintw(win, 0, 1, "MonitorCrebirth");

    char time_str[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm);
    mvwprintw(win, 0, w - (int)strlen(time_str) - 1, "%s", time_str);

    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    wrefresh(win);
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
    wrefresh(win);
}

static void draw_status(WINDOW *win, int entry_count, int news_count,
                         ui_mode_t mode, const char *search_query)
{
    int w = getmaxx(win);
    werase(win);
    wattron(win, COLOR_PAIR(CP_HEADER));
    mvwhline(win, 0, 0, ACS_HLINE, w);

    if (mode == MODE_SEARCH) {
        /* Search bar takes over the status line */
        panel_draw_search_bar(win, search_query, 1);
    } else if (search_query[0]) {
        /* Show active filter */
        panel_draw_search_bar(win, search_query, 0);
    } else {
        mvwprintw(win, 1, 1,
                  "TAB/1-5:switch  j/k:scroll  /:search  Enter:detail  r:refresh  q:quit  |  %d entries  %d news",
                  entry_count, news_count);
    }
    wattroff(win, COLOR_PAIR(CP_HEADER));
    wrefresh(win);
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
            /* Quick case-insensitive check across symbol/display_name/source */
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

int tui_run(mc_client_t *client)
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
        init_pair(CP_UP,     COLOR_GREEN,  -1);
        init_pair(CP_DOWN,   COLOR_RED,    -1);
        init_pair(CP_HEADER, COLOR_CYAN,   -1);
        init_pair(CP_ACTIVE, COLOR_YELLOW, -1);
        init_pair(CP_NORMAL, COLOR_WHITE,  -1);
        init_pair(CP_DIM,    COLOR_BLACK,  -1);
        init_pair(CP_SEARCH, COLOR_MAGENTA, -1);
    }

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    WINDOW *header_win  = newwin(1, max_x, 0, 0);
    WINDOW *tab_win     = newwin(1, max_x, 1, 0);
    WINDOW *content_win = newwin(max_y - 4, max_x, 2, 0);
    WINDOW *status_win  = newwin(2, max_x, max_y - 2, 0);

    int active_tab = 0;
    int scroll_pos = 0;
    int cursor_pos = 0;
    int filtered_total = 0;

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
        /* Fetch data every 4 ticks (~2 seconds) */
        if (tick % 4 == 0) {
            entry_count = mc_client_get_entries(client, entries, MAX_ENTRIES);
            news_count = mc_client_get_news(client, news, MAX_NEWS);
        }

        /* Draw UI */
        draw_header(header_win);
        draw_tabs(tab_win, active_tab);

        if (mode == MODE_DETAIL) {
            /* Detail popup overlay on content */
            if (active_tab == 3) {
                /* First draw the list behind */
                panel_draw_news(content_win, news, news_count,
                                scroll_pos, search_query, cursor_pos);
                /* Then overlay detail */
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
            if (active_tab == 3) {
                filtered_total = panel_draw_news(content_win, news, news_count,
                                                  scroll_pos, search_query, cursor_pos);
            } else {
                filtered_total = panel_draw_entries(content_win, entries, entry_count,
                                                     tab_categories[active_tab], scroll_pos,
                                                     search_query, cursor_pos);
            }
        }

        draw_status(status_win, entry_count, news_count, mode, search_query);

        /* Handle input */
        int ch = getch();

        if (mode == MODE_SEARCH) {
            /* Search mode input handling */
            switch (ch) {
            case 27: /* ESC - clear search and exit */
                search_query[0] = '\0';
                search_len = 0;
                mode = MODE_NORMAL;
                cursor_pos = 0;
                scroll_pos = 0;
                break;

            case '\n':
            case '\r': /* Enter - confirm search */
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
            /* Detail mode - only ESC/q close it */
            switch (ch) {
            case 27:
            case 'q':
                mode = MODE_NORMAL;
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

            case '1': case '2': case '3': case '4': case '5':
                active_tab = ch - '1';
                scroll_pos = 0;
                cursor_pos = 0;
                break;

            case 'j':
            case KEY_DOWN:
                if (cursor_pos < filtered_total - 1) {
                    cursor_pos++;
                    /* Auto-scroll if cursor goes below visible area */
                    int visible_rows = max_y - 4 - 3; /* content_win height - header/separator/scrollbar */
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

            case 'g': /* Go to top */
                cursor_pos = 0;
                scroll_pos = 0;
                break;

            case 'G': /* Go to bottom */
                if (filtered_total > 0) {
                    cursor_pos = filtered_total - 1;
                    int visible_rows = max_y - 4 - 3;
                    if (cursor_pos >= visible_rows)
                        scroll_pos = cursor_pos - visible_rows + 1;
                    else
                        scroll_pos = 0;
                }
                break;

            case '/': /* Enter search mode */
                mode = MODE_SEARCH;
                search_len = (int)strlen(search_query);
                break;

            case 27: /* ESC - clear search filter */
                if (search_query[0]) {
                    search_query[0] = '\0';
                    search_len = 0;
                    cursor_pos = 0;
                    scroll_pos = 0;
                }
                break;

            case '\n':
            case '\r': /* Enter - open detail */
            case 'o':
                if (filtered_total > 0)
                    mode = MODE_DETAIL;
                break;

            case 'r':
                mc_client_refresh(client);
                tick = -1;
                break;

            case KEY_RESIZE:
                getmaxyx(stdscr, max_y, max_x);
                wresize(header_win, 1, max_x);
                wresize(tab_win, 1, max_x);
                wresize(content_win, max_y - 4, max_x);
                mvwin(status_win, max_y - 2, 0);
                wresize(status_win, 2, max_x);
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
