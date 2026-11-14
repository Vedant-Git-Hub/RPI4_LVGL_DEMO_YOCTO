#ifndef CAMERA_BACKEND_H
#define CAMERA_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#define CAMERA_MAX_MODES 64

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;
} camera_mode_t;

typedef struct camera_device camera_device_t;

int camera_discover(char *path, size_t path_size, char *name, size_t name_size,
                    camera_mode_t *modes, size_t capacity);
const char *camera_format_name(uint32_t format);
int camera_settings_read(uint32_t *width, uint32_t *height, uint32_t *format);
int camera_settings_write(const camera_mode_t *mode);
camera_device_t *camera_start(const char *path, const camera_mode_t *mode,
                              char *error, size_t error_size);
void camera_stop(camera_device_t *camera);
int camera_poll(camera_device_t *camera, uint8_t *preview, uint32_t width,
                uint32_t height);
int camera_wait(camera_device_t *camera, int timeout_ms);
int camera_capture_jpeg(camera_device_t *camera, uint8_t **output,
                        size_t *output_size);

#endif
