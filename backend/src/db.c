#include "mc_db.h"
#include "mc_log.h"

#include <sqlite3.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct mc_db {
    sqlite3        *handle;
    pthread_mutex_t mutex;
};

static const char *SCHEMA_SQL =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"

    "CREATE TABLE IF NOT EXISTS data_entries ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  source_name  TEXT NOT NULL,"
    "  source_type  INTEGER NOT NULL,"
    "  category     INTEGER NOT NULL,"
    "  symbol       TEXT NOT NULL,"
    "  display_name TEXT,"
    "  value        REAL,"
    "  currency     TEXT,"
    "  change_pct   REAL,"
    "  volume       REAL,"
    "  timestamp    INTEGER NOT NULL,"
    "  fetched_at   INTEGER NOT NULL"
    ");"

    "CREATE INDEX IF NOT EXISTS idx_entries_symbol ON data_entries(symbol);"
    "CREATE INDEX IF NOT EXISTS idx_entries_source ON data_entries(source_name);"
    "CREATE INDEX IF NOT EXISTS idx_entries_ts     ON data_entries(timestamp DESC);"

    "CREATE TABLE IF NOT EXISTS news_items ("
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  title        TEXT NOT NULL,"
    "  source       TEXT NOT NULL,"
    "  url          TEXT UNIQUE,"
    "  summary      TEXT,"
    "  category     INTEGER NOT NULL,"
    "  published_at INTEGER,"
    "  fetched_at   INTEGER NOT NULL"
    ");"

    "CREATE INDEX IF NOT EXISTS idx_news_pub ON news_items(published_at DESC);"
    "CREATE INDEX IF NOT EXISTS idx_news_src ON news_items(source);"

    "CREATE TABLE IF NOT EXISTS source_status ("
    "  source_name  TEXT PRIMARY KEY,"
    "  source_type  INTEGER NOT NULL,"
    "  last_fetched INTEGER,"
    "  last_error   TEXT,"
    "  error_count  INTEGER DEFAULT 0"
    ");";

mc_db_t *mc_db_open(const char *path)
{
    mc_db_t *db = calloc(1, sizeof(*db));
    if (!db) return NULL;

    pthread_mutex_init(&db->mutex, NULL);

    int rc = sqlite3_open(path, &db->handle);
    if (rc != SQLITE_OK) {
        MC_LOG_ERROR("Failed to open DB %s: %s", path, sqlite3_errmsg(db->handle));
        free(db);
        return NULL;
    }

    MC_LOG_INFO("Database opened: %s", path);
    return db;
}

void mc_db_close(mc_db_t *db)
{
    if (!db) return;
    pthread_mutex_lock(&db->mutex);
    sqlite3_close(db->handle);
    pthread_mutex_unlock(&db->mutex);
    pthread_mutex_destroy(&db->mutex);
    free(db);
}

mc_error_t mc_db_migrate(mc_db_t *db)
{
    pthread_mutex_lock(&db->mutex);
    char *err = NULL;
    int rc = sqlite3_exec(db->handle, SCHEMA_SQL, NULL, NULL, &err);
    pthread_mutex_unlock(&db->mutex);

    if (rc != SQLITE_OK) {
        MC_LOG_ERROR("Migration failed: %s", err ? err : "unknown");
        sqlite3_free(err);
        return MC_ERR_DB;
    }
    MC_LOG_INFO("Database migration complete");
    return MC_OK;
}

mc_error_t mc_db_insert_entry(mc_db_t *db, const mc_data_entry_t *e)
{
    const char *sql =
        "INSERT INTO data_entries "
        "(source_name,source_type,category,symbol,display_name,"
        "value,currency,change_pct,volume,timestamp,fetched_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?)";

    pthread_mutex_lock(&db->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        MC_LOG_ERROR("Prepare insert_entry: %s", sqlite3_errmsg(db->handle));
        return MC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, e->source_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, e->source_type);
    sqlite3_bind_int(stmt, 3, e->category);
    sqlite3_bind_text(stmt, 4, e->symbol, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, e->display_name, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, e->value);
    sqlite3_bind_text(stmt, 7, e->currency, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 8, e->change_pct);
    sqlite3_bind_double(stmt, 9, e->volume);
    sqlite3_bind_int64(stmt, 10, e->timestamp);
    sqlite3_bind_int64(stmt, 11, e->fetched_at);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);

    if (rc != SQLITE_DONE) {
        MC_LOG_ERROR("Insert entry failed: %s", sqlite3_errmsg(db->handle));
        return MC_ERR_DB;
    }
    return MC_OK;
}

mc_error_t mc_db_insert_news(mc_db_t *db, const mc_news_item_t *item)
{
    const char *sql =
        "INSERT OR IGNORE INTO news_items "
        "(title,source,url,summary,category,published_at,fetched_at) "
        "VALUES (?,?,?,?,?,?,?)";

    pthread_mutex_lock(&db->mutex);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        MC_LOG_ERROR("Prepare insert_news: %s", sqlite3_errmsg(db->handle));
        return MC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, item->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, item->source, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, item->url, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, item->summary, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, item->category);
    sqlite3_bind_int64(stmt, 6, item->published_at);
    sqlite3_bind_int64(stmt, 7, item->fetched_at);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);

    if (rc != SQLITE_DONE) {
        MC_LOG_ERROR("Insert news failed: %s", sqlite3_errmsg(db->handle));
        return MC_ERR_DB;
    }
    return MC_OK;
}

static int read_entries(sqlite3_stmt *stmt, mc_data_entry_t *out, int max_count)
{
    int count = 0;
    while (count < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        mc_data_entry_t *e = &out[count];
        memset(e, 0, sizeof(*e));

        e->id = sqlite3_column_int64(stmt, 0);

        const char *s;
        s = (const char *)sqlite3_column_text(stmt, 1);
        if (s) strncpy(e->source_name, s, MC_MAX_SOURCE - 1);

        e->source_type = sqlite3_column_int(stmt, 2);
        e->category = sqlite3_column_int(stmt, 3);

        s = (const char *)sqlite3_column_text(stmt, 4);
        if (s) strncpy(e->symbol, s, MC_MAX_SYMBOL - 1);

        s = (const char *)sqlite3_column_text(stmt, 5);
        if (s) strncpy(e->display_name, s, MC_MAX_NAME - 1);

        e->value = sqlite3_column_double(stmt, 6);

        s = (const char *)sqlite3_column_text(stmt, 7);
        if (s) strncpy(e->currency, s, MC_MAX_SYMBOL - 1);

        e->change_pct = sqlite3_column_double(stmt, 8);
        e->volume = sqlite3_column_double(stmt, 9);
        e->timestamp = sqlite3_column_int64(stmt, 10);
        e->fetched_at = sqlite3_column_int64(stmt, 11);

        count++;
    }
    return count;
}

int mc_db_get_latest_entries(mc_db_t *db, mc_category_t cat,
                             mc_data_entry_t *out, int max_count)
{
    /* Deduplicate: latest entry per (symbol, source_name) */
    const char *sql =
        "SELECT d.id,d.source_name,d.source_type,d.category,d.symbol,"
        "d.display_name,d.value,d.currency,d.change_pct,d.volume,"
        "d.timestamp,d.fetched_at "
        "FROM data_entries d "
        "INNER JOIN (SELECT symbol,source_name,MAX(fetched_at) AS max_fa "
        "  FROM data_entries WHERE category=? "
        "  GROUP BY symbol,source_name) g "
        "ON d.symbol=g.symbol AND d.source_name=g.source_name "
        "  AND d.fetched_at=g.max_fa "
        "ORDER BY d.symbol LIMIT ?";

    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, cat);
    sqlite3_bind_int(stmt, 2, max_count);

    int count = read_entries(stmt, out, max_count);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return count;
}

int mc_db_get_latest_news(mc_db_t *db, mc_category_t cat,
                          mc_news_item_t *out, int max_count)
{
    const char *sql =
        "SELECT id,title,source,url,summary,category,published_at,fetched_at "
        "FROM news_items WHERE category=? "
        "ORDER BY published_at DESC LIMIT ?";

    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return 0;
    }

    sqlite3_bind_int(stmt, 1, cat);
    sqlite3_bind_int(stmt, 2, max_count);

    int count = 0;
    while (count < max_count && sqlite3_step(stmt) == SQLITE_ROW) {
        mc_news_item_t *n = &out[count];
        memset(n, 0, sizeof(*n));

        n->id = sqlite3_column_int64(stmt, 0);

        const char *s;
        s = (const char *)sqlite3_column_text(stmt, 1);
        if (s) strncpy(n->title, s, MC_MAX_TITLE - 1);

        s = (const char *)sqlite3_column_text(stmt, 2);
        if (s) strncpy(n->source, s, MC_MAX_SOURCE - 1);

        s = (const char *)sqlite3_column_text(stmt, 3);
        if (s) strncpy(n->url, s, MC_MAX_URL - 1);

        s = (const char *)sqlite3_column_text(stmt, 4);
        if (s) strncpy(n->summary, s, MC_MAX_SUMMARY - 1);

        n->category = sqlite3_column_int(stmt, 5);
        n->published_at = sqlite3_column_int64(stmt, 6);
        n->fetched_at = sqlite3_column_int64(stmt, 7);

        count++;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return count;
}

int mc_db_get_entry_history(mc_db_t *db, const char *symbol,
                            mc_data_entry_t *out, int max_count)
{
    const char *sql =
        "SELECT id,source_name,source_type,category,symbol,display_name,"
        "value,currency,change_pct,volume,timestamp,fetched_at "
        "FROM data_entries WHERE symbol=? "
        "ORDER BY timestamp DESC LIMIT ?";

    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return 0;
    }

    sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_count);

    int count = read_entries(stmt, out, max_count);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return count;
}

mc_error_t mc_db_update_source_status(mc_db_t *db, const char *source_name,
                                      mc_source_type_t type, const char *error)
{
    const char *sql =
        "INSERT INTO source_status (source_name,source_type,last_fetched,last_error,error_count) "
        "VALUES (?,?,?,?,?) "
        "ON CONFLICT(source_name) DO UPDATE SET "
        "last_fetched=excluded.last_fetched,"
        "last_error=excluded.last_error,"
        "error_count=CASE WHEN excluded.last_error IS NULL THEN 0 "
        "ELSE source_status.error_count+1 END";

    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return MC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, source_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, type);
    sqlite3_bind_int64(stmt, 3, time(NULL));
    if (error)
        sqlite3_bind_text(stmt, 4, error, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int(stmt, 5, error ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);

    return (rc == SQLITE_DONE) ? MC_OK : MC_ERR_DB;
}

mc_error_t mc_db_prune_old(mc_db_t *db, int max_age_sec)
{
    time_t cutoff = time(NULL) - max_age_sec;

    pthread_mutex_lock(&db->mutex);
    char *err = NULL;
    char sql[256];

    snprintf(sql, sizeof(sql),
             "DELETE FROM data_entries WHERE fetched_at < %ld;"
             "DELETE FROM news_items WHERE fetched_at < %ld;",
             (long)cutoff, (long)cutoff);

    int rc = sqlite3_exec(db->handle, sql, NULL, NULL, &err);
    pthread_mutex_unlock(&db->mutex);

    if (rc != SQLITE_OK) {
        MC_LOG_ERROR("Prune failed: %s", err ? err : "unknown");
        sqlite3_free(err);
        return MC_ERR_DB;
    }
    return MC_OK;
}

int mc_db_count_entries(mc_db_t *db)
{
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db->handle,
            "SELECT COUNT(*) FROM data_entries", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db->mutex);
    return count;
}

int mc_db_count_news(mc_db_t *db)
{
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db->handle,
            "SELECT COUNT(*) FROM news_items", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db->mutex);
    return count;
}
