#include "camera_ipc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct camera_device { int fd; };

static int transfer(int fd, void *data, size_t size, int sending)
{
    unsigned char *position = data;
    while(size) {
        ssize_t count = sending ? send(fd, position, size, MSG_NOSIGNAL) :
                                  recv(fd, position, size, 0);
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return 0;
        position += count;
        size -= (size_t)count;
    }
    return 1;
}

static int request(camera_device_t *camera, camera_request_t *message,
                   camera_response_t *response)
{
    return transfer(camera->fd, message, sizeof(*message), 1) &&
           transfer(camera->fd, response, sizeof(*response), 0);
}

camera_device_t *camera_start(const char *path, const camera_mode_t *mode,
                              char *error, size_t error_size)
{
    if(!path || !mode) return NULL;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0) return NULL;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", CAMERA_SOCKET);
    if(connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        if(error && error_size) snprintf(error, error_size, "Camera service unavailable");
        close(fd);
        return NULL;
    }
    camera_device_t *camera = calloc(1, sizeof(*camera));
    if(!camera) { close(fd); return NULL; }
    camera->fd = fd;
    camera_request_t message = {.operation = CAMERA_START, .mode = *mode};
    snprintf(message.path, sizeof(message.path), "%s", path);
    camera_response_t response = {0};
    if(!request(camera, &message, &response) || response.status != 1) {
        if(error && error_size)
            snprintf(error, error_size, "%s", response.error[0] ? response.error :
                     "Could not connect to camera service");
        camera_stop(camera);
        return NULL;
    }
    return camera;
}

void camera_stop(camera_device_t *camera)
{
    if(!camera) return;
    /* Closing the socket also releases the camera if the service crashed. */
    close(camera->fd);
    free(camera);
}

int camera_wait(camera_device_t *camera, int timeout_ms)
{
    if(!camera) return -1;
    camera_request_t message = {.operation = CAMERA_WAIT, .timeout_ms = timeout_ms};
    camera_response_t response = {0};
    return request(camera, &message, &response) ? response.status : -1;
}

int camera_poll(camera_device_t *camera, uint8_t *preview, uint32_t width,
                uint32_t height)
{
    if(!camera || (preview && (!width || !height ||
       (uint64_t)width * height > 1920u * 1080u))) return -1;
    camera_request_t message = {.operation = CAMERA_POLL, .width = preview ? width : 0,
                                .height = preview ? height : 0};
    camera_response_t response = {0};
    if(!request(camera, &message, &response)) return -1;
    size_t expected = preview && response.status == 1 ? (size_t)width * height * 3 : 0;
    if(response.size != expected) return -1;
    if(expected && !transfer(camera->fd, preview, expected, 0)) return -1;
    return response.status;
}

int camera_capture_jpeg(camera_device_t *camera, uint8_t **output,
                        size_t *output_size)
{
    if(!camera || !output || !output_size) return 0;
    *output = NULL;
    *output_size = 0;
    camera_request_t message = {.operation = CAMERA_CAPTURE};
    camera_response_t response = {0};
    if(!request(camera, &message, &response) || response.status != 1 ||
       !response.size || response.size > CAMERA_IPC_MAX_FRAME) return 0;
    uint8_t *frame = malloc(response.size);
    if(!frame) return 0;
    if(!transfer(camera->fd, frame, response.size, 0)) { free(frame); return 0; }
    *output = frame;
    *output_size = response.size;
    return 1;
}
