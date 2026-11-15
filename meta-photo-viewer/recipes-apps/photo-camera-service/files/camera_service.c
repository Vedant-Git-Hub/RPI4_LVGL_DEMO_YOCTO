#define _GNU_SOURCE
#include "camera_ipc.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

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

static void *client_main(void *context)
{
    int fd = (int)(intptr_t)context;
    camera_device_t *camera = NULL;
    camera_request_t message;
    while(transfer(fd, &message, sizeof(message), 0)) {
        camera_response_t response = {0};
        uint8_t *frame = NULL;
        size_t frame_size = 0;
        switch(message.operation) {
        case CAMERA_START:
            if(camera) break;
            message.path[sizeof(message.path) - 1] = '\0';
            if(strncmp(message.path, "/dev/video", 10)) break;
            camera = camera_start(message.path, &message.mode, response.error,
                                  sizeof(response.error));
            response.status = camera ? 1 : 0;
            break;
        case CAMERA_WAIT:
            response.status = camera ? camera_wait(camera, message.timeout_ms) : -1;
            break;
        case CAMERA_POLL:
            if(!camera || (uint64_t)message.width * message.height > 1920u * 1080u)
                response.status = -1;
            else {
                if(message.width && message.height) {
                    frame_size = (size_t)message.width * message.height * 3;
                    frame = malloc(frame_size);
                    if(!frame) { response.status = -1; break; }
                }
                response.status = camera_poll(camera, frame, message.width,
                                              message.height);
                if(response.status == 1 && frame) response.size = (uint32_t)frame_size;
            }
            break;
        case CAMERA_CAPTURE:
            if(camera && camera_capture_jpeg(camera, &frame, &frame_size) &&
               frame_size <= CAMERA_IPC_MAX_FRAME) {
                response.status = 1;
                response.size = (uint32_t)frame_size;
            }
            break;
        case CAMERA_STOP:
            camera_stop(camera);
            camera = NULL;
            break;
        default:
            response.status = -1;
        }
        int okay = transfer(fd, &response, sizeof(response), 1);
        if(okay && response.size) okay = transfer(fd, frame, response.size, 1);
        free(frame);
        if(!okay) break;
    }
    camera_stop(camera);
    close(fd);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    if(mkdir("/run/photo-viewer", 0755) && errno != EEXIST) return 1;
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(listener < 0) return 1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", CAMERA_SOCKET);
    unlink(CAMERA_SOCKET);
    if(bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
       listen(listener, 8)) { close(listener); return 1; }
    chmod(CAMERA_SOCKET, 0660);
    for(;;) {
        int fd = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
        if(fd < 0) { if(errno == EINTR) continue; return 1; }
        pthread_t thread;
        if(pthread_create(&thread, NULL, client_main, (void *)(intptr_t)fd))
            close(fd);
        else pthread_detach(thread);
    }
}
