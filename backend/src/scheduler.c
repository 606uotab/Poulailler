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

#define MAX_SNAPSHOT_ENTRIES 256
#define MAX_SNAPSHOT_NEWS   256

struct mc_scheduler {
    const mc_config_t *cfg;
    mc_db_t           *db;

    /* Background threads */
    pthread_t          rss_thread;
    pthread_t          rest_thread;
    int                rss_thread_active;
    int                rest_thread_active;

    /* WebSocket connections */
    mc_ws_conn_t      *ws_conns[MC_MAX_SOURCES];
    int                ws_count;

    /* Shared snapshot for API consumers */
    pthread_rwlock_t   snapshot_lock;
    mc_data_entry_t    entries[MAX_SNAPSHOT_ENTRIES];
    int                entry_count;
    mc_news_item_t     news[MAX_SNAPSHOT_NEWS];
    int                news_count;

    volatile int       running;
    volatile int       force_refresh;
};

static void update_snapshot(mc_scheduler_t *sched)
{
    pthread_rwlock_wrlock(&sched->snapshot_lock);

    /* Refresh entries from DB for each category */
    sched->entry_count = 0;
    for (int cat = MC_CAT_CRYPTO; cat <= MC_CAT_CUSTOM; cat++) {
        int remaining = MAX_SNAPSHOT_ENTRIES - sched->entry_count;
        if (remaining <= 0) break;
        int n = mc_db_get_latest_entries(sched->db, cat,
                    &sched->entries[sched->entry_count], remaining);
        sched->entry_count += n;
    }

    /* Refresh news */
    sched->news_count = 0;
    for (int cat = MC_CAT_CRYPTO; cat <= MC_CAT_CUSTOM; cat++) {
        int remaining = MAX_SNAPSHOT_NEWS - sched->news_count;
        if (remaining <= 0) break;
        int n = mc_db_get_latest_news(sched->db, cat,
                    &sched->news[sched->news_count], remaining);
        sched->news_count += n;
    }

    pthread_rwlock_unlock(&sched->snapshot_lock);
}

static void *rss_thread_func(void *arg)
{
    mc_scheduler_t *sched = arg;
    mc_news_item_t items[64];

    while (sched->running) {
        for (int i = 0; i < sched->cfg->rss_count && sched->running; i++) {
            const mc_rss_source_cfg_t *src = &sched->cfg->rss_sources[i];

            int n = mc_fetch_rss(src, items, 64);
            if (n > 0) {
                for (int j = 0; j < n; j++)
                    mc_db_insert_news(sched->db, &items[j]);
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_RSS, NULL);
            } else if (n < 0) {
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_RSS, "fetch failed");
            }
        }

        update_snapshot(sched);

        /* Sleep in small increments so we can stop quickly */
        int sleep_sec = sched->cfg->refresh_interval_sec;
        for (int s = 0; s < sleep_sec && sched->running; s++) {
            if (sched->force_refresh) {
                sched->force_refresh = 0;
                break;
            }
            sleep(1);
        }
    }

    return NULL;
}

static void *rest_thread_func(void *arg)
{
    mc_scheduler_t *sched = arg;
    mc_data_entry_t entries[64];

    while (sched->running) {
        for (int i = 0; i < sched->cfg->rest_count && sched->running; i++) {
            const mc_rest_source_cfg_t *src = &sched->cfg->rest_sources[i];

            int n = mc_fetch_rest(src, entries, 64);
            if (n > 0) {
                for (int j = 0; j < n; j++)
                    mc_db_insert_entry(sched->db, &entries[j]);
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_REST, NULL);
            } else if (n < 0) {
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_REST, "fetch failed");
            }
        }

        update_snapshot(sched);

        int sleep_sec = sched->cfg->refresh_interval_sec;
        for (int s = 0; s < sleep_sec && sched->running; s++) {
            if (sched->force_refresh) {
                sched->force_refresh = 0;
                break;
            }
            sleep(1);
        }
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

int mc_scheduler_start(mc_scheduler_t *sched)
{
    sched->running = 1;

    /* Start RSS thread */
    if (sched->cfg->rss_count > 0) {
        if (pthread_create(&sched->rss_thread, NULL, rss_thread_func, sched) == 0)
            sched->rss_thread_active = 1;
        else
            MC_LOG_ERROR("Failed to start RSS thread");
    }

    /* Start REST thread */
    if (sched->cfg->rest_count > 0) {
        if (pthread_create(&sched->rest_thread, NULL, rest_thread_func, sched) == 0)
            sched->rest_thread_active = 1;
        else
            MC_LOG_ERROR("Failed to start REST thread");
    }

    /* Start WebSocket connections */
    for (int i = 0; i < sched->cfg->ws_count; i++) {
        sched->ws_conns[sched->ws_count] = mc_ws_connect(
            &sched->cfg->ws_sources[i], sched->db);
        if (sched->ws_conns[sched->ws_count])
            sched->ws_count++;
    }

    MC_LOG_INFO("Scheduler started: %d RSS, %d REST, %d WS",
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
