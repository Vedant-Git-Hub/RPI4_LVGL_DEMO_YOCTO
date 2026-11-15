#ifndef IMAGE_SETTINGS_H
#define IMAGE_SETTINGS_H

typedef enum {
    IMAGE_FIT_WHOLE = 0,
    IMAGE_FILL_DISPLAY = 1
} image_mode_t;

typedef struct {
    unsigned int rotation;
    image_mode_t mode;
} image_settings_t;

void image_settings_load(const char *image_path, image_settings_t *settings);
int image_settings_save(const char *image_path, const image_settings_t *settings);
void image_settings_remove(const char *image_path);
int image_settings_transfer(const char *from, const char *to, int copy);

#endif
