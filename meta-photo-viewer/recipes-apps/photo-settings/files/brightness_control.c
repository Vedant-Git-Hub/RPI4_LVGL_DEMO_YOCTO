#include "brightness_control.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PHOTO_BACKLIGHT_DIR
#define PHOTO_BACKLIGHT_DIR "/sys/class/backlight"
#endif
#ifndef PHOTO_BRIGHTNESS_PATH
#define PHOTO_BRIGHTNESS_PATH "/opt/photo-viewer/brightness.conf"
#endif

/* Keep the panel visible so the slider can always be moved back up. */
#define MIN_BRIGHTNESS_PERCENT 5

static int read_number(const char *path)
{
    FILE *stream = fopen(path, "r");
    if(!stream) return -1;
    int value = -1;
    if(fscanf(stream, "%d", &value) != 1) value = -1;
    fclose(stream);
    return value;
}

static int find_backlight(char *brightness, size_t capacity, int *maximum)
{
    DIR *directory = opendir(PHOTO_BACKLIGHT_DIR);
    if(!directory) return 0;
    struct dirent *entry;
    int found = 0;
    while((entry = readdir(directory))) {
        if(entry->d_name[0] == '.') continue;
        char max_path[512];
        int a = snprintf(brightness, capacity, "%s/%s/brightness",
                         PHOTO_BACKLIGHT_DIR, entry->d_name);
        int b = snprintf(max_path, sizeof(max_path), "%s/%s/max_brightness",
                         PHOTO_BACKLIGHT_DIR, entry->d_name);
        if(a < 0 || (size_t)a >= capacity || b < 0 || (size_t)b >= sizeof(max_path)) continue;
        int max = read_number(max_path);
        if(max <= 0 || access(brightness, W_OK) != 0) continue;
        *maximum = max;
        found = 1;
        break;
    }
    closedir(directory);
    return found;
}

int brightness_get_percent(void)
{
    char path[512];
    int maximum;
    if(!find_backlight(path, sizeof(path), &maximum)) return -1;
    int value = read_number(path);
    if(value < 0) return -1;
    if(value > maximum) value = maximum;
    return (value * 100 + maximum / 2) / maximum;
}

int brightness_set_percent(int percent)
{
    if(percent < MIN_BRIGHTNESS_PERCENT || percent > 100) return 0;
    char path[512];
    int maximum;
    if(!find_backlight(path, sizeof(path), &maximum)) return 0;
    int raw = (int)(((long long)percent * maximum + 50) / 100);
    if(raw < 1) raw = 1;
    FILE *stream = fopen(path, "w");
    if(!stream) return 0;
    int ok = fprintf(stream, "%d\n", raw) > 0 && fflush(stream) == 0;
    if(fclose(stream)) ok = 0;
    return ok;
}

int brightness_load_saved(void)
{
    int percent = read_number(PHOTO_BRIGHTNESS_PATH);
    return percent >= MIN_BRIGHTNESS_PERCENT && percent <= 100 ? percent : -1;
}

int brightness_save(int percent)
{
    if(percent < MIN_BRIGHTNESS_PERCENT || percent > 100) return 0;
    char temporary[] = PHOTO_BRIGHTNESS_PATH ".XXXXXX";
    int descriptor = mkstemp(temporary);
    if(descriptor < 0) return 0;
    FILE *stream = fdopen(descriptor, "w");
    if(!stream) { close(descriptor); unlink(temporary); return 0; }
    int ok = fprintf(stream, "%d\n", percent) > 0 && fflush(stream) == 0 &&
             fsync(descriptor) == 0;
    if(fclose(stream)) ok = 0;
    if(ok) ok = rename(temporary, PHOTO_BRIGHTNESS_PATH) == 0;
    if(!ok) unlink(temporary);
    return ok;
}

int brightness_restore(void)
{
    int percent = brightness_load_saved();
    return percent < 0 || brightness_set_percent(percent);
}
