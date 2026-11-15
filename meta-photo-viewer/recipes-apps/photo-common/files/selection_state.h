#ifndef PHOTO_SELECTION_STATE_H
#define PHOTO_SELECTION_STATE_H

#include <stddef.h>

/* Small file-based IPC: selections are per boot and paths are checked again
 * against each library before enabling an editor. */
int selection_state_write(const char *kind, const char *path);
int selection_state_read(const char *kind, char *path, size_t capacity);

#endif
