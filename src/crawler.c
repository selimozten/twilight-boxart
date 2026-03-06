#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#endif

#include "crawler.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <curl/curl.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

// LibRetro thumbnails base URL
#define LIBRETRO_THUMB_URL "https://thumbnails.libretro.com"
// GameTDB boxart (NDS/DSi)
#define GAMETDB_URL "https://art.gametdb.com/ds/coverS"

#define NDS_GAMECODE_OFFSET 0x0C
#define NDS_GAMECODE_LEN    4
#define NUM_WORKERS         8
#define MAX_WORK_ITEMS      4096
#define MAX_NAME_VARIANTS   16

// ---------- System mapping ----------

RomSystem system_from_extension(const char *ext) {
    if (!ext || ext[0] != '.') return SYS_UNKNOWN;
    char lower[16] = {0};
    for (int i = 0; ext[i] && i < 15; i++) lower[i] = tolower(ext[i]);

    if (strcmp(lower, ".nes") == 0)  return SYS_NES;
    if (strcmp(lower, ".fds") == 0)  return SYS_FDS;
    if (strcmp(lower, ".sfc") == 0 || strcmp(lower, ".smc") == 0 || strcmp(lower, ".snes") == 0) return SYS_SNES;
    if (strcmp(lower, ".gb") == 0 || strcmp(lower, ".sgb") == 0) return SYS_GB;
    if (strcmp(lower, ".gbc") == 0) return SYS_GBC;
    if (strcmp(lower, ".gba") == 0) return SYS_GBA;
    if (strcmp(lower, ".nds") == 0 || strcmp(lower, ".ds") == 0 || strcmp(lower, ".dsi") == 0) return SYS_NDS;
    if (strcmp(lower, ".gg") == 0)  return SYS_GG;
    if (strcmp(lower, ".gen") == 0) return SYS_GENESIS;
    if (strcmp(lower, ".sms") == 0) return SYS_SMS;
    return SYS_UNKNOWN;
}

const char *libretro_system_name(RomSystem sys) {
    switch (sys) {
        case SYS_NES:     return "Nintendo - Nintendo Entertainment System";
        case SYS_FDS:     return "Nintendo - Family Computer Disk System";
        case SYS_SNES:    return "Nintendo - Super Nintendo Entertainment System";
        case SYS_GB:      return "Nintendo - Game Boy";
        case SYS_GBC:     return "Nintendo - Game Boy Color";
        case SYS_GBA:     return "Nintendo - Game Boy Advance";
        case SYS_NDS:     return "Nintendo - Nintendo DS";
        case SYS_GG:      return "Sega - Game Gear";
        case SYS_GENESIS: return "Sega - Mega Drive - Genesis";
        case SYS_SMS:     return "Sega - Master System - Mark III";
        default:          return NULL;
    }
}

bool is_supported_extension(const char *ext) {
    return system_from_extension(ext) != SYS_UNKNOWN;
}

// ---------- Logging ----------

void crawler_init(CrawlerContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->config.boxart_width = 128;
    ctx->config.boxart_height = 115;
    ctx->config.keep_aspect_ratio = true;
    ctx->config.overwrite_existing = false;
    ctx->state = CRAWL_IDLE;
    pthread_mutex_init(&ctx->log.mutex, NULL);
    pthread_mutex_init(&ctx->last_dl_mutex, NULL);
}

void crawler_log(CrawlerContext *ctx, const char *fmt, ...) {
    pthread_mutex_lock(&ctx->log.mutex);
    if (ctx->log.count >= MAX_LOG_LINES) {
        memmove(ctx->log.lines[0], ctx->log.lines[1],
                (MAX_LOG_LINES - 1) * MAX_LOG_LINE_LEN);
        ctx->log.count = MAX_LOG_LINES - 1;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->log.lines[ctx->log.count], MAX_LOG_LINE_LEN, fmt, args);
    va_end(args);
    ctx->log.count++;
    ctx->log.scroll_offset = 0;
    pthread_mutex_unlock(&ctx->log.mutex);
}

char *crawler_get_log_text(CrawlerContext *ctx) {
    pthread_mutex_lock(&ctx->log.mutex);
    size_t total = 0;
    for (int i = 0; i < ctx->log.count; i++)
        total += strlen(ctx->log.lines[i]) + 1;
    char *out = malloc(total + 1);
    if (out) {
        out[0] = '\0';
        for (int i = 0; i < ctx->log.count; i++) {
            strcat(out, ctx->log.lines[i]);
            strcat(out, "\n");
        }
    }
    pthread_mutex_unlock(&ctx->log.mutex);
    return out;
}

// ---------- curl helpers ----------

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MemBuffer;

static size_t write_mem_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    MemBuffer *buf = (MemBuffer *)userdata;
    size_t total = size * nmemb;
    if (buf->size + total > buf->capacity) {
        size_t new_cap = (buf->capacity + total) * 2;
        unsigned char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    return total;
}

// Download using a persistent CURL handle (for connection reuse)
static long download_url_h(CURL *curl, const char *url, MemBuffer *buf) {
    buf->data = malloc(32768);
    buf->size = 0;
    buf->capacity = 32768;
    if (!buf->data) return 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    if (res != CURLE_OK) {
        free(buf->data);
        buf->data = NULL;
        buf->size = 0;
        return 0;
    }
    return http_code;
}

static CURL *create_curl_handle(void) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TwilightBoxart/2.0");
    // Enable connection reuse + HTTP/2 multiplexing
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    return curl;
}

// ---------- File helpers ----------

static const char *get_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}

static void strip_extension(const char *filename, char *buf, size_t buflen) {
    const char *dot = strrchr(filename, '.');
    size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, filename, len);
    buf[len] = '\0';
}

static bool extract_nds_gamecode(const char *rom_path, char *code_out) {
    FILE *f = fopen(rom_path, "rb");
    if (!f) return false;
    if (fseek(f, NDS_GAMECODE_OFFSET, SEEK_SET) != 0) { fclose(f); return false; }
    char code[NDS_GAMECODE_LEN + 1] = {0};
    if (fread(code, 1, NDS_GAMECODE_LEN, f) != NDS_GAMECODE_LEN) { fclose(f); return false; }
    fclose(f);
    for (int i = 0; i < NDS_GAMECODE_LEN; i++) {
        if (!isprint((unsigned char)code[i])) return false;
    }
    memcpy(code_out, code, NDS_GAMECODE_LEN + 1);
    return true;
}

static char *url_encode_h(CURL *curl, const char *str) {
    char *encoded = curl_easy_escape(curl, str, 0);
    char *result = strdup(encoded);
    curl_free(encoded);
    return result;
}

// ---------- Name cleaning for LibRetro matching ----------

typedef struct { const char *abbr; const char *full; } RegionMap;
static const RegionMap REGION_MAP[] = {
    {"(U)",  "(USA)"},    {"(E)",  "(Europe)"},  {"(J)",  "(Japan)"},
    {"(F)",  "(France)"}, {"(G)",  "(Germany)"}, {"(S)",  "(Spain)"},
    {"(I)",  "(Italy)"},  {"(K)",  "(Korea)"},   {"(W)",  "(World)"},
    {"(UE)", "(USA, Europe)"}, {"(JU)", "(Japan, USA)"},
    {"(US)", "(USA)"},    {"(EU)", "(Europe)"},
    {NULL, NULL}
};

// For each short code, extra full-region variants to try (No-Intro often uses multi-region)
typedef struct { const char *abbr; const char *alts[4]; } RegionAlts;
static const RegionAlts REGION_ALTS[] = {
    {"(U)",       {"(USA, Europe)", "(USA, Australia)", "(USA, Korea)", NULL}},
    {"(US)",      {"(USA, Europe)", "(USA, Australia)", NULL}},
    {"(USA)",     {"(USA, Europe)", "(USA, Australia)", "(USA, Korea)", NULL}},
    {"(E)",       {"(Europe, Australia)", "(USA, Europe)", NULL}},
    {"(Europe)",  {"(Europe, Australia)", "(USA, Europe)", NULL}},
    {"(J)",       {"(Japan, Korea)", "(Japan, USA)", NULL}},
    {"(Japan)",   {"(Japan, Korea)", "(Japan, USA)", NULL}},
    {"(W)",       {"(USA)", "(Europe)", "(Japan)", NULL}},
    {"(World)",   {"(USA)", "(Europe)", "(Japan)", NULL}},
    {NULL, {NULL}}
};

static void strip_num_prefix(const char *in, char *out, size_t out_len) {
    const char *p = in;
    while (*p && isdigit((unsigned char)*p)) p++;
    if (p > in && strncmp(p, " - ", 3) == 0)
        p += 3;
    else
        p = in;
    strncpy(out, p, out_len - 1);
    out[out_len - 1] = '\0';
}

static bool clean_region_tags(const char *in, char *out, size_t out_len) {
    const char *paren = in;
    while ((paren = strchr(paren, '(')) != NULL) {
        const char *close = strchr(paren, ')');
        if (!close) break;

        size_t tag_len = close - paren + 1;
        char tag[32] = {0};
        if (tag_len < sizeof(tag)) memcpy(tag, paren, tag_len);

        const char *expanded = NULL;
        for (int i = 0; REGION_MAP[i].abbr; i++) {
            if (strcasecmp(tag, REGION_MAP[i].abbr) == 0) {
                expanded = REGION_MAP[i].full;
                break;
            }
        }
        if (!expanded) {
            const char *known[] = {"(USA)", "(Europe)", "(Japan)", "(World)",
                "(USA, Europe)", "(Japan, USA)", "(France)", "(Germany)",
                "(Spain)", "(Italy)", "(Korea)", "(Australia)", NULL};
            for (int i = 0; known[i]; i++) {
                if (strcasecmp(tag, known[i]) == 0) { expanded = known[i]; break; }
            }
        }
        if (expanded) {
            size_t prefix_len = paren - in;
            if (prefix_len >= out_len) prefix_len = out_len - 1;
            memcpy(out, in, prefix_len);
            strncpy(out + prefix_len, expanded, out_len - prefix_len - 1);
            out[out_len - 1] = '\0';
            return true;
        }
        paren = close + 1;
    }
    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
    return false;
}

static void strip_all_tags(const char *in, char *out, size_t out_len) {
    size_t j = 0;
    bool in_paren = false;
    for (size_t i = 0; in[i] && j < out_len - 1; i++) {
        if (in[i] == '(') { in_paren = true; continue; }
        if (in[i] == ')') { in_paren = false; continue; }
        if (!in_paren) out[j++] = in[i];
    }
    while (j > 0 && out[j-1] == ' ') j--;
    out[j] = '\0';
}

static void libretro_sanitize(const char *in, char *out, size_t out_len) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_len - 1; i++) {
        char c = in[i];
        if (c == '&' || c == '*' || c == '/' || c == ':' ||
            c == '<' || c == '>' || c == '?' || c == '\\' || c == '|')
            out[j++] = '_';
        else
            out[j++] = c;
    }
    out[j] = '\0';
}

// Helper: add a variant if it's unique and there's room
static int add_variant(char variants[][256], int count, int max, const char *v) {
    if (count >= max || strlen(v) < 3) return count;
    for (int i = 0; i < count; i++) {
        if (strcmp(variants[i], v) == 0) return count; // duplicate
    }
    strncpy(variants[count], v, 255);
    variants[count][255] = '\0';
    return count + 1;
}

// Find the first region tag like (U) (USA) etc. in a name, return pointer to it
static const char *find_region_tag(const char *name) {
    const char *p = name;
    while ((p = strchr(p, '(')) != NULL) {
        const char *close = strchr(p, ')');
        if (!close) break;
        size_t tl = close - p + 1;
        char tag[32] = {0};
        if (tl < sizeof(tag)) memcpy(tag, p, tl);
        // Check if known region
        for (int i = 0; REGION_MAP[i].abbr; i++) {
            if (strcasecmp(tag, REGION_MAP[i].abbr) == 0) return p;
        }
        const char *known[] = {"(USA)", "(Europe)", "(Japan)", "(World)",
            "(USA, Europe)", "(Japan, USA)", "(France)", "(Germany)",
            "(Spain)", "(Italy)", "(Korea)", "(Australia)",
            "(USA, Australia)", "(USA, Korea)", "(Europe, Australia)", NULL};
        for (int i = 0; known[i]; i++) {
            if (strcasecmp(tag, known[i]) == 0) return p;
        }
        p = close + 1;
    }
    return NULL;
}

// Build "prefix + (region)" string, trimming trailing space before region
static void build_with_region(const char *name, const char *region_start,
                               const char *new_region, char *out, size_t out_len) {
    size_t prefix_len = region_start - name;
    // Trim trailing spaces in prefix
    while (prefix_len > 0 && name[prefix_len - 1] == ' ') prefix_len--;
    if (prefix_len >= out_len) prefix_len = out_len - 2;
    memcpy(out, name, prefix_len);
    out[prefix_len] = ' ';
    strncpy(out + prefix_len + 1, new_region, out_len - prefix_len - 2);
    out[out_len - 1] = '\0';
}

static int generate_name_variants(const char *filename, char variants[][256], int max) {
    int count = 0;
    char base[256];
    strip_extension(filename, base, sizeof(base));

    // Sanitize for LibRetro (& -> _ etc.)
    char sanitized[256];
    libretro_sanitize(base, sanitized, sizeof(sanitized));

    // Clean version: strip numeric prefix
    char clean[256];
    strip_num_prefix(sanitized, clean, sizeof(clean));

    // Work primarily with the clean (prefix-stripped) version
    // 1. Original clean name as-is
    count = add_variant(variants, count, max, clean);

    // 2. Expanded region: (U) -> (USA)
    char expanded[256];
    if (clean_region_tags(clean, expanded, sizeof(expanded)))
        count = add_variant(variants, count, max, expanded);

    // 3. Multi-region alternatives: (U) -> (USA, Europe) etc.
    const char *rtag = find_region_tag(clean);
    if (rtag) {
        const char *close = strchr(rtag, ')');
        if (close) {
            char tag[32] = {0};
            size_t tl = close - rtag + 1;
            if (tl < sizeof(tag)) memcpy(tag, rtag, tl);

            for (int i = 0; REGION_ALTS[i].abbr; i++) {
                if (strcasecmp(tag, REGION_ALTS[i].abbr) == 0) {
                    for (int j = 0; REGION_ALTS[i].alts[j]; j++) {
                        char alt[256];
                        build_with_region(clean, rtag, REGION_ALTS[i].alts[j], alt, sizeof(alt));
                        count = add_variant(variants, count, max, alt);
                    }
                    break;
                }
            }
        }
    }

    // 4. If no region tag found at all, try appending common regions
    if (!rtag) {
        char bare[256];
        strip_all_tags(clean, bare, sizeof(bare));
        size_t blen = strlen(bare);
        if (blen > 3) {
            const char *try_regions[] = {"(USA)", "(Europe)", "(Japan)",
                                          "(USA, Europe)", "(USA, Australia)", NULL};
            for (int i = 0; try_regions[i] && count < max; i++) {
                char with_region[256];
                snprintf(with_region, sizeof(with_region), "%s %s", bare, try_regions[i]);
                count = add_variant(variants, count, max, with_region);
            }
        }
    }

    // 5. Bare name (no tags at all)
    char bare[256];
    strip_all_tags(clean, bare, sizeof(bare));
    count = add_variant(variants, count, max, bare);

    // 6. If original had a prefix, also try it
    if (strcmp(sanitized, clean) != 0)
        count = add_variant(variants, count, max, sanitized);

    return count;
}

// ---------- Work queue ----------

typedef struct {
    char rom_path[MAX_PATH_LEN];
    char filename[256];
    char target_path[MAX_PATH_LEN];
} WorkItem;

typedef struct {
    WorkItem     items[MAX_WORK_ITEMS];
    int          count;
    int          next;           // next item to process (atomic-ish via mutex)
    pthread_mutex_t mutex;
} WorkQueue;

typedef struct {
    CrawlerContext *ctx;
    WorkQueue      *queue;
} WorkerArg;

// Try to download boxart with a persistent CURL handle
static bool try_download_boxart_h(CURL *curl, CrawlerContext *ctx,
                                   const char *rom_path, const char *filename,
                                   const char *target_path) {
    const char *ext = get_extension(filename);
    RomSystem sys = system_from_extension(ext);
    if (sys == SYS_UNKNOWN) return false;

    MemBuffer buf = {0};

    // Strategy 1: GameTDB for NDS (title ID from ROM header)
    if (sys == SYS_NDS) {
        char gamecode[NDS_GAMECODE_LEN + 1] = {0};
        if (extract_nds_gamecode(rom_path, gamecode)) {
            const char *regions[] = {"US", "EN", "JA", "FR", "DE", "ES", "IT", "KO", NULL};
            // Try HQ first (768x680), then medium (400x352), then regular (160x144)
            const char *patterns[] = {
                "https://art.gametdb.com/ds/coverHQ/%s/%s.jpg",
                "https://art.gametdb.com/ds/coverM/%s/%s.jpg",
                "https://art.gametdb.com/ds/cover/%s/%s.jpg",
                NULL
            };
            for (int p = 0; patterns[p]; p++) {
                for (int i = 0; regions[i]; i++) {
                    char url[512];
                    snprintf(url, sizeof(url), patterns[p], regions[i], gamecode);
                    long code = download_url_h(curl, url, &buf);
                    if (code == 200 && buf.size > 100) {
                        crawler_log(ctx, "OK: %s [GameTDB %s/%s]", filename, gamecode, regions[i]);
                        goto save_image;
                    }
                    free(buf.data);
                    buf = (MemBuffer){0};
                }
            }
        }
    }

    // Strategy 2: LibRetro Thumbnails with name variants
    {
        const char *sys_name = libretro_system_name(sys);
        if (sys_name) {
            char variants[MAX_NAME_VARIANTS][256];
            int nv = generate_name_variants(filename, variants, MAX_NAME_VARIANTS);

            for (int i = 0; i < nv; i++) {
                char *encoded_sys = url_encode_h(curl, sys_name);
                char *encoded_name = url_encode_h(curl, variants[i]);
                char url[1024];
                snprintf(url, sizeof(url), "%s/%s/Named_Boxarts/%s.png",
                         LIBRETRO_THUMB_URL, encoded_sys, encoded_name);
                free(encoded_sys);
                free(encoded_name);

                long code = download_url_h(curl, url, &buf);
                if (code == 200 && buf.size > 100) {
                    crawler_log(ctx, "OK: %s [LibRetro: %s]", filename, variants[i]);
                    goto save_image;
                }
                free(buf.data);
                buf = (MemBuffer){0};
            }
        }
    }

    // Strategy 3: SteamGridDB (if API key provided)
    if (ctx->config.sgdb_api_key[0] != '\0') {
        // Get a clean search name
        char search_name[256];
        char base_name[256];
        strip_extension(filename, base_name, sizeof(base_name));
        strip_num_prefix(base_name, search_name, sizeof(search_name));
        strip_all_tags(search_name, search_name, sizeof(search_name));

        if (strlen(search_name) > 2) {
            // Step 1: Search for the game
            char *encoded_q = url_encode_h(curl, search_name);
            char search_url[512];
            snprintf(search_url, sizeof(search_url),
                     "https://www.steamgriddb.com/api/v2/search/autocomplete/%s", encoded_q);
            free(encoded_q);

            // Set auth header
            char auth_header[256];
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", ctx->config.sgdb_api_key);
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Accept: application/json");
            char auth_full[280];
            snprintf(auth_full, sizeof(auth_full), "Authorization: %s", auth_header);
            headers = curl_slist_append(headers, auth_full);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            MemBuffer search_buf = {0};
            long scode = download_url_h(curl, search_url, &search_buf);

            int game_id = 0;
            if (scode == 200 && search_buf.data) {
                // Simple JSON parse: find first "id": <number>
                // Response: {"success":true,"data":[{"id":12345,"name":"..."},...]}
                char *id_pos = strstr((char *)search_buf.data, "\"id\":");
                if (id_pos) {
                    game_id = atoi(id_pos + 5);
                }
            }
            free(search_buf.data);

            if (game_id > 0) {
                // Step 2: Get grids for this game
                char grid_url[256];
                snprintf(grid_url, sizeof(grid_url),
                         "https://www.steamgriddb.com/api/v2/grids/game/%d", game_id);

                MemBuffer grid_buf = {0};
                long gcode = download_url_h(curl, grid_url, &grid_buf);

                char image_url[512] = {0};
                if (gcode == 200 && grid_buf.data) {
                    // Find first "url":"https://..." in response
                    char *url_pos = strstr((char *)grid_buf.data, "\"url\":\"");
                    if (url_pos) {
                        url_pos += 7; // skip "url":"
                        char *end = strchr(url_pos, '"');
                        if (end && (end - url_pos) < (int)sizeof(image_url)) {
                            memcpy(image_url, url_pos, end - url_pos);
                            image_url[end - url_pos] = '\0';
                        }
                    }
                }
                free(grid_buf.data);

                if (image_url[0]) {
                    // Step 3: Download the actual image
                    long icode = download_url_h(curl, image_url, &buf);
                    if (icode == 200 && buf.size > 100) {
                        crawler_log(ctx, "OK: %s [SteamGridDB #%d]", filename, game_id);
                        // Reset headers
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
                        curl_slist_free_all(headers);
                        goto save_image;
                    }
                    free(buf.data);
                    buf = (MemBuffer){0};
                }
            }

            // Reset headers for subsequent requests
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
            curl_slist_free_all(headers);
        }
    }

    return false;

save_image:
    {
        // Detect image format from magic bytes
        const char *fmt = ".png";
        if (buf.size >= 2 && buf.data[0] == 0xFF && buf.data[1] == 0xD8)
            fmt = ".jpg";

        // Decode image in memory
        Image img = LoadImageFromMemory(fmt, buf.data, (int)buf.size);
        free(buf.data);
        if (img.data == NULL) return false;

        // Save 32x32 custom icon for non-NDS systems only
        // (NDS ROMs have their own embedded icons in the ROM header)
        if (sys != SYS_NDS) {
            char icons_dir[MAX_PATH_LEN];
            snprintf(icons_dir, sizeof(icons_dir), "%s/_nds/TWiLightMenu/icons",
                     ctx->config.sd_root);
            mkdir(icons_dir, 0755);
            char icon_path[MAX_PATH_LEN];
            snprintf(icon_path, sizeof(icon_path), "%s/%s.png", icons_dir, filename);

            // Try LibRetro title screen, resize to 32x32 icon
            bool icon_saved = false;
            const char *sys_name = libretro_system_name(sys);
            if (sys_name) {
                char variants[MAX_NAME_VARIANTS][256];
                int nv = generate_name_variants(filename, variants, MAX_NAME_VARIANTS);
                for (int vi = 0; vi < nv && !icon_saved; vi++) {
                    char *enc_sys = url_encode_h(curl, sys_name);
                    char *enc_name = url_encode_h(curl, variants[vi]);
                    char url[1024];
                    snprintf(url, sizeof(url), "%s/%s/Named_Titles/%s.png",
                             LIBRETRO_THUMB_URL, enc_sys, enc_name);
                    free(enc_sys);
                    free(enc_name);

                    MemBuffer icon_buf = {0};
                    long code = download_url_h(curl, url, &icon_buf);
                    if (code == 200 && icon_buf.size > 100) {
                        Image icon_img = LoadImageFromMemory(".png", icon_buf.data, (int)icon_buf.size);
                        if (icon_img.data) {
                            ImageResize(&icon_img, 32, 32);
                            ExportImage(icon_img, icon_path);
                            UnloadImage(icon_img);
                            icon_saved = true;
                        }
                        free(icon_buf.data);
                    } else {
                        free(icon_buf.data);
                    }
                }
            }

            // Fallback: resize boxart with raylib
            if (!icon_saved) {
                Image icon = ImageCopy(img);
                ImageResize(&icon, 32, 32);
                ExportImage(icon, icon_path);
                UnloadImage(icon);
            }
        }

        // Resize to target dimensions (DSi has limited RAM)
        int tw = ctx->config.boxart_width;
        int th = ctx->config.boxart_height;
        if (tw > 0 && th > 0 && (img.width > tw || img.height > th)) {
            if (ctx->config.keep_aspect_ratio) {
                float scale_w = (float)tw / img.width;
                float scale_h = (float)th / img.height;
                float scale = scale_w < scale_h ? scale_w : scale_h;
                int nw = (int)(img.width * scale);
                int nh = (int)(img.height * scale);
                if (nw < 1) nw = 1;
                if (nh < 1) nh = 1;
                ImageResize(&img, nw, nh);
            } else {
                ImageResize(&img, tw, th);
            }
        }

        // Save as PNG
        bool ok = ExportImage(img, target_path);
        UnloadImage(img);

        // Track max file size for TwilightMenu++ settings fix
        if (ok) {
            struct stat fst;
            if (stat(target_path, &fst) == 0) {
                long size = (long)fst.st_size;
                long old;
                do {
                    old = ctx->max_file_size;
                    if (size <= old) break;
                } while (!__sync_bool_compare_and_swap(&ctx->max_file_size, old, size));
            }
        }

        return ok;
    }
}

// Worker thread: pulls jobs from the queue and downloads
static void *worker_thread(void *arg) {
    WorkerArg *wa = (WorkerArg *)arg;
    CrawlerContext *ctx = wa->ctx;
    WorkQueue *q = wa->queue;

    CURL *curl = create_curl_handle();
    if (!curl) return NULL;

    while (ctx->state == CRAWL_RUNNING) {
        // Grab next work item
        pthread_mutex_lock(&q->mutex);
        int idx = q->next;
        if (idx < q->count) q->next++;
        pthread_mutex_unlock(&q->mutex);

        if (idx >= q->count) break; // no more work

        WorkItem *item = &q->items[idx];

        if (try_download_boxart_h(curl, ctx, item->rom_path, item->filename, item->target_path)) {
            __sync_fetch_and_add(&ctx->files_downloaded, 1);
            pthread_mutex_lock(&ctx->last_dl_mutex);
            strncpy(ctx->last_downloaded, item->target_path, MAX_PATH_LEN - 1);
            pthread_mutex_unlock(&ctx->last_dl_mutex);
        } else {
            crawler_log(ctx, "MISS: %s", item->filename);
            __sync_fetch_and_add(&ctx->files_notfound, 1);
        }
    }

    curl_easy_cleanup(curl);
    free(wa);
    return NULL;
}

// ---------- Directory scanning (fast, no network) ----------

// Directories to skip (TwilightMenu system folders, etc.)
static bool is_system_dir(const char *name) {
    if (name[0] == '_') return true;  // _nds, _rpg, __rpg, etc.
    if (strcmp(name, "BIOS") == 0) return true;
    if (strcmp(name, "gm9") == 0) return true;
    if (strcmp(name, "luma") == 0) return true;
    if (strcmp(name, "Nintendo 3DS") == 0) return true;
    if (strcmp(name, "private") == 0) return true;
    return false;
}

static void scan_directory_collect(CrawlerContext *ctx, WorkQueue *q, const char *dir_path) {
    if (ctx->state == CRAWL_STOPPING) return;

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (ctx->state == CRAWL_STOPPING) break;
        if (entry->d_name[0] == '.') continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!is_system_dir(entry->d_name))
                scan_directory_collect(ctx, q, full_path);
            continue;
        }

        const char *ext = get_extension(entry->d_name);
        if (!is_supported_extension(ext)) continue;

        // Skip known non-game files (homebrew tools, system files, etc.)
        {
            const char *name = entry->d_name;
            // Skip files with no region/scene tag that are likely homebrew
            // Common patterns: all-caps short names, known tools
            const char *skip_names[] = {
                "BOOT.NDS", "UNLAUNCH.DSI", "dumpTool.nds",
                "vnds.nds", "pictochat.nds", "dlplay.nds",
                NULL
            };
            bool skip = false;
            for (int i = 0; skip_names[i]; i++) {
                if (strcasecmp(name, skip_names[i]) == 0) { skip = true; break; }
            }
            // Skip NDS homebrew: files ending in .nds with names that look like
            // tool names (no spaces, no parentheses, short)
            if (!skip && (strcasecmp(ext, ".nds") == 0 || strcasecmp(ext, ".dsi") == 0)) {
                bool has_paren = (strchr(name, '(') != NULL);
                bool has_dash_space = (strstr(name, " - ") != NULL);
                size_t name_len = strlen(name);
                // Likely homebrew: no region tag, no "Name - Subtitle" pattern, short name
                if (!has_paren && !has_dash_space && name_len < 30) skip = true;
            }
            if (skip) {
                __sync_fetch_and_add(&ctx->files_found, 1);
                __sync_fetch_and_add(&ctx->files_skipped, 1);
                continue;
            }
        }

        // Check if already exists
        char target_path[MAX_PATH_LEN];
        snprintf(target_path, sizeof(target_path), "%s/%s.png",
                 ctx->config.boxart_path, entry->d_name);

        if (!ctx->config.overwrite_existing) {
            struct stat ts;
            if (stat(target_path, &ts) == 0) {
                crawler_log(ctx, "Skip: %s (exists)", entry->d_name);
                __sync_fetch_and_add(&ctx->files_skipped, 1);
                __sync_fetch_and_add(&ctx->files_found, 1);
                continue;
            }
        }

        if (q->count >= MAX_WORK_ITEMS) {
            crawler_log(ctx, "Warning: too many ROMs (max %d), skipping rest", MAX_WORK_ITEMS);
            break;
        }

        __sync_fetch_and_add(&ctx->files_found, 1);

        WorkItem *item = &q->items[q->count++];
        strncpy(item->rom_path, full_path, MAX_PATH_LEN - 1);
        strncpy(item->filename, entry->d_name, 255);
        strncpy(item->target_path, target_path, MAX_PATH_LEN - 1);
    }
    closedir(d);
}

static void ensure_dir(const char *path) {
    char tmp[MAX_PATH_LEN];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

#define MAX_CACHE_SIZE_TWILIGHT 43000

static void fix_twilight_settings(CrawlerContext *ctx, bool enable_icons) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/_nds/TWiLightMenu/settings.ini", ctx->config.sd_root);

    FILE *f = fopen(path, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 65536) { fclose(f); return; }

    char *content = malloc(fsize + 1);
    if (!content) { fclose(f); return; }
    fread(content, 1, fsize, f);
    content[fsize] = '\0';
    fclose(f);

    bool changed = false;

    // Enable custom icons (for GBA/SNES/GB etc. that lack ROM header icons)
    if (enable_icons) {
        char *pos = strstr(content, "SHOW_CUSTOM_ICONS = 0");
        if (pos) {
            pos[strlen("SHOW_CUSTOM_ICONS = ")] = '1';
            crawler_log(ctx, "Patching TwilightMenu++ settings: enabled custom icons");
            changed = true;
        }
    }

    // Disable boxart cache if images are large (legacy setting)
    {
        char *pos = strstr(content, "CACHE_BOX_ART = 1");
        if (pos) {
            pos[strlen("CACHE_BOX_ART = ")] = '0';
            crawler_log(ctx, "Patching TwilightMenu++ settings: disabled boxart cache");
            changed = true;
        }
    }

    if (changed) {
        f = fopen(path, "w");
        if (f) {
            fwrite(content, 1, fsize, f);
            fclose(f);
        }
    }
    free(content);
}

static void *crawler_thread(void *arg) {
    CrawlerContext *ctx = (CrawlerContext *)arg;
    ctx->files_found = 0;
    ctx->files_downloaded = 0;
    ctx->files_skipped = 0;
    ctx->files_notfound = 0;
    ctx->max_file_size = 0;

    crawler_log(ctx, "Scanning: %s", ctx->config.sd_root);
    crawler_log(ctx, "Output:   %s", ctx->config.boxart_path);
    crawler_log(ctx, "Workers:  %d parallel threads", NUM_WORKERS);
    crawler_log(ctx, "Sources:  GameTDB (NDS), LibRetro Thumbnails%s",
                ctx->config.sgdb_api_key[0] ? ", SteamGridDB" : "");
    crawler_log(ctx, "---");

    ensure_dir(ctx->config.boxart_path);

    // Phase 1: Fast filesystem scan to build work queue
    WorkQueue *queue = calloc(1, sizeof(WorkQueue));
    pthread_mutex_init(&queue->mutex, NULL);
    queue->next = 0;

    scan_directory_collect(ctx, queue, ctx->config.sd_root);

    int to_download = queue->count;
    ctx->files_to_download = to_download;
    crawler_log(ctx, "Found %d ROMs, %d to download, %d skipped",
                ctx->files_found, to_download, ctx->files_skipped);
    crawler_log(ctx, "---");

    if (to_download > 0) {
        // Phase 2: Spawn worker threads for parallel downloads
        int num_workers = to_download < NUM_WORKERS ? to_download : NUM_WORKERS;
        pthread_t workers[NUM_WORKERS];

        for (int i = 0; i < num_workers; i++) {
            WorkerArg *wa = malloc(sizeof(WorkerArg));
            wa->ctx = ctx;
            wa->queue = queue;
            pthread_create(&workers[i], NULL, worker_thread, wa);
        }

        for (int i = 0; i < num_workers; i++) {
            pthread_join(workers[i], NULL);
        }
    }

    pthread_mutex_destroy(&queue->mutex);
    free(queue);

    // Phase 3: Generate missing custom icons from LibRetro title screens or boxart
    {
        char icons_dir[MAX_PATH_LEN];
        snprintf(icons_dir, sizeof(icons_dir), "%s/_nds/TWiLightMenu/icons",
                 ctx->config.sd_root);
        mkdir(icons_dir, 0755);

        CURL *icon_curl = create_curl_handle();
        DIR *bd = opendir(ctx->config.boxart_path);
        int icons_created = 0;
        if (bd) {
            struct dirent *be;
            while ((be = readdir(bd)) != NULL && ctx->state != CRAWL_STOPPING) {
                if (be->d_name[0] == '.') continue;
                const char *name = be->d_name;
                size_t nlen = strlen(name);
                if (nlen < 5) continue;
                if (strcasecmp(name + nlen - 4, ".png") != 0) continue;

                // Check if icon already exists
                char icon_path[MAX_PATH_LEN];
                snprintf(icon_path, sizeof(icon_path), "%s/%s", icons_dir, name);
                struct stat ist;
                if (stat(icon_path, &ist) == 0) continue;

                // Extract ROM extension before .png
                const char *pre_png = name + nlen - 4;
                const char *rom_ext = NULL;
                for (const char *p = pre_png - 1; p > name && p > pre_png - 6; p--) {
                    if (*p == '.') { rom_ext = p; break; }
                }
                if (!rom_ext) continue;
                char ext_buf[8] = {0};
                size_t ext_len = pre_png - rom_ext;
                if (ext_len >= sizeof(ext_buf)) continue;
                memcpy(ext_buf, rom_ext, ext_len);
                RomSystem sys = system_from_extension(ext_buf);
                if (sys == SYS_UNKNOWN || sys == SYS_NDS) continue;

                // ROM filename = boxart name minus trailing ".png"
                char rom_filename[256];
                size_t rflen = nlen - 4;
                if (rflen >= sizeof(rom_filename)) rflen = sizeof(rom_filename) - 1;
                memcpy(rom_filename, name, rflen);
                rom_filename[rflen] = '\0';

                // Try LibRetro title screen
                bool icon_saved = false;
                const char *sys_name = libretro_system_name(sys);
                if (sys_name && icon_curl) {
                    char variants[MAX_NAME_VARIANTS][256];
                    int nv = generate_name_variants(rom_filename, variants, MAX_NAME_VARIANTS);
                    for (int vi = 0; vi < nv && !icon_saved; vi++) {
                        char *enc_sys = url_encode_h(icon_curl, sys_name);
                        char *enc_name = url_encode_h(icon_curl, variants[vi]);
                        char url[1024];
                        snprintf(url, sizeof(url), "%s/%s/Named_Titles/%s.png",
                                 LIBRETRO_THUMB_URL, enc_sys, enc_name);
                        free(enc_sys);
                        free(enc_name);

                        MemBuffer ibuf = {0};
                        long code = download_url_h(icon_curl, url, &ibuf);
                        if (code == 200 && ibuf.size > 100) {
                            Image icon_img = LoadImageFromMemory(".png", ibuf.data, (int)ibuf.size);
                            if (icon_img.data) {
                                ImageResize(&icon_img, 32, 32);
                                ExportImage(icon_img, icon_path);
                                UnloadImage(icon_img);
                                icon_saved = true;
                            }
                            free(ibuf.data);
                        } else {
                            free(ibuf.data);
                        }
                    }
                }

                // Fallback: resize existing boxart
                if (!icon_saved) {
                    char boxart_path[MAX_PATH_LEN];
                    snprintf(boxart_path, sizeof(boxart_path), "%s/%s",
                             ctx->config.boxart_path, name);
                    Image img = LoadImage(boxart_path);
                    if (img.data) {
                        ImageResize(&img, 32, 32);
                        ExportImage(img, icon_path);
                        UnloadImage(img);
                        icon_saved = true;
                    }
                }

                if (icon_saved) icons_created++;
            }
            closedir(bd);
        }
        if (icon_curl) curl_easy_cleanup(icon_curl);
        if (icons_created > 0) {
            crawler_log(ctx, "Created %d custom icons (title screens from LibRetro)", icons_created);
        }
    }

    // Fix TwilightMenu++ settings: enable custom icons, disable cache if needed
    {
        bool large_files = ctx->max_file_size >= MAX_CACHE_SIZE_TWILIGHT;
        fix_twilight_settings(ctx, true);
        if (large_files) {
            crawler_log(ctx, "Note: large boxart detected (max %ld bytes)", ctx->max_file_size);
        }
    }

    crawler_log(ctx, "---");
    crawler_log(ctx, "Done! Found: %d  Downloaded: %d  Skipped: %d  Not found: %d",
                ctx->files_found, ctx->files_downloaded,
                ctx->files_skipped, ctx->files_notfound);
    if (ctx->files_downloaded > 0) {
        crawler_log(ctx, "Images resized to %dx%d%s",
                    ctx->config.boxart_width, ctx->config.boxart_height,
                    ctx->config.keep_aspect_ratio ? " (aspect ratio preserved)" : "");
    }

    ctx->state = CRAWL_DONE;
    return NULL;
}

void crawler_start(CrawlerContext *ctx) {
    if (ctx->state == CRAWL_RUNNING) return;

    struct stat st;
    if (stat(ctx->config.sd_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        crawler_log(ctx, "Error: '%s' is not a valid directory.", ctx->config.sd_root);
        return;
    }

    ctx->state = CRAWL_RUNNING;

    pthread_t thread;
    pthread_create(&thread, NULL, crawler_thread, ctx);
    pthread_detach(thread);
}

void crawler_stop(CrawlerContext *ctx) {
    if (ctx->state == CRAWL_RUNNING) {
        ctx->state = CRAWL_STOPPING;
        crawler_log(ctx, "Stopping...");
    }
}
