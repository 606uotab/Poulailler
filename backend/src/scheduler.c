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

#define MAX_SNAPSHOT_ENTRIES 2048
#define MAX_SNAPSHOT_NEWS   2048
#define PRUNE_INTERVAL_SEC  120   /* Prune DB every 2 minutes */
#define PRUNE_MAX_AGE_SEC   1800  /* Keep data for 30 minutes */
#define MAX_BACKOFF_SEC     300   /* Max retry backoff: 5 min */
#define REST_WORKER_COUNT   8     /* Parallel REST fetch workers */

/* Per-source tracking for retry backoff */
typedef struct {
    int  consecutive_failures;
    int  backoff_sec;
    time_t last_attempt;
    time_t last_success;
} source_health_t;

/* Job queue for REST worker pool */
typedef struct {
    int             indices[MC_MAX_SOURCES]; /* source indices to fetch */
    int             count;                   /* total jobs in batch */
    int             next;                    /* next job to claim */
    pthread_mutex_t mutex;
    pthread_cond_t  ready;                   /* signaled when jobs available */
} rest_queue_t;

struct mc_scheduler {
    const mc_config_t *cfg;
    mc_db_t           *db;

    /* Background threads */
    pthread_t          rss_thread;
    pthread_t          rest_dispatch_thread;
    pthread_t          rest_workers[REST_WORKER_COUNT];
    int                rest_worker_count;
    pthread_t          prune_thread;
    int                rss_thread_active;
    int                rest_dispatch_active;
    int                prune_thread_active;

    /* REST worker pool */
    rest_queue_t       rest_queue;
    int                rest_pending;         /* jobs still in progress */
    pthread_mutex_t    rest_done_mutex;
    pthread_cond_t     rest_done_cond;       /* signaled when batch complete */

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

    /* Global snapshot throttle */
    volatile time_t    last_snapshot_time;
    pthread_mutex_t    snapshot_throttle_mutex;
};

static double time_decay_factor(time_t published_at)
{
    if (published_at == 0) return 0.10;
    double age_h = difftime(time(NULL), published_at) / 3600.0;
    if (age_h < 1.0)  return 1.00;
    if (age_h < 3.0)  return 0.85;
    if (age_h < 6.0)  return 0.65;
    if (age_h < 12.0) return 0.45;
    if (age_h < 24.0) return 0.25;
    return 0.10;
}

static int cmp_news_score(const void *a, const void *b)
{
    const mc_news_item_t *na = a, *nb = b;
    if (nb->score != na->score)
        return (nb->score > na->score) ? 1 : -1;
    if (nb->published_at != na->published_at)
        return (nb->published_at > na->published_at) ? 1 : -1;
    return (nb->id > na->id) ? 1 : (nb->id < na->id) ? -1 : 0;
}

#define SNAPSHOT_THROTTLE_SEC 5

static void update_snapshot(mc_scheduler_t *sched)
{
    /* Global throttle: skip if called less than 5s ago */
    time_t now = time(NULL);
    pthread_mutex_lock(&sched->snapshot_throttle_mutex);
    if (now - sched->last_snapshot_time < SNAPSHOT_THROTTLE_SEC) {
        pthread_mutex_unlock(&sched->snapshot_throttle_mutex);
        return;
    }
    sched->last_snapshot_time = now;
    pthread_mutex_unlock(&sched->snapshot_throttle_mutex);

    /* Query DB into temp buffers WITHOUT holding the snapshot lock,
       so API readers are never blocked by slow DB queries */
    mc_data_entry_t *tmp_entries = malloc(MAX_SNAPSHOT_ENTRIES * sizeof(mc_data_entry_t));
    mc_news_item_t *tmp_news = malloc(MAX_SNAPSHOT_NEWS * sizeof(mc_news_item_t));
    if (!tmp_entries || !tmp_news) { free(tmp_entries); free(tmp_news); return; }

    int tmp_entry_count = 0;
    for (int cat = MC_CAT_CRYPTO; cat <= MC_CAT_CRYPTO_EXCHANGE; cat++) {
        int remaining = MAX_SNAPSHOT_ENTRIES - tmp_entry_count;
        if (remaining <= 0) break;
        int n = mc_db_get_latest_entries(sched->db, cat,
                    &tmp_entries[tmp_entry_count], remaining);
        tmp_entry_count += n;
    }

    int tmp_news_count = mc_db_get_all_latest_news(sched->db,
                            tmp_news, MAX_SNAPSHOT_NEWS);

    /* Apply time decay to news scores and sort by final score */
    for (int i = 0; i < tmp_news_count; i++)
        tmp_news[i].score *= time_decay_factor(tmp_news[i].published_at);
    if (tmp_news_count > 1)
        qsort(tmp_news, tmp_news_count, sizeof(mc_news_item_t), cmp_news_score);

    /* Hold write lock only for the fast memcpy */
    pthread_rwlock_wrlock(&sched->snapshot_lock);
    memcpy(sched->entries, tmp_entries, tmp_entry_count * sizeof(mc_data_entry_t));
    sched->entry_count = tmp_entry_count;
    memcpy(sched->news, tmp_news, tmp_news_count * sizeof(mc_news_item_t));
    sched->news_count = tmp_news_count;
    pthread_rwlock_unlock(&sched->snapshot_lock);

    free(tmp_entries);
    free(tmp_news);
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
            } else if (n == 0) {
                h->last_attempt = time(NULL);
            } else {
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

/* ── REST worker pool ── */

static void *rest_worker_func(void *arg)
{
    mc_scheduler_t *sched = arg;
    mc_data_entry_t *entries = malloc(MAX_SNAPSHOT_ENTRIES * sizeof(mc_data_entry_t));
    if (!entries) return NULL;

    while (sched->running) {
        /* Wait for jobs */
        pthread_mutex_lock(&sched->rest_queue.mutex);
        while (sched->rest_queue.next >= sched->rest_queue.count && sched->running)
            pthread_cond_wait(&sched->rest_queue.ready, &sched->rest_queue.mutex);

        if (!sched->running) {
            pthread_mutex_unlock(&sched->rest_queue.mutex);
            break;
        }

        /* Claim a job */
        int idx = sched->rest_queue.indices[sched->rest_queue.next++];
        pthread_mutex_unlock(&sched->rest_queue.mutex);

        /* Fetch */
        const mc_rest_source_cfg_t *src = &sched->cfg->rest_sources[idx];
        source_health_t *h = &sched->rest_health[idx];

        /* Calendar sources produce news items, not data entries */
        if (src->category == MC_CAT_FINANCIAL_NEWS) {
            mc_news_item_t *cal_news = malloc(256 * sizeof(mc_news_item_t));
            if (cal_news) {
                int n = mc_fetch_rest_calendar(src, cal_news, 256);
                MC_LOG_DEBUG("Calendar: %s returned %d events", src->name, n);
                if (n > 0) {
                    for (int j = 0; j < n; j++) {
                        mc_error_t err = mc_db_insert_news(sched->db, &cal_news[j]);
                        if (err != MC_OK)
                            MC_LOG_ERROR("Calendar insert failed for: %s", cal_news[j].title);
                    }
                    mc_db_update_source_status(sched->db, src->name,
                                               MC_SOURCE_REST, NULL);
                    record_success(h);
                } else if (n == 0) {
                    h->last_attempt = time(NULL);
                } else {
                    mc_db_update_source_status(sched->db, src->name,
                                               MC_SOURCE_REST, "fetch failed");
                    record_failure(h, src->name);
                }
                free(cal_news);
            }
        } else {
            int n = mc_fetch_rest(src, entries, MAX_SNAPSHOT_ENTRIES);
            if (n > 0) {
                for (int j = 0; j < n; j++)
                    mc_db_insert_entry(sched->db, &entries[j]);
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_REST, NULL);
                record_success(h);
            } else if (n == 0) {
                h->last_attempt = time(NULL);
            } else {
                mc_db_update_source_status(sched->db, src->name,
                                           MC_SOURCE_REST, "fetch failed");
                record_failure(h, src->name);
            }
        }

        /* Signal batch progress */
        pthread_mutex_lock(&sched->rest_done_mutex);
        sched->rest_pending--;
        if (sched->rest_pending == 0)
            pthread_cond_signal(&sched->rest_done_cond);
        pthread_mutex_unlock(&sched->rest_done_mutex);
    }

    free(entries);
    return NULL;
}

static void *rest_dispatch_func(void *arg)
{
    mc_scheduler_t *sched = arg;

    while (sched->running) {
        /* Build batch of due sources */
        pthread_mutex_lock(&sched->rest_queue.mutex);
        sched->rest_queue.count = 0;
        sched->rest_queue.next = 0;

        for (int i = 0; i < sched->cfg->rest_count; i++) {
            source_health_t *h = &sched->rest_health[i];
            const mc_rest_source_cfg_t *src = &sched->cfg->rest_sources[i];

            if (should_skip_source(h, sched->force_refresh))
                continue;
            if (!source_due(h, src->refresh_interval_sec, sched->force_refresh))
                continue;

            sched->rest_queue.indices[sched->rest_queue.count++] = i;
        }

        int batch_size = sched->rest_queue.count;
        pthread_mutex_unlock(&sched->rest_queue.mutex);

        if (batch_size > 0) {
            /* Set pending counter and wake workers */
            pthread_mutex_lock(&sched->rest_done_mutex);
            sched->rest_pending = batch_size;
            pthread_mutex_unlock(&sched->rest_done_mutex);

            pthread_mutex_lock(&sched->rest_queue.mutex);
            pthread_cond_broadcast(&sched->rest_queue.ready);
            pthread_mutex_unlock(&sched->rest_queue.mutex);

            /* Wait for all workers to finish */
            pthread_mutex_lock(&sched->rest_done_mutex);
            while (sched->rest_pending > 0 && sched->running) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 3;
                pthread_cond_timedwait(&sched->rest_done_cond,
                                       &sched->rest_done_mutex, &ts);
            }
            pthread_mutex_unlock(&sched->rest_done_mutex);

            update_snapshot(sched);
            MC_LOG_INFO("REST batch: %d sources fetched in parallel", batch_size);
        }

        sched->force_refresh = 0;
        sleep_interruptible(sched, 5);
    }

    /* Wake workers so they can exit */
    pthread_mutex_lock(&sched->rest_queue.mutex);
    pthread_cond_broadcast(&sched->rest_queue.ready);
    pthread_mutex_unlock(&sched->rest_queue.mutex);

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
    pthread_mutex_init(&sched->rest_queue.mutex, NULL);
    pthread_cond_init(&sched->rest_queue.ready, NULL);
    pthread_mutex_init(&sched->rest_done_mutex, NULL);
    pthread_cond_init(&sched->rest_done_cond, NULL);
    pthread_mutex_init(&sched->snapshot_throttle_mutex, NULL);
    return sched;
}

/* Callback from WS threads when new data arrives */
static void ws_data_callback(void *userdata)
{
    mc_scheduler_t *sched = userdata;
    update_snapshot(sched); /* global throttle handles debounce */
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
        /* Start worker pool */
        int nworkers = REST_WORKER_COUNT;
        if (nworkers > sched->cfg->rest_count)
            nworkers = sched->cfg->rest_count;

        for (int i = 0; i < nworkers; i++) {
            if (pthread_create(&sched->rest_workers[i], NULL,
                               rest_worker_func, sched) == 0)
                sched->rest_worker_count++;
            else
                MC_LOG_ERROR("Failed to start REST worker %d", i);
        }

        /* Start dispatcher */
        if (pthread_create(&sched->rest_dispatch_thread, NULL,
                           rest_dispatch_func, sched) == 0)
            sched->rest_dispatch_active = 1;
        else
            MC_LOG_ERROR("Failed to start REST dispatcher");

        MC_LOG_INFO("REST pool: %d workers for %d sources",
                    sched->rest_worker_count, sched->cfg->rest_count);
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

    MC_LOG_INFO("Scheduler started: %d RSS, %d REST (%d workers), %d WS + pruning",
                sched->cfg->rss_count, sched->cfg->rest_count,
                sched->rest_worker_count, sched->ws_count);
    return 0;
}

void mc_scheduler_stop(mc_scheduler_t *sched)
{
    if (!sched) return;
    sched->running = 0;

    /* Wake workers waiting on queue */
    pthread_mutex_lock(&sched->rest_queue.mutex);
    pthread_cond_broadcast(&sched->rest_queue.ready);
    pthread_mutex_unlock(&sched->rest_queue.mutex);

    /* Wake dispatcher waiting on batch done */
    pthread_mutex_lock(&sched->rest_done_mutex);
    pthread_cond_signal(&sched->rest_done_cond);
    pthread_mutex_unlock(&sched->rest_done_mutex);

    if (sched->rss_thread_active)
        pthread_join(sched->rss_thread, NULL);
    if (sched->rest_dispatch_active)
        pthread_join(sched->rest_dispatch_thread, NULL);
    for (int i = 0; i < sched->rest_worker_count; i++)
        pthread_join(sched->rest_workers[i], NULL);
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
    pthread_mutex_destroy(&sched->rest_queue.mutex);
    pthread_cond_destroy(&sched->rest_queue.ready);
    pthread_mutex_destroy(&sched->rest_done_mutex);
    pthread_cond_destroy(&sched->rest_done_cond);
    pthread_mutex_destroy(&sched->snapshot_throttle_mutex);
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
