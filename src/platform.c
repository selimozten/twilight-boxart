#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

#if !defined(_WIN32)
#include <dirent.h>
#endif

// ---------- Folder dialog ----------

#if defined(_WIN32)

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len) {
    BROWSEINFOA bi = {0};
    bi.lpszTitle = prompt;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return false;
    char path[MAX_PATH];
    bool ok = SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);
    if (ok && strlen(path) > 0) {
        strncpy(out, path, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

#elif defined(__APPLE__)

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len) {
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

    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    while (len > 1 && buf[len-1] == '/') buf[--len] = '\0';

    if (len == 0) return false;
    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

#else // Linux

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len) {
    (void)prompt;
    const char *cmds[] = {
        "zenity --file-selection --directory 2>/dev/null",
        "kdialog --getexistingdirectory ~ 2>/dev/null",
        NULL
    };
    for (int i = 0; cmds[i]; i++) {
        FILE *fp = popen(cmds[i], "r");
        if (!fp) continue;
        char buf[MAX_PATH_LEN] = {0};
        if (fgets(buf, sizeof(buf), fp) != NULL) {
            int status = pclose(fp);
            if (status == 0) {
                size_t len = strlen(buf);
                while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
                while (len > 1 && buf[len-1] == '/') buf[--len] = '\0';
                if (len > 0) {
                    strncpy(out, buf, out_len - 1);
                    out[out_len - 1] = '\0';
                    return true;
                }
            }
        } else {
            pclose(fp);
        }
    }
    return false;
}

#endif

// ---------- SD card detection ----------

#if defined(_WIN32)

int platform_detect_sd(DetectedDrive *drives, int max_drives) {
    int count = 0;
    DWORD mask = GetLogicalDrives();
    for (int i = 3; i < 26 && count < max_drives; i++) {
        if (!(mask & (1 << i))) continue;
        char drive_root[] = { 'A' + i, ':', '\\', '\0' };
        UINT type = GetDriveTypeA(drive_root);
        if (type != DRIVE_REMOVABLE) continue;

        char vol_path[MAX_PATH_LEN];
        snprintf(vol_path, sizeof(vol_path), "%c:", 'A' + i);

        char nds_path[MAX_PATH_LEN];
        snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
        struct stat st;
        bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

        char vol_name[64] = {0};
        GetVolumeInformationA(drive_root, vol_name, sizeof(vol_name),
                              NULL, NULL, NULL, NULL, 0);

        strncpy(drives[count].path, vol_path, MAX_PATH_LEN - 1);
        if (vol_name[0])
            strncpy(drives[count].label, vol_name, 63);
        else
            snprintf(drives[count].label, 64, "Drive %c:", 'A' + i);
        drives[count].has_twilight = has_nds;
        count++;
    }
    return count;
}

#elif defined(__APPLE__)

int platform_detect_sd(DetectedDrive *drives, int max_drives) {
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

        char nds_path[MAX_PATH_LEN];
        snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
        bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

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

#else // Linux

int platform_detect_sd(DetectedDrive *drives, int max_drives) {
    int count = 0;
    const char *user = getenv("USER");
    if (!user) user = "root";

    char media_paths[2][MAX_PATH_LEN];
    snprintf(media_paths[0], MAX_PATH_LEN, "/media/%s", user);
    snprintf(media_paths[1], MAX_PATH_LEN, "/run/media/%s", user);

    for (int mp = 0; mp < 2 && count < max_drives; mp++) {
        DIR *volumes = opendir(media_paths[mp]);
        if (!volumes) continue;

        struct dirent *entry;
        while ((entry = readdir(volumes)) != NULL && count < max_drives) {
            if (entry->d_name[0] == '.') continue;

            char vol_path[MAX_PATH_LEN];
            snprintf(vol_path, sizeof(vol_path), "%s/%s", media_paths[mp], entry->d_name);

            struct stat st;
            if (stat(vol_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            char nds_path[MAX_PATH_LEN];
            snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
            bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

            strncpy(drives[count].path, vol_path, MAX_PATH_LEN - 1);
            strncpy(drives[count].label, entry->d_name, 63);
            drives[count].has_twilight = has_nds;
            count++;
        }
        closedir(volumes);
    }
    return count;
}

#endif

// ---------- Completion notification ----------

void platform_notify_done(void) {
#if defined(__APPLE__)
    system("osascript -e 'display notification \"Box art download complete!\" with title \"TwilightBoxart\"' &");
#elif defined(__linux__)
    system("notify-send 'TwilightBoxart' 'Box art download complete!' 2>/dev/null &");
#endif
    // Windows: no simple toast API; the app UI shows "Done!" status
}
