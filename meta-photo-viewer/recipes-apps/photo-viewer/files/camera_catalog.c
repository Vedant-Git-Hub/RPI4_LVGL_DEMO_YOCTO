#include "camera_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CAMERA_MAX_PIXELS (4096u * 2160u)
#define CAMERA_CONFIG "/opt/photo-viewer/camera.conf"

static int camera_ioctl(int fd, unsigned long request, void *argument)
{
    int result;
    do result = ioctl(fd, request, argument);
    while(result < 0 && errno == EINTR);
    return result;
}

static int supported_format(uint32_t format)
{
    return format == V4L2_PIX_FMT_MJPEG || format == V4L2_PIX_FMT_JPEG ||
           format == V4L2_PIX_FMT_YUYV;
}

const char *camera_format_name(uint32_t format)
{
    return format == V4L2_PIX_FMT_YUYV ? "YUYV" : "MJPEG";
}

int camera_settings_read(uint32_t *width, uint32_t *height, uint32_t *format)
{
    if(!width || !height || !format) return 0;
    FILE *file = fopen(CAMERA_CONFIG, "r");
    if(!file) return 0;
    *format = 0; /* Older settings files recorded only the resolution. */
    int valid = fscanf(file, "%u %u %u", width, height, format) >= 2;
    fclose(file);
    return valid;
}

int camera_settings_write(const camera_mode_t *mode)
{
    if(!mode) return 0;
    char temporary[] = CAMERA_CONFIG ".XXXXXX";
    int descriptor = mkstemp(temporary);
    if(descriptor < 0) return 0;
    FILE *file = fdopen(descriptor, "w");
    if(!file) { close(descriptor); unlink(temporary); return 0; }
    int okay = fprintf(file, "%u %u %u\n", mode->width, mode->height,
                       mode->format) > 0 &&
               fflush(file) == 0 && fsync(descriptor) == 0;
    if(fclose(file)) okay = 0;
    if(okay) okay = rename(temporary, CAMERA_CONFIG) == 0;
    if(!okay) unlink(temporary);
    return okay;
}

static void add_mode(camera_mode_t *modes, size_t capacity, int *count,
                     uint32_t width, uint32_t height, uint32_t format)
{
    if(width < 160 || height < 120 || !width || !height ||
       (uint64_t)width * height > CAMERA_MAX_PIXELS) return;
    for(int i = 0; i < *count; i++) {
        if(modes[i].width == width && modes[i].height == height &&
           modes[i].format == format) return;
    }
    if((size_t)*count < capacity) modes[(*count)++] = (camera_mode_t){width, height, format};
}

static void try_mode(int fd, camera_mode_t *modes, size_t capacity, int *count,
                     uint32_t width, uint32_t height, uint32_t format)
{
    struct v4l2_format request = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    request.fmt.pix.width = width;
    request.fmt.pix.height = height;
    request.fmt.pix.pixelformat = format;
    request.fmt.pix.field = V4L2_FIELD_ANY;
    if(!camera_ioctl(fd, VIDIOC_TRY_FMT, &request) &&
       request.fmt.pix.width == width && request.fmt.pix.height == height &&
       request.fmt.pix.pixelformat == format)
        add_mode(modes, capacity, count, width, height, format);
}

static int mode_order(const void *left, const void *right)
{
    const camera_mode_t *a = left, *b = right;
    uint64_t a_pixels = (uint64_t)a->width * a->height;
    uint64_t b_pixels = (uint64_t)b->width * b->height;
    if(a_pixels != b_pixels) return a_pixels < b_pixels ? -1 : 1;
    if(a->width != b->width) return a->width < b->width ? -1 : 1;
    int a_rank = a->format == V4L2_PIX_FMT_MJPEG ? 0 :
                 a->format == V4L2_PIX_FMT_JPEG ? 1 : 2;
    int b_rank = b->format == V4L2_PIX_FMT_MJPEG ? 0 :
                 b->format == V4L2_PIX_FMT_JPEG ? 1 : 2;
    if(a_rank != b_rank) return a_rank < b_rank ? -1 : 1;
    return 0;
}

int camera_discover(char *path, size_t path_size, char *name, size_t name_size,
                    camera_mode_t *modes, size_t capacity)
{
    if(!path || !path_size || !name || !name_size || !modes || !capacity) return 0;
    for(unsigned int node = 0; node < 16; node++) {
        char candidate[32];
        snprintf(candidate, sizeof(candidate), "/dev/video%u", node);
        int fd = open(candidate, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        struct v4l2_capability capabilities = {0};
        if(camera_ioctl(fd, VIDIOC_QUERYCAP, &capabilities)) { close(fd); continue; }
        uint32_t flags = capabilities.capabilities & V4L2_CAP_DEVICE_CAPS ?
                         capabilities.device_caps : capabilities.capabilities;
        if(!(flags & V4L2_CAP_VIDEO_CAPTURE) || !(flags & V4L2_CAP_STREAMING)) {
            close(fd);
            continue;
        }
        int count = 0;
        for(unsigned int format_index = 0; ; format_index++) {
            struct v4l2_fmtdesc format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                          .index = format_index};
            if(camera_ioctl(fd, VIDIOC_ENUM_FMT, &format)) break;
            if(!supported_format(format.pixelformat)) continue;
            int found_sizes = 0;
            for(unsigned int size_index = 0; ; size_index++) {
                struct v4l2_frmsizeenum size = {.index = size_index,
                                                .pixel_format = format.pixelformat};
                if(camera_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size)) break;
                found_sizes = 1;
                if(size.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                    add_mode(modes, capacity, &count, size.discrete.width,
                             size.discrete.height, format.pixelformat);
                else {
                    const uint32_t standard[][2] = {
                        {320, 240}, {640, 480}, {800, 600}, {1024, 768},
                        {1280, 720}, {1280, 960}, {1920, 1080},
                        {2560, 1440}, {3840, 2160}
                    };
                    try_mode(fd, modes, capacity, &count, size.stepwise.min_width,
                             size.stepwise.min_height, format.pixelformat);
                    for(size_t i = 0; i < sizeof(standard) / sizeof(standard[0]); i++)
                        try_mode(fd, modes, capacity, &count, standard[i][0],
                                 standard[i][1], format.pixelformat);
                    try_mode(fd, modes, capacity, &count, size.stepwise.max_width,
                             size.stepwise.max_height, format.pixelformat);
                    break;
                }
            }
            if(!found_sizes) {
                struct v4l2_format current = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
                if(!camera_ioctl(fd, VIDIOC_G_FMT, &current))
                    try_mode(fd, modes, capacity, &count, current.fmt.pix.width,
                             current.fmt.pix.height, format.pixelformat);
            }
        }
        close(fd);
        if(!count) continue;
        qsort(modes, (size_t)count, sizeof(modes[0]), mode_order);
        snprintf(path, path_size, "%s", candidate);
        snprintf(name, name_size, "%.*s", (int)sizeof(capabilities.card),
                 capabilities.card);
        return count;
    }
    path[0] = '\0';
    name[0] = '\0';
    return 0;
}

