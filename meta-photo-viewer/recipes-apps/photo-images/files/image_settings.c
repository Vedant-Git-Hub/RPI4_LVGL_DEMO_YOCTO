#include "image_settings.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int sidecar_path(const char *image_path, char *output, size_t capacity)
{
    const char *basename = strrchr(image_path, '/');
    size_t prefix_length = basename ? (size_t)(basename - image_path + 1) : 0;
    basename = basename ? basename + 1 : image_path;
    int length = snprintf(output, capacity, "%.*s.%s.pv",
                          (int)prefix_length, image_path, basename);
    return length > 0 && (size_t)length < capacity;
}

void image_settings_load(const char *image_path, image_settings_t *settings)
{
    settings->rotation = 0;
    settings->mode = IMAGE_FIT_WHOLE;
    char path[512], line[64];
    if(!sidecar_path(image_path, path, sizeof(path))) return;
    FILE *stream = fopen(path, "r");
    if(!stream) return;
    while(fgets(line, sizeof(line), stream)) {
        unsigned int angle;
        if(sscanf(line, "rotation=%u", &angle) == 1 && angle <= 270 &&
           angle % 90 == 0) settings->rotation = angle;
        else if(!strcmp(line, "mode=fill\n")) settings->mode = IMAGE_FILL_DISPLAY;
        else if(!strcmp(line, "mode=fit\n")) settings->mode = IMAGE_FIT_WHOLE;
    }
    fclose(stream);
}

int image_settings_save(const char *image_path, const image_settings_t *settings)
{
    if(settings->rotation > 270 || settings->rotation % 90 ||
       (settings->mode != IMAGE_FIT_WHOLE &&
        settings->mode != IMAGE_FILL_DISPLAY)) return 0;
    char path[512], temporary[528];
    if(!sidecar_path(image_path, path, sizeof(path)) ||
       snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary)) return 0;
    int descriptor = mkstemp(temporary);
    if(descriptor < 0) return 0;
    FILE *stream = fdopen(descriptor, "w");
    if(!stream) { close(descriptor); unlink(temporary); return 0; }
    int ok = fprintf(stream, "rotation=%u\nmode=%s\n", settings->rotation,
                     settings->mode == IMAGE_FILL_DISPLAY ? "fill" : "fit") > 0;
    if(ok) ok = fflush(stream) == 0 && fsync(descriptor) == 0;
    if(fclose(stream)) ok = 0;
    if(ok) ok = rename(temporary, path) == 0;
    if(!ok) unlink(temporary);
    return ok;
}

void image_settings_remove(const char *image_path)
{
    char path[512];
    if(sidecar_path(image_path, path, sizeof(path))) unlink(path);
}

int image_settings_transfer(const char *from, const char *to, int copy)
{
    char source[512], destination[512];
    if(!sidecar_path(from, source, sizeof(source)) ||
       !sidecar_path(to, destination, sizeof(destination))) return 0;
    if(access(source, F_OK)) {
        if(errno != ENOENT) return 0;
        image_settings_remove(to);
        return 1;
    }
    if(!copy) return rename(source, destination) == 0;
    image_settings_t settings;
    image_settings_load(from, &settings);
    return image_settings_save(to, &settings);
}
