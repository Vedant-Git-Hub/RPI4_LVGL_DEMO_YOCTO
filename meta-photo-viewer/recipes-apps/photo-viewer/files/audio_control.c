#include "audio_control.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define VOLUME_PATH "/opt/photo-viewer/volume.conf"

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
    char value[8];
    snprintf(value, sizeof(value), "%d%%", percent);
    pid_t child = fork();
    if(child == 0) {
        char *const arguments[] = {"amixer", "-q", "-c", "Headphones", "sset",
                                   "PCM", value, percent ? "unmute" : "mute", NULL};
        execv("/usr/bin/amixer", arguments);
        _exit(127);
    }
    if(child < 0) return 0;
    int status = 0;
    while(waitpid(child, &status, 0) < 0) {
        if(errno != EINTR) return 0;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
