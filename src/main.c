#include "crawler.h"
#include "platform.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define WINDOW_W   780
#define WINDOW_H   660
#define PAD        20
#define SMALL_FONT 14
#define FIELD_H    30
#define BTN_H      34
#define BROWSE_W   80
#define SECTION_GAP 14

// Modifier key: Cmd on macOS, Ctrl on Windows/Linux
#ifdef __APPLE__
#define MOD_KEY_LEFT  KEY_LEFT_SUPER
#define MOD_KEY_RIGHT KEY_RIGHT_SUPER
#else
#define MOD_KEY_LEFT  KEY_LEFT_CONTROL
#define MOD_KEY_RIGHT KEY_RIGHT_CONTROL
#endif

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
    if ((IsKeyDown(MOD_KEY_LEFT) || IsKeyDown(MOD_KEY_RIGHT)) && IsKeyPressed(KEY_A))
        tf->cursor = (int)strlen(tf->text);

    // Cmd+V paste
    if ((IsKeyDown(MOD_KEY_LEFT) || IsKeyDown(MOD_KEY_RIGHT)) && IsKeyPressed(KEY_V)) {
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

static void textfield_draw_ex(TextField *tf, Rectangle bounds, const char *placeholder, bool masked) {
    DrawRectangleRec(bounds, FIELD_BG);
    DrawRectangleLinesEx(bounds, 1, tf->active ? ACCENT_COLOR : FIELD_BORDER);

    bool has_text = strlen(tf->text) > 0;
    // Build masked display string if needed
    char mask_buf[MAX_PATH_LEN];
    if (masked && has_text) {
        int len = (int)strlen(tf->text);
        if (len > MAX_PATH_LEN - 1) len = MAX_PATH_LEN - 1;
        memset(mask_buf, '*', len);
        mask_buf[len] = '\0';
    }
    const char *display = has_text ? (masked ? mask_buf : tf->text) : placeholder;
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

static void textfield_draw(TextField *tf, Rectangle bounds, const char *placeholder) {
    textfield_draw_ex(tf, bounds, placeholder, false);
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
        // Draw checkmark with lines
        DrawRectangle(x + 3, y + 3, 12, 12, ACCENT_COLOR);
        DrawLineEx((Vector2){x + 5, y + 9}, (Vector2){x + 8, y + 13}, 2, WHITE);
        DrawLineEx((Vector2){x + 8, y + 13}, (Vector2){x + 14, y + 5}, 2, WHITE);
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

// ---------- Config persistence ----------

static void get_config_path(char *out, size_t len) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata)
        snprintf(out, len, "%s\\TwilightBoxart.ini", appdata);
    else
        snprintf(out, len, "TwilightBoxart.ini");
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (home)
        snprintf(out, len, "%s/Library/Preferences/TwilightBoxart.ini", home);
    else
        snprintf(out, len, "TwilightBoxart.ini");
#else
    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (config)
        snprintf(out, len, "%s/TwilightBoxart.ini", config);
    else if (home)
        snprintf(out, len, "%s/.config/TwilightBoxart.ini", home);
    else
        snprintf(out, len, "TwilightBoxart.ini");
#endif
}

typedef struct {
    char sd_root[MAX_PATH_LEN];
    char sgdb_api_key[MAX_API_KEY_LEN];
    int  preset;
    bool keep_aspect;
    bool overwrite;
} SavedConfig;

static void load_config(SavedConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->keep_aspect = true;

    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1280];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        // Trim newline
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r')) val[--vlen] = '\0';

        if (strcmp(key, "sd_root") == 0)
            strncpy(cfg->sd_root, val, MAX_PATH_LEN - 1);
        else if (strcmp(key, "sgdb_api_key") == 0)
            strncpy(cfg->sgdb_api_key, val, MAX_API_KEY_LEN - 1);
        else if (strcmp(key, "preset") == 0)
            cfg->preset = atoi(val);
        else if (strcmp(key, "keep_aspect") == 0)
            cfg->keep_aspect = atoi(val) != 0;
        else if (strcmp(key, "overwrite") == 0)
            cfg->overwrite = atoi(val) != 0;
    }
    fclose(f);
}

static void save_config(const char *sd_root, const char *sgdb_key,
                         int preset, bool keep_aspect, bool overwrite) {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "sd_root=%s\n", sd_root);
    fprintf(f, "sgdb_api_key=%s\n", sgdb_key);
    fprintf(f, "preset=%d\n", preset);
    fprintf(f, "keep_aspect=%d\n", keep_aspect ? 1 : 0);
    fprintf(f, "overwrite=%d\n", overwrite ? 1 : 0);
    fclose(f);
}

// ---------- Main ----------

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W, WINDOW_H, "TwilightBoxart");
    SetWindowMinSize(600, 500);
    SetTargetFPS(60);

    CrawlerContext ctx;
    crawler_init(&ctx);

    TextField tf_sdroot = {0};
    TextField tf_boxart = {0};
    TextField tf_sgdb_key = {0};

    int selected_preset = 0;
    bool manual_boxart = false;
    bool show_api_key = false;

    // Load saved settings
    SavedConfig saved;
    load_config(&saved);
    if (saved.sd_root[0]) textfield_set(&tf_sdroot, saved.sd_root);
    if (saved.sgdb_api_key[0]) textfield_set(&tf_sgdb_key, saved.sgdb_api_key);
    if (saved.preset >= 0 && saved.preset < PRESET_COUNT) selected_preset = saved.preset;
    ctx.config.keep_aspect_ratio = saved.keep_aspect;
    ctx.config.overwrite_existing = saved.overwrite;

    CrawlState prev_state = CRAWL_IDLE;
    int log_filter = 0; // 0=All, 1=OK, 2=Miss, 3=Skip

    // Boxart preview
    Texture2D preview_tex = {0};
    char preview_path[MAX_PATH_LEN] = {0};
    char preview_label[256] = {0};

    // Auto-detect SD cards on launch
    DetectedDrive detected[8];
    int num_detected = platform_detect_sd(detected, 8);
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
        int win_w = GetScreenWidth();
        int win_h = GetScreenHeight();

        // Auto-fill boxart path when not manual
        if (!manual_boxart && strlen(tf_sdroot.text) > 0) {
            snprintf(tf_boxart.text, sizeof(tf_boxart.text),
                     "%s/_nds/TWiLightMenu/boxart", tf_sdroot.text);
            tf_boxart.cursor = (int)strlen(tf_boxart.text);
        }

        // Completion notification
        if (prev_state == CRAWL_RUNNING && ctx.state == CRAWL_DONE) {
            platform_notify_done();
        }
        prev_state = ctx.state;

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

        // Update boxart preview
        {
            pthread_mutex_lock(&ctx.last_dl_mutex);
            bool changed = ctx.last_downloaded[0] && strcmp(ctx.last_downloaded, preview_path) != 0;
            char new_path[MAX_PATH_LEN] = {0};
            if (changed) strncpy(new_path, ctx.last_downloaded, MAX_PATH_LEN - 1);
            pthread_mutex_unlock(&ctx.last_dl_mutex);
            if (changed) {
                if (preview_tex.id > 0) UnloadTexture(preview_tex);
                Image img = LoadImage(new_path);
                if (img.data) {
                    preview_tex = LoadTextureFromImage(img);
                    // Extract filename for label
                    const char *slash = strrchr(new_path, '/');
                    if (!slash) slash = strrchr(new_path, '\\');
                    strncpy(preview_label, slash ? slash + 1 : new_path, sizeof(preview_label) - 1);
                    UnloadImage(img);
                }
                strncpy(preview_path, new_path, MAX_PATH_LEN - 1);
            }
        }

        // Keyboard shortcuts
        if (IsKeyPressed(KEY_TAB)) {
            // Cycle focus: sd_root -> boxart -> api_key -> sd_root
            if (tf_sdroot.active) {
                tf_sdroot.active = false;
                if (manual_boxart) tf_boxart.active = true;
                else tf_sgdb_key.active = true;
            } else if (tf_boxart.active) {
                tf_boxart.active = false;
                tf_sgdb_key.active = true;
            } else if (tf_sgdb_key.active) {
                tf_sgdb_key.active = false;
                tf_sdroot.active = true;
            } else {
                tf_sdroot.active = true;
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            bool can_start = strlen(tf_sdroot.text) > 0 && strlen(tf_boxart.text) > 0;
            bool is_idle = ctx.state == CRAWL_IDLE || ctx.state == CRAWL_DONE;
            if (can_start && is_idle) {
                strncpy(ctx.config.sd_root, tf_sdroot.text, MAX_PATH_LEN - 1);
                strncpy(ctx.config.boxart_path, tf_boxart.text, MAX_PATH_LEN - 1);
                size_t len = strlen(ctx.config.sd_root);
                if (len > 1 && ctx.config.sd_root[len-1] == '/')
                    ctx.config.sd_root[len-1] = '\0';
                ctx.config.boxart_width  = SIZE_PRESETS[selected_preset].w;
                ctx.config.boxart_height = SIZE_PRESETS[selected_preset].h;
                strncpy(ctx.config.sgdb_api_key, tf_sgdb_key.text, MAX_API_KEY_LEN - 1);
                ctx.config.sgdb_api_key[MAX_API_KEY_LEN - 1] = '\0';
                crawler_start(&ctx);
            }
        }
        if (IsKeyPressed(KEY_ESCAPE) && ctx.state == CRAWL_RUNNING) {
            crawler_stop(&ctx);
        }

        BeginDrawing();
        ClearBackground(BG_COLOR);

        int y = PAD;

        // ===== Title bar =====
        DrawText("TwilightBoxart", PAD, y, 22, ACCENT_COLOR);
        DrawText("v2.0", PAD + MeasureText("TwilightBoxart", 22) + 8, y + 5, SMALL_FONT, DIM_TEXT);
        y += 32;

        // Thin separator
        DrawLine(PAD, y, win_w - PAD, y, FIELD_BORDER);
        y += SECTION_GAP;

        // ===== ROM Directory =====
        y = draw_section(y, "ROM / SD Card Directory");
        {
            float field_w = win_w - PAD * 2 - BROWSE_W - 8;
            Rectangle field = {PAD, (float)y, field_w, FIELD_H};
            textfield_handle(&tf_sdroot, field);
            textfield_draw(&tf_sdroot, field, "Select folder or drag & drop...");

            // Browse button
            Rectangle browse_btn = {PAD + field_w + 8, (float)y, BROWSE_W, FIELD_H};
            if (draw_button(browse_btn, "Browse", ACCENT_COLOR, true)) {
                char path[MAX_PATH_LEN];
                if (platform_folder_dialog("Select your ROM or SD card folder", path, sizeof(path))) {
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
                num_detected = platform_detect_sd(detected, 8);
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
                    if (bx > win_w - PAD - 50) break;
                }
            }
        }
        y += 26 + SECTION_GAP;

        // ===== Output Path =====
        y = draw_section(y, "Boxart Output");
        {
            draw_checkbox(PAD, y, "Custom path", &manual_boxart);
            y += 24;

            float field_w = win_w - PAD * 2 - (manual_boxart ? BROWSE_W + 8 : 0);
            Rectangle field = {PAD, (float)y, field_w, FIELD_H};

            if (manual_boxart) {
                textfield_handle(&tf_boxart, field);
                textfield_draw(&tf_boxart, field, "Select boxart output folder...");

                Rectangle browse_btn = {PAD + field_w + 8, (float)y, BROWSE_W, FIELD_H};
                if (draw_button(browse_btn, "Browse", ACCENT_COLOR, true)) {
                    char path[MAX_PATH_LEN];
                    if (platform_folder_dialog("Select boxart output folder", path, sizeof(path))) {
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
            float toggle_w = 50;
            Rectangle key_field = {PAD, (float)y, win_w - PAD * 2 - toggle_w - 6, FIELD_H - 4};
            textfield_handle(&tf_sgdb_key, key_field);
            textfield_draw_ex(&tf_sgdb_key, key_field, "Paste API key for extra coverage...", !show_api_key);
            // Show/Hide toggle
            Rectangle toggle_btn = {key_field.x + key_field.width + 6, (float)y, toggle_w, FIELD_H - 4};
            if (draw_button(toggle_btn, show_api_key ? "Hide" : "Show", ACCENT_DIM, true))
                show_api_key = !show_api_key;
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
        DrawLine(PAD, y, win_w - PAD, y, FIELD_BORDER);
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

            // Boxart preview thumbnail (right-aligned)
            if (preview_tex.id > 0) {
                int th = BTN_H;
                float scale = (float)th / preview_tex.height;
                int tw_px = (int)(preview_tex.width * scale);
                int px = win_w - PAD - tw_px;
                int py = y;
                DrawTextureEx(preview_tex, (Vector2){(float)px, (float)py}, 0, scale, WHITE);
                DrawRectangleLinesEx((Rectangle){(float)px, (float)py, (float)tw_px, (float)th}, 1, FIELD_BORDER);
                // Label under preview
                int lw = MeasureText(preview_label, SMALL_FONT - 2);
                if (lw > tw_px + 40) lw = tw_px + 40;
                DrawText(preview_label, px - lw - 6, py + th / 2 - 6, SMALL_FONT - 2, DIM_TEXT);
            }
        }
        y += BTN_H + 10;

        // ===== Progress bar =====
        {
            int processed = ctx.files_downloaded + ctx.files_notfound;
            int total = ctx.files_to_download;
            bool show_bar = ctx.state == CRAWL_RUNNING || ctx.state == CRAWL_DONE;
            if (show_bar && total > 0) {
                float progress = (float)processed / total;
                if (progress > 1.0f) progress = 1.0f;
                Rectangle bar_bg = {PAD, (float)y, win_w - PAD * 2, 8};
                DrawRectangleRounded(bar_bg, 0.5f, 4, FIELD_BG);
                Rectangle bar_fill = {PAD, (float)y, (win_w - PAD * 2) * progress, 8};
                Color bar_color = (ctx.state == CRAWL_DONE) ? GREEN_OK : ACCENT_COLOR;
                if (bar_fill.width > 0)
                    DrawRectangleRounded(bar_fill, 0.5f, 4, bar_color);
                // Percentage text
                char pct[16];
                snprintf(pct, sizeof(pct), "%d%%", (int)(progress * 100));
                DrawText(pct, win_w - PAD - MeasureText(pct, SMALL_FONT - 2),
                         y - 12, SMALL_FONT - 2, DIM_TEXT);
                y += 14;
            }
        }

        // ===== Log panel =====
        y = draw_section(y, "Log");
        {
            // Filter buttons
            int fx = PAD + MeasureText("Log", SMALL_FONT) + 12;
            int fy = y - 18;
            const char *filters[] = {"All", "OK", "Miss", "Skip"};
            for (int i = 0; i < 4; i++) {
                int fw = MeasureText(filters[i], SMALL_FONT - 2) + 12;
                Rectangle fb = {(float)fx, (float)fy, (float)fw, 18};
                Color fc = (log_filter == i) ? ACCENT_COLOR : ACCENT_DIM;
                if (draw_button(fb, filters[i], fc, true))
                    log_filter = i;
                fx += fw + 4;
            }
        }
        {
            // Copy Logs button (right-aligned on same line)
            Rectangle copy_btn = {win_w - PAD - 90, (float)(y - 18), 90, 20};
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
            int log_h = win_h - y - PAD;
            Rectangle log_area = {PAD, (float)y, win_w - PAD * 2, (float)log_h};
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

            // Build filtered index list
            int filtered[MAX_LOG_LINES];
            int filtered_count = 0;
            for (int i = 0; i < ctx.log.count; i++) {
                const char *line = ctx.log.lines[i];
                if (log_filter == 1 && strncmp(line, "OK:", 3) != 0) continue;
                if (log_filter == 2 && strncmp(line, "MISS:", 5) != 0) continue;
                if (log_filter == 3 && strncmp(line, "Skip:", 5) != 0) continue;
                filtered[filtered_count++] = i;
            }

            int start = filtered_count > visible_lines ? filtered_count - visible_lines : 0;
            start -= ctx.log.scroll_offset;
            if (start < 0) start = 0;

            BeginScissorMode((int)log_area.x + 1, (int)log_area.y + 1,
                             (int)log_area.width - 2, (int)log_area.height - 2);

            int ly = y + 6;
            for (int fi = start; fi < filtered_count && (ly - y) < log_h - 6; fi++) {
                int i = filtered[fi];
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
            if (filtered_count > visible_lines) {
                float ratio = (float)visible_lines / filtered_count;
                float sb_h = log_h * ratio;
                if (sb_h < 20) sb_h = 20;
                int max_off = filtered_count - visible_lines;
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

    // Save settings before exit
    save_config(tf_sdroot.text, tf_sgdb_key.text,
                selected_preset, ctx.config.keep_aspect_ratio,
                ctx.config.overwrite_existing);

    if (preview_tex.id > 0) UnloadTexture(preview_tex);
    CloseWindow();
    return 0;
}
