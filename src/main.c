#include "crawler.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#define WINDOW_W   780
#define WINDOW_H   660
#define PAD        20
#define SMALL_FONT 14
#define FIELD_H    30
#define BTN_H      34
#define BROWSE_W   80
#define SECTION_GAP 14

// Color scheme
#define BG_COLOR        CLITERAL(Color){30, 30, 38, 255}
#define ACCENT_COLOR    CLITERAL(Color){100, 149, 237, 255}
#define ACCENT_HOVER    CLITERAL(Color){130, 170, 255, 255}
#define ACCENT_DIM      CLITERAL(Color){70, 105, 180, 255}
#define TEXT_COLOR       WHITE
#define DIM_TEXT        CLITERAL(Color){140, 140, 160, 255}
#define FIELD_BG        CLITERAL(Color){22, 22, 30, 255}
#define FIELD_BORDER    CLITERAL(Color){60, 60, 80, 255}
#define LOG_BG          CLITERAL(Color){16, 16, 22, 255}
#define GREEN_OK        CLITERAL(Color){80, 200, 120, 255}
#define RED_ERR         CLITERAL(Color){220, 80, 80, 255}
#define ORANGE_WARN     CLITERAL(Color){230, 160, 50, 255}
#define DISABLED_BG     CLITERAL(Color){50, 50, 60, 255}
#define BTN_DISABLED    CLITERAL(Color){55, 55, 65, 255}

typedef struct { const char *label; int w; int h; } SizePreset;
static const SizePreset SIZE_PRESETS[] = {
    {"Default (128x115)", 128, 115},
    {"Large (168x130)",   168, 130},
    {"Full (208x143)",    208, 143},
};
#define PRESET_COUNT 3

// ---------- Native macOS folder picker via osascript ----------

static bool open_folder_dialog(const char *prompt, char *out, size_t out_len) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "osascript -e 'set f to POSIX path of (choose folder with prompt \"%s\")' 2>/dev/null",
        prompt);

    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    char buf[MAX_PATH_LEN] = {0};
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        pclose(fp);
        return false;
    }
    int status = pclose(fp);
    if (status != 0) return false;

    // Strip trailing newline and slash
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    while (len > 1 && buf[len-1] == '/') buf[--len] = '\0';

    if (len == 0) return false;
    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

// ---------- SD card auto-detection ----------

typedef struct {
    char path[MAX_PATH_LEN];
    char label[64];
    bool has_twilight; // has _nds directory
} DetectedDrive;

static int detect_sd_cards(DetectedDrive *drives, int max_drives) {
    int count = 0;
    DIR *volumes = opendir("/Volumes");
    if (!volumes) return 0;

    struct dirent *entry;
    while ((entry = readdir(volumes)) != NULL && count < max_drives) {
        if (entry->d_name[0] == '.') continue;

        char vol_path[MAX_PATH_LEN];
        snprintf(vol_path, sizeof(vol_path), "/Volumes/%s", entry->d_name);

        struct stat st;
        if (stat(vol_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Check for _nds directory (TwilightMenu++ indicator)
        char nds_path[MAX_PATH_LEN];
        snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
        bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

        // Skip the main macOS volume
        if (strcmp(entry->d_name, "Macintosh HD") == 0 ||
            strcmp(entry->d_name, "Macintosh HD - Data") == 0) continue;

        strncpy(drives[count].path, vol_path, MAX_PATH_LEN - 1);
        strncpy(drives[count].label, entry->d_name, 63);
        drives[count].has_twilight = has_nds;
        count++;
    }
    closedir(volumes);
    return count;
}

// ---------- Text field ----------

typedef struct {
    char text[MAX_PATH_LEN];
    bool active;
    int  cursor;
    int  scroll_x; // horizontal scroll offset in pixels
} TextField;

static void textfield_set(TextField *tf, const char *text) {
    strncpy(tf->text, text, MAX_PATH_LEN - 1);
    tf->text[MAX_PATH_LEN - 1] = '\0';
    tf->cursor = (int)strlen(tf->text);
    tf->scroll_x = 0;
}

static void textfield_handle(TextField *tf, Rectangle bounds) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        tf->active = CheckCollisionPointRec(GetMousePosition(), bounds);
        if (tf->active) {
            // Click to position cursor
            int mx = (int)(GetMousePosition().x - bounds.x - 6) + tf->scroll_x;
            int best = 0;
            int best_dist = 99999;
            int len = (int)strlen(tf->text);
            for (int i = 0; i <= len; i++) {
                char tmp[MAX_PATH_LEN];
                memcpy(tmp, tf->text, i);
                tmp[i] = '\0';
                int tw = MeasureText(tmp, SMALL_FONT);
                int dist = mx > tw ? mx - tw : tw - mx;
                if (dist < best_dist) { best_dist = dist; best = i; }
            }
            tf->cursor = best;
        }
    }
    if (!tf->active) return;

    int key;
    while ((key = GetCharPressed()) > 0) {
        int len = (int)strlen(tf->text);
        if (key >= 32 && key < 127 && len < MAX_PATH_LEN - 1) {
            memmove(tf->text + tf->cursor + 1, tf->text + tf->cursor,
                    len - tf->cursor + 1);
            tf->text[tf->cursor++] = (char)key;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (tf->cursor > 0) {
            int len = (int)strlen(tf->text);
            memmove(tf->text + tf->cursor - 1, tf->text + tf->cursor,
                    len - tf->cursor + 1);
            tf->cursor--;
        }
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
        int len = (int)strlen(tf->text);
        if (tf->cursor < len)
            memmove(tf->text + tf->cursor, tf->text + tf->cursor + 1, len - tf->cursor);
    }
    if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && tf->cursor > 0)
        tf->cursor--;
    if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) && tf->cursor < (int)strlen(tf->text))
        tf->cursor++;
    if (IsKeyPressed(KEY_HOME)) tf->cursor = 0;
    if (IsKeyPressed(KEY_END))  tf->cursor = (int)strlen(tf->text);

    // Cmd+A select all → just move cursor to end
    if ((IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) && IsKeyPressed(KEY_A))
        tf->cursor = (int)strlen(tf->text);

    // Cmd+V paste
    if ((IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            int clip_len = (int)strlen(clip);
            int cur_len = (int)strlen(tf->text);
            int space = MAX_PATH_LEN - 1 - cur_len;
            if (clip_len > space) clip_len = space;
            if (clip_len > 0) {
                memmove(tf->text + tf->cursor + clip_len,
                        tf->text + tf->cursor, cur_len - tf->cursor + 1);
                memcpy(tf->text + tf->cursor, clip, clip_len);
                tf->cursor += clip_len;
            }
        }
    }
}

static void textfield_draw(TextField *tf, Rectangle bounds, const char *placeholder) {
    DrawRectangleRec(bounds, FIELD_BG);
    DrawRectangleLinesEx(bounds, 1, tf->active ? ACCENT_COLOR : FIELD_BORDER);

    bool has_text = strlen(tf->text) > 0;
    const char *display = has_text ? tf->text : placeholder;
    Color col = has_text ? TEXT_COLOR : DIM_TEXT;

    // Calculate cursor pixel position for auto-scroll
    int field_inner_w = (int)bounds.width - 12;
    if (has_text && tf->active) {
        char tmp[MAX_PATH_LEN];
        int n = tf->cursor < (int)sizeof(tmp)-1 ? tf->cursor : (int)sizeof(tmp)-1;
        memcpy(tmp, tf->text, n); tmp[n] = '\0';
        int cursor_px = MeasureText(tmp, SMALL_FONT);
        // Auto-scroll to keep cursor visible
        if (cursor_px - tf->scroll_x > field_inner_w - 10)
            tf->scroll_x = cursor_px - field_inner_w + 20;
        if (cursor_px - tf->scroll_x < 0)
            tf->scroll_x = cursor_px > 10 ? cursor_px - 10 : 0;
    }

    int text_x = (int)bounds.x + 6 - (has_text ? tf->scroll_x : 0);
    int text_y = (int)bounds.y + (FIELD_H - SMALL_FONT) / 2;

    BeginScissorMode((int)bounds.x + 2, (int)bounds.y, (int)bounds.width - 4, (int)bounds.height);
    DrawText(display, text_x, text_y, SMALL_FONT, col);

    // Blinking cursor
    if (tf->active && ((int)(GetTime() * 2.5) % 2 == 0)) {
        char tmp[MAX_PATH_LEN];
        int n = tf->cursor < (int)sizeof(tmp)-1 ? tf->cursor : (int)sizeof(tmp)-1;
        memcpy(tmp, tf->text, n); tmp[n] = '\0';
        int cx = text_x + MeasureText(tmp, SMALL_FONT);
        DrawRectangle(cx, (int)bounds.y + 5, 2, FIELD_H - 10, ACCENT_COLOR);
    }
    EndScissorMode();
}

// ---------- UI widgets ----------

static bool draw_button(Rectangle bounds, const char *label, Color bg_color, bool enabled) {
    bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    Color bg = enabled ? (hovered ? ACCENT_HOVER : bg_color) : BTN_DISABLED;
    Color fg = enabled ? WHITE : DIM_TEXT;

    DrawRectangleRounded(bounds, 0.3f, 8, bg);
    int tw = MeasureText(label, SMALL_FONT);
    DrawText(label, (int)(bounds.x + (bounds.width - tw) / 2),
             (int)(bounds.y + (bounds.height - SMALL_FONT) / 2), SMALL_FONT, fg);

    return enabled && hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool draw_checkbox(int x, int y, const char *label, bool *value) {
    Rectangle box = {(float)x, (float)y, 18, 18};
    Rectangle hit = {(float)x, (float)y, (float)(22 + MeasureText(label, SMALL_FONT)), 18};
    bool hovered = CheckCollisionPointRec(GetMousePosition(), hit);

    DrawRectangleRec(box, FIELD_BG);
    DrawRectangleLinesEx(box, 1, hovered ? ACCENT_COLOR : FIELD_BORDER);
    if (*value) {
        // Draw checkmark
        DrawRectangle(x + 3, y + 3, 12, 12, ACCENT_COLOR);
        DrawText("v", x + 4, y, SMALL_FONT, WHITE);
    }
    DrawText(label, x + 24, y + 1, SMALL_FONT, TEXT_COLOR);

    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        *value = !(*value);
        return true;
    }
    return false;
}

static bool draw_radio(int x, int y, const char *label, bool selected) {
    Rectangle hit = {(float)x, (float)y, (float)(22 + MeasureText(label, SMALL_FONT)), 18};
    bool hovered = CheckCollisionPointRec(GetMousePosition(), hit);

    DrawCircleLines(x + 9, y + 9, 8, hovered ? ACCENT_COLOR : FIELD_BORDER);
    if (selected) DrawCircle(x + 9, y + 9, 5, ACCENT_COLOR);
    DrawText(label, x + 22, y + 1, SMALL_FONT, TEXT_COLOR);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Draw a labeled section header
static int draw_section(int y, const char *label) {
    DrawText(label, PAD, y, SMALL_FONT, ACCENT_DIM);
    return y + 18;
}

// ---------- Main ----------

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_W, WINDOW_H, "TwilightBoxart");
    SetTargetFPS(60);

    CrawlerContext ctx;
    crawler_init(&ctx);

    TextField tf_sdroot = {0};
    TextField tf_boxart = {0};
    TextField tf_sgdb_key = {0};

    int selected_preset = 0;
    bool manual_boxart = false;

    // Auto-detect SD cards on launch
    DetectedDrive detected[8];
    int num_detected = detect_sd_cards(detected, 8);
    bool show_drive_picker = false;

    // If we found a drive with _nds, auto-select it
    for (int i = 0; i < num_detected; i++) {
        if (detected[i].has_twilight) {
            textfield_set(&tf_sdroot, detected[i].path);
            crawler_log(&ctx, "Auto-detected TwilightMenu SD: %s", detected[i].path);
            break;
        }
    }

    while (!WindowShouldClose()) {
        // Auto-fill boxart path when not manual
        if (!manual_boxart && strlen(tf_sdroot.text) > 0) {
            snprintf(tf_boxart.text, sizeof(tf_boxart.text),
                     "%s/_nds/TWiLightMenu/boxart", tf_sdroot.text);
            tf_boxart.cursor = (int)strlen(tf_boxart.text);
        }

        // Drag & drop a folder
        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count > 0) {
                textfield_set(&tf_sdroot, dropped.paths[0]);
                // Remove trailing slash
                size_t len = strlen(tf_sdroot.text);
                if (len > 1 && tf_sdroot.text[len-1] == '/') tf_sdroot.text[len-1] = '\0';
            }
            UnloadDroppedFiles(dropped);
        }

        BeginDrawing();
        ClearBackground(BG_COLOR);

        int y = PAD;

        // ===== Title bar =====
        DrawText("TwilightBoxart", PAD, y, 22, ACCENT_COLOR);
        DrawText("v2.0", PAD + MeasureText("TwilightBoxart", 22) + 8, y + 5, SMALL_FONT, DIM_TEXT);
        y += 32;

        // Thin separator
        DrawLine(PAD, y, WINDOW_W - PAD, y, FIELD_BORDER);
        y += SECTION_GAP;

        // ===== ROM Directory =====
        y = draw_section(y, "ROM / SD Card Directory");
        {
            float field_w = WINDOW_W - PAD * 2 - BROWSE_W - 8;
            Rectangle field = {PAD, (float)y, field_w, FIELD_H};
            textfield_handle(&tf_sdroot, field);
            textfield_draw(&tf_sdroot, field, "Select folder or drag & drop...");

            // Browse button
            Rectangle browse_btn = {PAD + field_w + 8, (float)y, BROWSE_W, FIELD_H};
            if (draw_button(browse_btn, "Browse", ACCENT_COLOR, true)) {
                char path[MAX_PATH_LEN];
                if (open_folder_dialog("Select your ROM or SD card folder", path, sizeof(path))) {
                    textfield_set(&tf_sdroot, path);
                }
            }
        }
        y += FIELD_H + 6;

        // Drive detection buttons
        {
            int bx = PAD;
            Rectangle detect_btn = {(float)bx, (float)y, 110, 26};
            if (draw_button(detect_btn, "Detect SD", ACCENT_DIM, true)) {
                num_detected = detect_sd_cards(detected, 8);
                if (num_detected > 0) {
                    show_drive_picker = true;
                    // Auto-pick if only one with _nds
                    int nds_count = 0;
                    int nds_idx = -1;
                    for (int i = 0; i < num_detected; i++) {
                        if (detected[i].has_twilight) { nds_count++; nds_idx = i; }
                    }
                    if (nds_count == 1) {
                        textfield_set(&tf_sdroot, detected[nds_idx].path);
                        show_drive_picker = false;
                        crawler_log(&ctx, "Auto-selected: %s", detected[nds_idx].path);
                    }
                } else {
                    crawler_log(&ctx, "No removable drives found in /Volumes");
                    show_drive_picker = false;
                }
            }

            // Show detected drives as clickable buttons
            if (show_drive_picker && num_detected > 0) {
                bx += 118;
                for (int i = 0; i < num_detected; i++) {
                    char label[80];
                    snprintf(label, sizeof(label), "%s%s",
                             detected[i].label,
                             detected[i].has_twilight ? " *" : "");
                    int lw = MeasureText(label, SMALL_FONT) + 16;
                    Rectangle dbtn = {(float)bx, (float)y, (float)lw, 26};
                    Color dc = detected[i].has_twilight ? GREEN_OK : DISABLED_BG;
                    if (draw_button(dbtn, label, dc, true)) {
                        textfield_set(&tf_sdroot, detected[i].path);
                        show_drive_picker = false;
                    }
                    bx += lw + 6;
                    if (bx > WINDOW_W - PAD - 50) break;
                }
            }
        }
        y += 26 + SECTION_GAP;

        // ===== Output Path =====
        y = draw_section(y, "Boxart Output");
        {
            draw_checkbox(PAD, y, "Custom path", &manual_boxart);
            y += 24;

            float field_w = WINDOW_W - PAD * 2 - (manual_boxart ? BROWSE_W + 8 : 0);
            Rectangle field = {PAD, (float)y, field_w, FIELD_H};

            if (manual_boxart) {
                textfield_handle(&tf_boxart, field);
                textfield_draw(&tf_boxart, field, "Select boxart output folder...");

                Rectangle browse_btn = {PAD + field_w + 8, (float)y, BROWSE_W, FIELD_H};
                if (draw_button(browse_btn, "Browse", ACCENT_COLOR, true)) {
                    char path[MAX_PATH_LEN];
                    if (open_folder_dialog("Select boxart output folder", path, sizeof(path))) {
                        textfield_set(&tf_boxart, path);
                    }
                }
            } else {
                tf_boxart.active = false;
                textfield_draw(&tf_boxart, field, "Auto: {sd_root}/_nds/TWiLightMenu/boxart");
                DrawRectangleRec(field, CLITERAL(Color){30, 30, 38, 80});
            }
        }
        y += FIELD_H + SECTION_GAP;

        // ===== Settings =====
        y = draw_section(y, "Settings");
        {
            // Size presets
            int rx = PAD;
            for (int i = 0; i < PRESET_COUNT; i++) {
                if (draw_radio(rx, y, SIZE_PRESETS[i].label, selected_preset == i))
                    selected_preset = i;
                rx += MeasureText(SIZE_PRESETS[i].label, SMALL_FONT) + 36;
            }
            y += 24;

            // Options
            rx = PAD;
            draw_checkbox(rx, y, "Keep aspect ratio", &ctx.config.keep_aspect_ratio);
            rx += 190;
            draw_checkbox(rx, y, "Overwrite existing", &ctx.config.overwrite_existing);
        }
        y += 26;

        // SteamGridDB API key (optional)
        {
            DrawText("SteamGridDB API Key (optional, get free at steamgriddb.com):",
                     PAD, y, SMALL_FONT - 2, DIM_TEXT);
            y += 14;
            Rectangle key_field = {PAD, (float)y, WINDOW_W - PAD * 2, FIELD_H - 4};
            textfield_handle(&tf_sgdb_key, key_field);
            textfield_draw(&tf_sgdb_key, key_field, "Paste API key for extra coverage...");
        }
        y += FIELD_H - 4 + 6;

        // Sources line
        {
            bool has_sgdb = strlen(tf_sgdb_key.text) > 0;
            const char *src = has_sgdb
                ? "Sources: GameTDB (NDS) | LibRetro Thumbnails | SteamGridDB"
                : "Sources: GameTDB (NDS) | LibRetro Thumbnails";
            DrawText(src, PAD, y, SMALL_FONT - 2, DIM_TEXT);
        }
        y += 16 + SECTION_GAP - 6;

        // ===== Start / Stop =====
        DrawLine(PAD, y, WINDOW_W - PAD, y, FIELD_BORDER);
        y += 10;
        {
            bool can_start = strlen(tf_sdroot.text) > 0 && strlen(tf_boxart.text) > 0;
            bool is_running = ctx.state == CRAWL_RUNNING;
            bool is_idle = ctx.state == CRAWL_IDLE || ctx.state == CRAWL_DONE;

            Rectangle btn = {PAD, (float)y, 130, BTN_H};
            if (is_running) {
                if (draw_button(btn, "Stop", RED_ERR, true))
                    crawler_stop(&ctx);
            } else {
                if (draw_button(btn, "Start Download", ACCENT_COLOR, can_start && is_idle)) {
                    strncpy(ctx.config.sd_root, tf_sdroot.text, MAX_PATH_LEN - 1);
                    strncpy(ctx.config.boxart_path, tf_boxart.text, MAX_PATH_LEN - 1);

                    size_t len = strlen(ctx.config.sd_root);
                    if (len > 1 && ctx.config.sd_root[len-1] == '/')
                        ctx.config.sd_root[len-1] = '\0';

                    ctx.config.boxart_width  = SIZE_PRESETS[selected_preset].w;
                    ctx.config.boxart_height = SIZE_PRESETS[selected_preset].h;

                    // Pass SteamGridDB key
                    strncpy(ctx.config.sgdb_api_key, tf_sgdb_key.text, MAX_API_KEY_LEN - 1);
                    ctx.config.sgdb_api_key[MAX_API_KEY_LEN - 1] = '\0';

                    crawler_start(&ctx);
                }
            }

            // Live status + SD card warning
            int sx = PAD + 140;
            if (is_running) {
                int dots = ((int)(GetTime() * 3)) % 4;
                char status[128];
                snprintf(status, sizeof(status), "Scanning%.*s  Found: %d  OK: %d  Miss: %d",
                         dots, "...", ctx.files_found, ctx.files_downloaded, ctx.files_notfound);
                DrawText(status, sx, y + 10, SMALL_FONT, GREEN_OK);
                // SD card removal warning while running
                DrawText("Do not remove the SD card!", sx, y - 6, SMALL_FONT - 2, RED_ERR);
            } else if (ctx.state == CRAWL_DONE) {
                char status[128];
                snprintf(status, sizeof(status), "Done!  Found: %d  Downloaded: %d  Skipped: %d  Not found: %d",
                         ctx.files_found, ctx.files_downloaded, ctx.files_skipped, ctx.files_notfound);
                DrawText(status, sx, y + 10, SMALL_FONT, GREEN_OK);
                // Safe to eject
                DrawText("Safe to eject SD card.", sx, y - 6, SMALL_FONT - 2, GREEN_OK);
            } else if (!can_start) {
                DrawText("Select a ROM directory to begin", sx, y + 10, SMALL_FONT, ORANGE_WARN);
            }
        }
        y += BTN_H + 10;

        // ===== Log panel =====
        y = draw_section(y, "Log");
        {
            // Copy Logs button (right-aligned on same line)
            Rectangle copy_btn = {WINDOW_W - PAD - 90, (float)(y - 18), 90, 20};
            bool has_logs = ctx.log.count > 0;
            static float copy_flash = 0; // for brief "Copied!" feedback
            if (copy_flash > 0) {
                DrawText("Copied!", (int)copy_btn.x, (int)copy_btn.y + 2, SMALL_FONT, GREEN_OK);
                copy_flash -= GetFrameTime();
            } else if (draw_button(copy_btn, "Copy Logs", ACCENT_DIM, has_logs)) {
                char *text = crawler_get_log_text(&ctx);
                if (text) {
                    SetClipboardText(text);
                    free(text);
                    copy_flash = 1.5f;
                }
            }
        }
        {
            int log_h = WINDOW_H - y - PAD;
            Rectangle log_area = {PAD, (float)y, WINDOW_W - PAD * 2, (float)log_h};
            DrawRectangleRounded(log_area, 0.015f, 8, LOG_BG);
            DrawRectangleRoundedLinesEx(log_area, 0.015f, 8, 1, FIELD_BORDER);

            int line_h = SMALL_FONT + 3;
            int visible_lines = (log_h - 10) / line_h;

            // Mouse wheel scroll
            if (CheckCollisionPointRec(GetMousePosition(), log_area)) {
                int wheel = (int)GetMouseWheelMove();
                if (wheel != 0) {
                    pthread_mutex_lock(&ctx.log.mutex);
                    ctx.log.scroll_offset += wheel * 3;
                    int max_off = ctx.log.count > visible_lines ? ctx.log.count - visible_lines : 0;
                    if (ctx.log.scroll_offset < 0) ctx.log.scroll_offset = 0;
                    if (ctx.log.scroll_offset > max_off) ctx.log.scroll_offset = max_off;
                    pthread_mutex_unlock(&ctx.log.mutex);
                }
            }

            pthread_mutex_lock(&ctx.log.mutex);
            int start = ctx.log.count > visible_lines ? ctx.log.count - visible_lines : 0;
            start -= ctx.log.scroll_offset;
            if (start < 0) start = 0;

            BeginScissorMode((int)log_area.x + 1, (int)log_area.y + 1,
                             (int)log_area.width - 2, (int)log_area.height - 2);

            int ly = y + 6;
            for (int i = start; i < ctx.log.count && (ly - y) < log_h - 6; i++) {
                Color lc = DIM_TEXT;
                const char *line = ctx.log.lines[i];
                if (strncmp(line, "OK:", 3) == 0)
                    lc = GREEN_OK;
                else if (strncmp(line, "MISS:", 5) == 0)
                    lc = RED_ERR;
                else if (strncmp(line, "Skip:", 5) == 0)
                    lc = DIM_TEXT;
                else if (strstr(line, "Error"))
                    lc = RED_ERR;
                else if (strstr(line, "Done!") || strstr(line, "Auto-detected"))
                    lc = GREEN_OK;
                else if (strstr(line, "Scanning") || strstr(line, "Found") || strstr(line, "Workers"))
                    lc = TEXT_COLOR;
                else if (strstr(line, "---"))
                    lc = FIELD_BORDER;

                DrawText(ctx.log.lines[i], (int)log_area.x + 10, ly, SMALL_FONT, lc);
                ly += line_h;
            }
            EndScissorMode();

            // Scrollbar
            if (ctx.log.count > visible_lines) {
                float ratio = (float)visible_lines / ctx.log.count;
                float sb_h = log_h * ratio;
                if (sb_h < 20) sb_h = 20;
                int max_off = ctx.log.count - visible_lines;
                float pos = max_off > 0 ? 1.0f - (float)ctx.log.scroll_offset / max_off : 0;
                float sb_y = y + pos * (log_h - sb_h);
                DrawRectangleRounded(
                    (Rectangle){log_area.x + log_area.width - 8, sb_y, 5, sb_h},
                    0.5f, 4, CLITERAL(Color){80, 80, 100, 180});
            }

            // Empty state
            if (ctx.log.count == 0) {
                const char *hint = "Drag & drop a folder, use Browse, or click Detect SD to get started";
                int hw = MeasureText(hint, SMALL_FONT);
                DrawText(hint, (int)(log_area.x + (log_area.width - hw) / 2),
                         (int)(log_area.y + log_h / 2 - 7), SMALL_FONT, DIM_TEXT);
            }

            pthread_mutex_unlock(&ctx.log.mutex);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
