#include "ui.h"
#include "ui_panels.h"
#include "client.h"

#include <ncurses.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define CP_UP      1
#define CP_DOWN    2
#define CP_HEADER  3
#define CP_ACTIVE  4
#define CP_NORMAL  5

#define MAX_ENTRIES 256
#define MAX_NEWS    256
#define NUM_TABS    5

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

static void draw_status(WINDOW *win, int entry_count, int news_count)
{
    int w = getmaxx(win);
    werase(win);
    wattron(win, COLOR_PAIR(CP_HEADER));
    mvwhline(win, 0, 0, ACS_HLINE, w);
    mvwprintw(win, 1, 1,
              "TAB/1-5:switch  j/k:scroll  r:refresh  q:quit  |  %d entries  %d news",
              entry_count, news_count);
    wattroff(win, COLOR_PAIR(CP_HEADER));
    wrefresh(win);
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
    }

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    WINDOW *header_win  = newwin(1, max_x, 0, 0);
    WINDOW *tab_win     = newwin(1, max_x, 1, 0);
    WINDOW *content_win = newwin(max_y - 4, max_x, 2, 0);
    WINDOW *status_win  = newwin(2, max_x, max_y - 2, 0);

    int active_tab = 0;
    int scroll_pos = 0;

    mc_data_entry_t *entries = malloc(MAX_ENTRIES * sizeof(mc_data_entry_t));
    mc_news_item_t *news = malloc(MAX_NEWS * sizeof(mc_news_item_t));
    int entry_count = 0, news_count = 0;

    /* Non-blocking getch with 1s timeout */
    wtimeout(stdscr, 1000);

    int running = 1;
    int tick = 0;

    while (running) {
        /* Fetch data every 2 seconds */
        if (tick % 2 == 0) {
            entry_count = mc_client_get_entries(client, entries, MAX_ENTRIES);
            news_count = mc_client_get_news(client, news, MAX_NEWS);
        }

        /* Draw UI */
        draw_header(header_win);
        draw_tabs(tab_win, active_tab);
        draw_status(status_win, entry_count, news_count);

        if (active_tab == 3) {
            /* News tab */
            panel_draw_news(content_win, news, news_count, scroll_pos);
        } else {
            panel_draw_entries(content_win, entries, entry_count,
                               tab_categories[active_tab], scroll_pos);
        }

        /* Handle input */
        int ch = getch();
        switch (ch) {
        case 'q':
        case 'Q':
            running = 0;
            break;

        case '\t':
        case 't':
            active_tab = (active_tab + 1) % NUM_TABS;
            scroll_pos = 0;
            break;

        case 'T':
        case KEY_BTAB:
            active_tab = (active_tab - 1 + NUM_TABS) % NUM_TABS;
            scroll_pos = 0;
            break;

        case '1': case '2': case '3': case '4': case '5':
            active_tab = ch - '1';
            scroll_pos = 0;
            break;

        case 'j':
        case KEY_DOWN:
            scroll_pos++;
            break;

        case 'k':
        case KEY_UP:
            if (scroll_pos > 0) scroll_pos--;
            break;

        case 'r':
            mc_client_refresh(client);
            tick = -1; /* Force data fetch on next iteration */
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
