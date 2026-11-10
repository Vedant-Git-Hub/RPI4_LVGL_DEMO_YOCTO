#ifndef IMAGE_CONVERT_H
#define IMAGE_CONVERT_H

#include <stddef.h>
#include "image_settings.h"

int image_supported_name(const char *name);
int image_convert_to_bmp(const char *source, const char *name_hint,
                         char *output, size_t output_size);
int image_convert_to_bmp_size(const char *source, const char *name_hint,
                              unsigned int width, unsigned int height,
                              char *output, size_t output_size);
int image_convert_to_bmp_edit(const char *source, const char *name_hint,
                              unsigned int width, unsigned int height,
                              const image_settings_t *settings,
                              char *output, size_t output_size);

#endif
