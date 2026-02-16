#ifndef MC_TUI_UI_H
#define MC_TUI_UI_H

#include "client.h"

typedef enum {
    THEME_DARK,
    THEME_LIGHT
} tui_theme_t;

int tui_run(mc_client_t *client, tui_theme_t theme);

#endif
