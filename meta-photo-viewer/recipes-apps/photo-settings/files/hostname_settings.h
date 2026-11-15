#ifndef HOSTNAME_SETTINGS_H
#define HOSTNAME_SETTINGS_H

#include <stddef.h>

#define PHOTO_HOSTNAME_DEFAULT "vedantsphotogallary"

void photo_hostname_current(char *name, size_t capacity);
int photo_hostname_valid(const char *name);
/* 1: active now; 2: saved but service restart failed; 0: could not save. */
int photo_hostname_apply(const char *name);

#endif
