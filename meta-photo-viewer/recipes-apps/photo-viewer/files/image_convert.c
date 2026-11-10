#include "image_convert.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int image_supported_name(const char *name)
{
    const char *ext = strrchr(name, '.');
    return name[0] != '.' && ext && ext != name && ext[1] != '\0';
}

int image_is_gif(const char *path)
{
    unsigned char signature[6];
    FILE *stream = fopen(path, "rb");
    if(!stream) return 0;
    int result = fread(signature, 1, sizeof(signature), stream) == sizeof(signature) &&
                 (!memcmp(signature, "GIF89a", sizeof(signature)) ||
                  !memcmp(signature, "GIF87a", sizeof(signature)));
    fclose(stream);
    return result;
}

static unsigned int read_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int read_u32(const unsigned char *p)
{
    return read_u16(p) | (read_u16(p + 2) << 16);
}

static int valid_output(const char *path, unsigned int width, unsigned int height)
{
    struct stat info;
    unsigned char header[54];
    FILE *stream = fopen(path, "rb");
    if(!stream) return 0;
    int ok = !fstat(fileno(stream), &info) &&
             fread(header, 1, sizeof(header), stream) == sizeof(header);
    fclose(stream);
    if(!ok) return 0;
    unsigned int offset = read_u32(header + 10);
    return header[0] == 'B' && header[1] == 'M' &&
           read_u32(header + 14) == 40 && read_u16(header + 26) == 1 &&
           read_u16(header + 28) == 24 && read_u32(header + 30) == 0 &&
           read_u32(header + 18) == width && read_u32(header + 22) == height &&
           offset >= 54 &&
           info.st_size >= (off_t)offset +
                           (off_t)(((width * 3 + 3) / 4) * 4) * height;
}

int image_convert_to_bmp_edit(const char *source, const char *name_hint,
                              unsigned int width, unsigned int height,
                              const image_settings_t *settings,
                              char *output, size_t output_size)
{
    if(!settings || settings->rotation > 270 || settings->rotation % 90 ||
       (settings->mode != IMAGE_FIT_WHOLE &&
        settings->mode != IMAGE_FILL_DISPLAY) ||
       width == 0 || height == 0 || width > 800 || height > 480 ||
       output_size < sizeof("/run/photo-viewer/slide-XXXXXX.bmp")) return 0;
    if(mkdir("/run/photo-viewer", 0755) && errno != EEXIST) return 0;
    char temporary[] = "/run/photo-viewer/slide-XXXXXX.bmp";
    int fd = mkstemps(temporary, 4);
    if(fd < 0) return 0;
    close(fd);
    const char *extension = strrchr(name_hint, '.');
    if(!extension || strlen(extension) > 16) extension = ".img";
    for(const char *cursor = extension + 1; *cursor; cursor++) {
        if(!isalnum((unsigned char)*cursor)) {
            extension = ".img";
            break;
        }
    }
    char alias[80];
    if(snprintf(alias, sizeof(alias), "/run/photo-viewer/source-XXXXXX%s", extension) >= (int)sizeof(alias)) {
        unlink(temporary);
        return 0;
    }
    fd = mkstemps(alias, (int)strlen(extension));
    if(fd < 0) { unlink(temporary); return 0; }
    close(fd);
    unlink(alias);
    if(symlink(source, alias)) { unlink(temporary); return 0; }
    char input[512], destination[512], geometry[32], resize[40], rotation[8];
    if(snprintf(input, sizeof(input), "%s[0]", alias) >= (int)sizeof(input) ||
       snprintf(destination, sizeof(destination), "BMP3:%s", temporary) >= (int)sizeof(destination) ||
       snprintf(geometry, sizeof(geometry), "%ux%u", width, height) >= (int)sizeof(geometry) ||
       snprintf(resize, sizeof(resize), "%ux%u%s", width, height,
                settings->mode == IMAGE_FILL_DISPLAY ? "^" : "") >= (int)sizeof(resize) ||
       snprintf(rotation, sizeof(rotation), "%u", settings->rotation) >= (int)sizeof(rotation)) {
        unlink(alias);
        unlink(temporary);
        return 0;
    }
    pid_t child = fork();
    if(child == 0) {
        char *const args[] = {
            "/usr/bin/magick.im7", "-limit", "memory", "128MiB",
            "-limit", "map", "256MiB", "-limit", "disk", "256MiB",
            "-limit", "time", "30",
            input, "-auto-orient", "-background", "#000000",
            "-rotate", rotation, "-resize", resize, "-gravity", "center",
            "-extent", geometry, "-depth", "8", "-type", "TrueColor",
            destination, NULL
        };
        execv(args[0], args);
        _exit(127);
    }
    int status = 0;
    if(child < 0 || waitpid(child, &status, 0) != child ||
       !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(alias);
        unlink(temporary);
        return 0;
    }
    unlink(alias);
    if(!valid_output(temporary, width, height)) {
        unlink(temporary);
        return 0;
    }
    snprintf(output, output_size, "%s", temporary);
    return 1;
}

int image_convert_to_bmp_size(const char *source, const char *name_hint,
                              unsigned int width, unsigned int height,
                              char *output, size_t output_size)
{
    const image_settings_t defaults = {0, IMAGE_FIT_WHOLE};
    return image_convert_to_bmp_edit(source, name_hint, width, height,
                                     &defaults, output, output_size);
}

int image_convert_to_bmp(const char *source, const char *name_hint,
                         char *output, size_t output_size)
{
    return image_convert_to_bmp_size(source, name_hint, 800, 480,
                                     output, output_size);
}
