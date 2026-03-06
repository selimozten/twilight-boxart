#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 1024
#endif

typedef struct {
    char path[MAX_PATH_LEN];
    char label[64];
    bool has_twilight;
} DetectedDrive;

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len);
int platform_detect_sd(DetectedDrive *drives, int max_drives);
void platform_notify_done(void);

#endif
