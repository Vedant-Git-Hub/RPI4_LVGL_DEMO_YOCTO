#include "audio_control.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define VOLUME_PATH "/opt/photo-viewer/volume.conf"
#define MAX_VOLUME_INPUTS 12

static atomic_int current_volume = 75;
static int input_fds[MAX_VOLUME_INPUTS];
static int input_count;
static time_t last_input_scan;
static int last_audible_volume = 75;

int audio_volume_load(void)
{
    FILE *stream = fopen(VOLUME_PATH, "r");
    if(!stream) return 75;
    int value = 75;
    if(fscanf(stream, "%d", &value) != 1 || value < 0 || value > 100) value = 75;
    fclose(stream);
    return value;
}

int audio_volume_save(int percent)
{
    if(percent < 0 || percent > 100) return 0;
    char temporary[] = VOLUME_PATH ".XXXXXX";
    int descriptor = mkstemp(temporary);
    if(descriptor < 0) return 0;
    FILE *stream = fdopen(descriptor, "w");
    if(!stream) { close(descriptor); unlink(temporary); return 0; }
    int ok = fprintf(stream, "%d\n", percent) > 0 && fflush(stream) == 0 &&
             fsync(descriptor) == 0;
    if(fclose(stream)) ok = 0;
    if(ok) ok = rename(temporary, VOLUME_PATH) == 0;
    if(!ok) unlink(temporary);
    return ok;
}

int audio_volume_apply(int percent)
{
    if(percent < 0 || percent > 100) return 0;
    /* The PCM bridge applies this gain to each sample. This works with USB
     * devices that expose no ALSA mixer, and takes effect during playback. */
    atomic_store(&current_volume, percent);
    if(percent) last_audible_volume = percent;
    return 1;
}

int audio_volume_current(void)
{
    return atomic_load(&current_volume);
}

void audio_output_device(char *device, size_t capacity)
{
    if(!device || !capacity) return;
    snprintf(device, capacity, "sysdefault:CARD=Headphones");
    FILE *cards = fopen("/proc/asound/cards", "r");
    if(!cards) return;
    char line[256];
    while(fgets(line, sizeof(line), cards)) {
        int card;
        /* The ALSA card header contains the driver type after the dash. */
        if(sscanf(line, " %d [", &card) == 1 &&
           strstr(line, "USB-Audio") && card >= 0) {
            snprintf(device, capacity, "plughw:%d,0", card);
            break;
        }
    }
    fclose(cards);
}

static int has_key(const unsigned long *bits, int key)
{
    return !!(bits[key / (8 * sizeof(unsigned long))] &
              (1UL << (key % (8 * sizeof(unsigned long)))));
}

static void scan_volume_inputs(void)
{
    for(int i = 0; i < input_count; i++) close(input_fds[i]);
    input_count = 0;
    DIR *directory = opendir("/dev/input");
    if(!directory) return;
    struct dirent *entry;
    while((entry = readdir(directory)) && input_count < MAX_VOLUME_INPUTS) {
        if(strncmp(entry->d_name, "event", 5)) continue;
        char path[128];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        unsigned long keys[(KEY_MAX + 8 * sizeof(unsigned long)) /
                           (8 * sizeof(unsigned long))] = {0};
        if(ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 &&
           (has_key(keys, KEY_VOLUMEUP) || has_key(keys, KEY_VOLUMEDOWN) ||
            has_key(keys, KEY_MUTE))) input_fds[input_count++] = fd;
        else close(fd);
    }
    closedir(directory);
}

int audio_input_poll(void)
{
    time_t now = time(NULL);
    if(now != last_input_scan && (last_input_scan == 0 || now - last_input_scan >= 2)) {
        scan_volume_inputs();
        last_input_scan = now;
    }
    int changed = -1;
    for(int i = 0; i < input_count; i++) {
        struct input_event event;
        while(read(input_fds[i], &event, sizeof(event)) == sizeof(event)) {
            if(event.type != EV_KEY || event.value == 0) continue;
            int value = audio_volume_current();
            if(event.code == KEY_VOLUMEUP) value += 5;
            else if(event.code == KEY_VOLUMEDOWN) value -= 5;
            else if(event.code == KEY_MUTE) value = value ? 0 : last_audible_volume;
            else continue;
            if(value < 0) value = 0;
            if(value > 100) value = 100;
            if(value != audio_volume_current()) {
                audio_volume_apply(value);
                audio_volume_save(value);
                changed = value;
            }
        }
    }
    return changed;
}
