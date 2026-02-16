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

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)
            host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                "MonitorCrebirth TUI Client\n"
                "Usage: %s [--host HOST] [--port PORT]\n\n"
                "Defaults: host=127.0.0.1, port=8420\n", argv[0]);
            return 0;
        }
    }

    curl_global_init(CURL_GLOBAL_ALL);

    mc_client_t *client = mc_client_create(host, port);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }

    int ret = tui_run(client);

    mc_client_destroy(client);
    curl_global_cleanup();

    return ret;
}
