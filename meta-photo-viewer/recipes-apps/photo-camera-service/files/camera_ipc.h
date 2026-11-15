#ifndef PHOTO_CAMERA_IPC_H
#define PHOTO_CAMERA_IPC_H

#include "camera_backend.h"
#include <stdint.h>

#define CAMERA_SOCKET "/run/photo-viewer/camera.sock"
#define CAMERA_IPC_MAX_FRAME (32u * 1024u * 1024u)
enum { CAMERA_START = 1, CAMERA_WAIT, CAMERA_POLL, CAMERA_CAPTURE, CAMERA_STOP };

typedef struct {
    uint32_t operation;
    uint32_t width, height;
    int32_t timeout_ms;
    camera_mode_t mode;
    char path[64];
} camera_request_t;

typedef struct {
    int32_t status;
    uint32_t size;
    char error[128];
} camera_response_t;

#endif
