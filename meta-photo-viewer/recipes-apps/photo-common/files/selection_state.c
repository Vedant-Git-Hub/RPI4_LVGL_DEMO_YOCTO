#include "selection_state.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int state_path(const char *kind, char *output, size_t capacity)
{
    if(strcmp(kind, "image") && strcmp(kind, "video")) return 0;
    return snprintf(output, capacity, "/run/photo-viewer/selected-%s", kind) <
           (int)capacity;
}

int selection_state_write(const char *kind, const char *path)
{
    char output[80];
    if(!path || !state_path(kind, output, sizeof(output))) return 0;
    if(mkdir("/run/photo-viewer", 0755) && errno != EEXIST) return 0;
    FILE *stream = fopen(output, "w");
    if(!stream) return 0;
    int okay = fprintf(stream, "%s\n", path) > 0;
    if(fclose(stream)) okay = 0;
    return okay;
}

int selection_state_read(const char *kind, char *path, size_t capacity)
{
    char input[80];
    if(!path || capacity < 2 || !state_path(kind, input, sizeof(input))) return 0;
    FILE *stream = fopen(input, "r");
    if(!stream) return 0;
    int okay = fgets(path, capacity, stream) != NULL;
    fclose(stream);
    if(!okay) return 0;
    size_t length = strlen(path);
    if(length && path[length - 1] == '\n') path[--length] = '\0';
    return length > 0;
}
