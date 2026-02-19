#include "mc_scheduler.h"
#include "mc_fetch_rss.h"
#include "mc_fetch_rest.h"
#include "mc_fetch_ws.h"
#include "mc_log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define MAX_SNAPSHOT_ENTRIES 512
#define MAX_SNAPSHOT_NEWS   512
#define PRUNE_INTERVAL_SEC  300   /* Prune DB every 5 minutes */
#define PRUNE_MAX_AGE_SEC   86400 /* Keep data for 24 hours */
#define MAX_BACKOFF_SEC     300   /* Max retry backoff: 5 min */

/* Per-source tracking for retry backoff */
typedef struct {
    int  consecutive_failures;
    int  backoff_sec;
    time_t last_attempt;
    time_t last_success;
} source_health_t;

struct mc_scheduler {
    const mc_config_t *cfg;
    mc_db_t           *db;

    /* Background threads */
    pthread_t          rss_thread;
    pthread_t          rest_thread;
    pthread_t          prune_thread;
    int                rss_thread_active;
    int                rest_thread_active;
    int                prune_thread_active;

    /* WebSocket connections */
    mc_ws_conn_t      *ws_conns[MC_MAX_SOURCES];
    int                ws_count;

    /* Per-source health tracking */
    source_health_t    rss_health[MC_MAX_SOURCES];
    source_health_t    rest_health[MC_MAX_SOURCES];

    /* Shared snapshot for API consumers */
    pthread_rwlock_t   snapshot_lock;
    mc_data_entry_t    entries[MAX_SNAPSHOT_ENTRIES];
    int                entry_count;
    mc_news_item_t     news[MAX_SNAPSHOT_NEWS];
    int                news_count;

    volatile int       running;
    volatile int       force_refresh;

    /* WS snapshot debounce */
    volatile time_t    ws_last_snapshot;
};

static void update_snapshot(mc_scheduler_t *sched)
{
    pthread_rwlock_wrlock(&sched->snapshot_lock);

    sched->entry_count = 0;
    for (int cat = MC_CAT_CRYPTO; cat <= MC_CAT_CRYPTO_EXCHANGE; cat++) {
        int remaining = MAX_SNAPSHOT_ENTRIES - sched->entry_count;
        if (remaining <= 0) break;
        int n = mc_db_get_latest_entries(sched->db, cat,
                    &sched->entries[sched->entry_count], remaining);
        sched->entry_count += n;
    }

    sched->news_count = mc_db_get_all_latest_news(sched->db,
                            sched->news, MAX_SNAPSHOT_NEWS);

    pthread_rwlock_unlock(&sched->snapshot_lock);
}

static int should_skip_source(source_health_t *h, int force)
{
    if (force) return 0;
    if (h->consecutive_failures == 0) return 0;

    time_t now = time(NULL);
    if (now - h->last_attempt < h->backoff_sec) {
        return 1; /* Still in backoff period */
    }
    return 0;
}

static void record_success(source_health_t *h)
{
    h->consecutive_failures = 0;
    h->backoff_sec = 0;
    h->last_success = time(NULL);
    h->last_attempt = time(NULL);
}

static void record_failure(source_health_t *h, const char *name)
{
    h->consecutive_failures++;
    h->last_attempt = time(NULL);

    /* Exponential backoff: 2, 4, 8, 16, 32... capped at MAX_BACKOFF_SEC */
    h->backoff_sec = 2;
    for (int i = 1; i < h->consecutive_failures && h->backoff_sec < MAX_BACKOFF_SEC; i++)
        h->backoff_sec *= 2;
    if (h->backoff_sec > MAX_BACKOFF_SEC)
        h->backoff_sec = MAX_BACKOFF_SEC;

    MC_LOG_WARN("Source %s: %d consecutive failures, backoff %ds",
                name, h->consecutive_failures, h->backoff_sec);
}

static void sleep_interruptible(mc_scheduler_t *sched, int seconds)
{
    for (int s = 0; s < seconds && sched->running; s++) {
        if (sched->force_refresh) {
            sched->force_refresh = 0;
            break;
        }
        sleep(1);
    }
}

static int source_due(source_health_t *h, int interval_sec, int force)
{
    if (force) return 1;
    if (h->last_attempt == 0) return 1; /* Never fetched */
    time_t now = time(NULL);
    return (now - h->last_attempt >= interval_sec);
}

static void *rss_thread_func(void *arg)
{
    mc_scheduler_t *sched = arg;
    mc_news_item_t items[64];

    while (sched->running) {
        int any_fetched = 0;

        for (int i = 0; i < sched->cfg->rss_count && sched->running; i++) {
            const mc_rss_source_cfg_t *src = &sched->cfg->rss_sources[i];
            source_health_t *h = &sched->rss_health[i];

            if (should_skip_source(h, sched->force_refresh))
                continue;
            if (!source_due(h, src->refresh_interval_sec, sched->force_refresh))
                continue;

            int n = mc_fetch_rss(src, items, 64);
            if (n > 0) {
                for (int j = 0; j < n; j++)
                    mc_db_insert_news(sched->db, &items[j]);
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_RSS, NULL);
                record_success(h);
                any_fetched = 1;
            } else if (n < 0) {
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_RSS, "fetch failed");
                record_failure(h, src->name);
            }
        }

        if (any_fetched)
            update_snapshot(sched);

        /* Sleep 5s between checks (per-source intervals handle timing) */
        sleep_interruptible(sched, 5);
    }

    return NULL;
}

static void *rest_thread_func(void *arg)
{
    mc_scheduler_t *sched = arg;
    mc_data_entry_t entries[MAX_SNAPSHOT_ENTRIES];

    while (sched->running) {
        int any_fetched = 0;

        for (int i = 0; i < sched->cfg->rest_count && sched->running; i++) {
            const mc_rest_source_cfg_t *src = &sched->cfg->rest_sources[i];
            source_health_t *h = &sched->rest_health[i];

            if (should_skip_source(h, sched->force_refresh))
                continue;
            if (!source_due(h, src->refresh_interval_sec, sched->force_refresh))
                continue;

            int n = mc_fetch_rest(src, entries, MAX_SNAPSHOT_ENTRIES);
            if (n > 0) {
                for (int j = 0; j < n; j++)
                    mc_db_insert_entry(sched->db, &entries[j]);
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_REST, NULL);
                record_success(h);
                any_fetched = 1;
            } else if (n < 0) {
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_REST, "fetch failed");
                record_failure(h, src->name);
            }
        }

        if (any_fetched)
            update_snapshot(sched);

        /* Sleep 5s between checks (per-source intervals handle timing) */
        sleep_interruptible(sched, 5);
    }

    return NULL;
}

/* Background thread: prune old data + update snapshot periodically */
static void *prune_thread_func(void *arg)
{
    mc_scheduler_t *sched = arg;

    while (sched->running) {
        sleep_interruptible(sched, PRUNE_INTERVAL_SEC);
        if (!sched->running) break;

        mc_error_t err = mc_db_prune_old(sched->db, PRUNE_MAX_AGE_SEC);
        if (err == MC_OK)
            MC_LOG_INFO("DB pruned (entries older than %d hours removed)",
                        PRUNE_MAX_AGE_SEC / 3600);

        update_snapshot(sched);
    }
    return NULL;
}

mc_scheduler_t *mc_scheduler_create(const mc_config_t *cfg, mc_db_t *db)
{
    mc_scheduler_t *sched = calloc(1, sizeof(*sched));
    if (!sched) return NULL;

    sched->cfg = cfg;
    sched->db = db;
    pthread_rwlock_init(&sched->snapshot_lock, NULL);
    return sched;
}

/* Callback from WS threads when new data arrives (debounced) */
static void ws_data_callback(void *userdata)
{
    mc_scheduler_t *sched = userdata;
    time_t now = time(NULL);

    /* Debounce: update snapshot at most once per 2 seconds */
    if (now - sched->ws_last_snapshot >= 2) {
        sched->ws_last_snapshot = now;
        update_snapshot(sched);
    }
}

int mc_scheduler_start(mc_scheduler_t *sched)
{
    sched->running = 1;

    if (sched->cfg->rss_count > 0) {
        if (pthread_create(&sched->rss_thread, NULL, rss_thread_func, sched) == 0)
            sched->rss_thread_active = 1;
        else
            MC_LOG_ERROR("Failed to start RSS thread");
    }

    if (sched->cfg->rest_count > 0) {
        if (pthread_create(&sched->rest_thread, NULL, rest_thread_func, sched) == 0)
            sched->rest_thread_active = 1;
        else
            MC_LOG_ERROR("Failed to start REST thread");
    }

    /* Start WebSocket connections */
    for (int i = 0; i < sched->cfg->ws_count; i++) {
        mc_ws_conn_t *conn = mc_ws_connect(
            &sched->cfg->ws_sources[i], sched->db,
            ws_data_callback, sched);
        if (conn) {
            sched->ws_conns[sched->ws_count] = conn;
            sched->ws_count++;
        }
    }

    /* Start pruning thread */
    if (pthread_create(&sched->prune_thread, NULL, prune_thread_func, sched) == 0)
        sched->prune_thread_active = 1;

    MC_LOG_INFO("Scheduler started: %d RSS, %d REST, %d WS + pruning",
                sched->cfg->rss_count, sched->cfg->rest_count,
                sched->ws_count);
    return 0;
}

void mc_scheduler_stop(mc_scheduler_t *sched)
{
    if (!sched) return;
    sched->running = 0;

    if (sched->rss_thread_active)
        pthread_join(sched->rss_thread, NULL);
    if (sched->rest_thread_active)
        pthread_join(sched->rest_thread, NULL);
    if (sched->prune_thread_active)
        pthread_join(sched->prune_thread, NULL);

    for (int i = 0; i < sched->ws_count; i++)
        mc_ws_disconnect(sched->ws_conns[i]);

    MC_LOG_INFO("Scheduler stopped");
}

void mc_scheduler_destroy(mc_scheduler_t *sched)
{
    if (!sched) return;
    pthread_rwlock_destroy(&sched->snapshot_lock);
    free(sched);
}

void mc_scheduler_force_refresh(mc_scheduler_t *sched)
{
    if (sched) sched->force_refresh = 1;
}

int mc_scheduler_get_entries(mc_scheduler_t *sched,
                             mc_data_entry_t *out, int max_count)
{
    pthread_rwlock_rdlock(&sched->snapshot_lock);
    int n = sched->entry_count < max_count ? sched->entry_count : max_count;
    memcpy(out, sched->entries, n * sizeof(mc_data_entry_t));
    pthread_rwlock_unlock(&sched->snapshot_lock);
    return n;
}

int mc_scheduler_get_news(mc_scheduler_t *sched,
                          mc_news_item_t *out, int max_count)
{
    pthread_rwlock_rdlock(&sched->snapshot_lock);
    int n = sched->news_count < max_count ? sched->news_count : max_count;
    memcpy(out, sched->news, n * sizeof(mc_news_item_t));
    pthread_rwlock_unlock(&sched->snapshot_lock);
    return n;
}
