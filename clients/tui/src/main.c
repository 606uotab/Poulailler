#include "client.h"
#include "ui.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = 8420;
    tui_theme_t theme = THEME_DARK;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)
            host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--light") == 0)
            theme = THEME_LIGHT;
        else if (strcmp(argv[i], "--dark") == 0)
            theme = THEME_DARK;
        else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                "MonitorCrebirth TUI Client\n"
                "Usage: %s [OPTIONS]\n\n"
                "Options:\n"
                "  --host HOST   Backend host (default: 127.0.0.1)\n"
                "  --port PORT   Backend port (default: 8420)\n"
                "  --light       Light terminal theme\n"
                "  --dark        Dark terminal theme (default)\n"
                "  --help        Show this help\n\n"
                "Keybindings:\n"
                "  TAB/1-6       Switch tabs\n"
                "  j/k           Scroll / move cursor\n"
                "  g/G           Jump to top/bottom\n"
                "  /             Search/filter\n"
                "  Enter/o       Detail view\n"
                "  L             Toggle light/dark theme\n"
                "  r             Force refresh\n"
                "  q             Quit\n", argv[0]);
            return 0;
        }
    }

    curl_global_init(CURL_GLOBAL_ALL);

    mc_client_t *client = mc_client_create(host, port);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    int ret = tui_run(client, theme);

    mc_client_destroy(client);
    curl_global_cleanup();

    return ret;
}
