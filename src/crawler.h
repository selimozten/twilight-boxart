#ifndef CRAWLER_H
#define CRAWLER_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_PATH_LEN    1024
#define MAX_LOG_LINES   512
#define MAX_LOG_LINE_LEN 256
#define ROM_HEADER_SIZE 512

// Systems mapped to LibRetro thumbnail directory names
typedef enum {
    SYS_UNKNOWN = 0,
    SYS_NES,
    SYS_SNES,
    SYS_GB,
    SYS_GBC,
    SYS_GBA,
    SYS_NDS,
    SYS_GG,
    SYS_GENESIS,
    SYS_SMS,
    SYS_FDS,
} RomSystem;

#define MAX_API_KEY_LEN 128

typedef struct {
    char sd_root[MAX_PATH_LEN];
    char boxart_path[MAX_PATH_LEN];
    int  boxart_width;
    int  boxart_height;
    bool keep_aspect_ratio;
    bool overwrite_existing;
    char sgdb_api_key[MAX_API_KEY_LEN]; // SteamGridDB API key (optional)
} CrawlerConfig;

typedef enum {
    CRAWL_IDLE = 0,
    CRAWL_RUNNING,
    CRAWL_STOPPING,
    CRAWL_DONE,
} CrawlState;

typedef struct {
    char lines[MAX_LOG_LINES][MAX_LOG_LINE_LEN];
    int  count;
    int  scroll_offset;
    pthread_mutex_t mutex;
} LogBuffer;

typedef struct {
    CrawlerConfig config;
    CrawlState    state;
    LogBuffer     log;
    int           files_found;
    int           files_downloaded;
    int           files_skipped;
    int           files_notfound;
    long          max_file_size;
} CrawlerContext;

void crawler_init(CrawlerContext *ctx);
void crawler_start(CrawlerContext *ctx);
void crawler_stop(CrawlerContext *ctx);
void crawler_log(CrawlerContext *ctx, const char *fmt, ...);
char *crawler_get_log_text(CrawlerContext *ctx); // caller must free()

RomSystem system_from_extension(const char *ext);
const char *libretro_system_name(RomSystem sys);
bool is_supported_extension(const char *ext);

#endif
