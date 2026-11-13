#include "camera_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <jpeglib.h>
#include <linux/videodev2.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define CAMERA_BUFFERS 4
#define CAMERA_MAX_PIXELS (4096u * 2160u)

typedef struct {
    void *address;
    size_t length;
} camera_buffer_t;

struct camera_device {
    int fd;
    int streaming;
    camera_mode_t mode;
    uint32_t stride;
    camera_buffer_t buffers[CAMERA_BUFFERS];
    unsigned int buffer_count;
    uint8_t *last_frame;
    size_t last_capacity;
    size_t last_size;
    int last_valid;
};

typedef struct {
    struct jpeg_error_mgr base;
    jmp_buf jump;
} jpeg_error_t;

static void jpeg_failure(j_common_ptr common)
{
    jpeg_error_t *error = (jpeg_error_t *)common->err;
    longjmp(error->jump, 1);
}

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

static void error_text(char *output, size_t size, const char *operation)
{
    if(output && size) snprintf(output, size, "%s: %s", operation, strerror(errno));
}

camera_device_t *camera_start(const char *path, const camera_mode_t *mode,
                              char *error, size_t error_size)
{
    if(!path || !mode) return NULL;
    camera_device_t *camera = calloc(1, sizeof(*camera));
    if(!camera) return NULL;
    camera->fd = -1;
    camera->fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if(camera->fd < 0) { error_text(error, error_size, "Open camera"); goto fail; }
    struct v4l2_format format = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    format.fmt.pix.width = mode->width;
    format.fmt.pix.height = mode->height;
    format.fmt.pix.pixelformat = mode->format;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if(camera_ioctl(camera->fd, VIDIOC_S_FMT, &format)) {
        error_text(error, error_size, "Set resolution"); goto fail;
    }
    camera->mode = (camera_mode_t){format.fmt.pix.width, format.fmt.pix.height,
                                   format.fmt.pix.pixelformat};
    camera->stride = format.fmt.pix.bytesperline;
    if(!supported_format(camera->mode.format) ||
       (uint64_t)camera->mode.width * camera->mode.height > CAMERA_MAX_PIXELS ||
       (camera->mode.format == V4L2_PIX_FMT_YUYV &&
        (camera->mode.width & 1u || camera->stride < camera->mode.width * 2))) {
        if(error && error_size) snprintf(error, error_size, "Camera returned an unsupported format");
        goto fail;
    }
    struct v4l2_requestbuffers request = {.count = CAMERA_BUFFERS,
                                           .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                           .memory = V4L2_MEMORY_MMAP};
    if(camera_ioctl(camera->fd, VIDIOC_REQBUFS, &request) || request.count < 2) {
        error_text(error, error_size, "Allocate camera buffers"); goto fail;
    }
    camera->buffer_count = request.count < CAMERA_BUFFERS ? request.count : CAMERA_BUFFERS;
    for(unsigned int i = 0; i < camera->buffer_count; i++) {
        struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                     .memory = V4L2_MEMORY_MMAP, .index = i};
        if(camera_ioctl(camera->fd, VIDIOC_QUERYBUF, &buffer)) {
            error_text(error, error_size, "Map camera buffer"); goto fail;
        }
        camera->buffers[i].length = buffer.length;
        camera->buffers[i].address = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
                                           MAP_SHARED, camera->fd, buffer.m.offset);
        if(camera->buffers[i].address == MAP_FAILED) {
            camera->buffers[i].address = NULL;
            error_text(error, error_size, "Map camera buffer"); goto fail;
        }
        if(camera_ioctl(camera->fd, VIDIOC_QBUF, &buffer)) {
            error_text(error, error_size, "Queue camera buffer"); goto fail;
        }
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(camera_ioctl(camera->fd, VIDIOC_STREAMON, &type)) {
        error_text(error, error_size, "Start camera"); goto fail;
    }
    camera->streaming = 1;
    return camera;
fail:
    camera_stop(camera);
    return NULL;
}

void camera_stop(camera_device_t *camera)
{
    if(!camera) return;
    if(camera->streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        camera_ioctl(camera->fd, VIDIOC_STREAMOFF, &type);
    }
    for(unsigned int i = 0; i < camera->buffer_count; i++)
        if(camera->buffers[i].address)
            munmap(camera->buffers[i].address, camera->buffers[i].length);
    if(camera->fd >= 0) close(camera->fd);
    free(camera->last_frame);
    free(camera);
}

static uint8_t clamp_rgb(int value)
{
    return value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value;
}

static void yuyv_pixel(const uint8_t *data, uint32_t stride, uint32_t x,
                       uint32_t y, uint8_t *rgb)
{
    const uint8_t *pair = data + (size_t)y * stride + (x & ~1u) * 2;
    int luma = pair[(x & 1u) ? 2 : 0] - 16;
    int blue = pair[1] - 128;
    int red = pair[3] - 128;
    if(luma < 0) luma = 0;
    rgb[0] = clamp_rgb((298 * luma + 409 * red + 128) >> 8);
    rgb[1] = clamp_rgb((298 * luma - 100 * blue - 208 * red + 128) >> 8);
    rgb[2] = clamp_rgb((298 * luma + 516 * blue + 128) >> 8);
}

static void preview_geometry(uint32_t src_w, uint32_t src_h, uint32_t dst_w,
                             uint32_t dst_h, uint32_t *draw_w, uint32_t *draw_h)
{
    *draw_w = dst_w;
    *draw_h = (uint32_t)((uint64_t)src_h * dst_w / src_w);
    if(*draw_h > dst_h) {
        *draw_h = dst_h;
        *draw_w = (uint32_t)((uint64_t)src_w * dst_h / src_h);
    }
    if(!*draw_w) *draw_w = 1;
    if(!*draw_h) *draw_h = 1;
}

static int preview_yuyv(camera_device_t *camera, uint8_t *preview,
                        uint32_t width, uint32_t height)
{
    if(camera->last_size < (size_t)camera->stride * camera->mode.height) return 0;
    memset(preview, 0, (size_t)width * height * 3);
    uint32_t draw_w, draw_h;
    preview_geometry(camera->mode.width, camera->mode.height, width, height,
                     &draw_w, &draw_h);
    uint32_t left = (width - draw_w) / 2, top = (height - draw_h) / 2;
    for(uint32_t y = 0; y < draw_h; y++) {
        uint32_t source_y = (uint32_t)((uint64_t)y * camera->mode.height / draw_h);
        for(uint32_t x = 0; x < draw_w; x++) {
            uint32_t source_x = (uint32_t)((uint64_t)x * camera->mode.width / draw_w);
            uint8_t *pixel = preview + ((size_t)(y + top) * width + x + left) * 3;
            yuyv_pixel(camera->last_frame, camera->stride, source_x, source_y,
                       pixel);
            uint8_t swap = pixel[0]; pixel[0] = pixel[2]; pixel[2] = swap;
        }
    }
    return 1;
}

static int preview_jpeg(camera_device_t *camera, uint8_t *preview,
                        uint32_t width, uint32_t height)
{
    struct jpeg_decompress_struct decoder = {0};
    jpeg_error_t error;
    uint8_t *volatile pixels = NULL;
    decoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpeg_failure;
    if(setjmp(error.jump)) {
        free(pixels);
        jpeg_destroy_decompress(&decoder);
        return 0;
    }
    jpeg_create_decompress(&decoder);
    jpeg_mem_src(&decoder, camera->last_frame, camera->last_size);
    jpeg_read_header(&decoder, TRUE);
    decoder.out_color_space = JCS_RGB;
    decoder.scale_num = 1;
    decoder.scale_denom = 1;
    while(decoder.scale_denom < 8 &&
          (decoder.image_width / decoder.scale_denom > width * 2 ||
           decoder.image_height / decoder.scale_denom > height * 2))
        decoder.scale_denom *= 2;
    jpeg_start_decompress(&decoder);
    uint32_t src_w = decoder.output_width, src_h = decoder.output_height;
    if(!src_w || !src_h || (uint64_t)src_w * src_h > CAMERA_MAX_PIXELS ||
       decoder.output_components != 3) {
        jpeg_destroy_decompress(&decoder);
        return 0;
    }
    pixels = malloc((size_t)src_w * src_h * 3);
    if(!pixels) { jpeg_destroy_decompress(&decoder); return 0; }
    while(decoder.output_scanline < src_h) {
        JSAMPROW row = pixels + (size_t)decoder.output_scanline * src_w * 3;
        jpeg_read_scanlines(&decoder, &row, 1);
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    memset(preview, 0, (size_t)width * height * 3);
    uint32_t draw_w, draw_h;
    preview_geometry(src_w, src_h, width, height, &draw_w, &draw_h);
    uint32_t left = (width - draw_w) / 2, top = (height - draw_h) / 2;
    for(uint32_t y = 0; y < draw_h; y++) {
        uint32_t source_y = (uint32_t)((uint64_t)y * src_h / draw_h);
        for(uint32_t x = 0; x < draw_w; x++) {
            uint32_t source_x = (uint32_t)((uint64_t)x * src_w / draw_w);
            const uint8_t *source = pixels + ((size_t)source_y * src_w + source_x) * 3;
            uint8_t *destination = preview + ((size_t)(y + top) * width + x + left) * 3;
            destination[0] = source[2];
            destination[1] = source[1];
            destination[2] = source[0];
        }
    }
    free(pixels);
    return 1;
}

int camera_poll(camera_device_t *camera, uint8_t *preview, uint32_t width,
                uint32_t height)
{
    if(!camera || !preview || !width || !height) return -1;
    int received = 0;
    for(unsigned int i = 0; i < camera->buffer_count; i++) {
        struct v4l2_buffer buffer = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                     .memory = V4L2_MEMORY_MMAP};
        if(camera_ioctl(camera->fd, VIDIOC_DQBUF, &buffer)) {
            if(errno == EAGAIN) break;
            return -1;
        }
        if(buffer.index >= camera->buffer_count ||
           buffer.bytesused > camera->buffers[buffer.index].length) return -1;
        if(buffer.bytesused) {
            if(buffer.bytesused > camera->last_capacity) {
                uint8_t *next = realloc(camera->last_frame, buffer.bytesused);
                if(!next) return -1;
                camera->last_frame = next;
                camera->last_capacity = buffer.bytesused;
            }
            memcpy(camera->last_frame, camera->buffers[buffer.index].address,
                   buffer.bytesused);
            camera->last_size = buffer.bytesused;
            received = 1;
        }
        if(camera_ioctl(camera->fd, VIDIOC_QBUF, &buffer)) return -1;
    }
    if(!received) return 0;
    int valid = camera->mode.format == V4L2_PIX_FMT_YUYV ?
                preview_yuyv(camera, preview, width, height) :
                preview_jpeg(camera, preview, width, height);
    camera->last_valid = valid;
    return valid ? 1 : 0;
}

int camera_capture_jpeg(camera_device_t *camera, uint8_t **output,
                        size_t *output_size)
{
    if(!camera || !output || !output_size || !camera->last_size ||
       !camera->last_valid) return 0;
    *output = NULL;
    *output_size = 0;
    if(camera->mode.format != V4L2_PIX_FMT_YUYV) {
        uint8_t *copy = malloc(camera->last_size);
        if(!copy) return 0;
        memcpy(copy, camera->last_frame, camera->last_size);
        *output = copy;
        *output_size = camera->last_size;
        return 1;
    }
    if(camera->last_size < (size_t)camera->stride * camera->mode.height) return 0;
    struct jpeg_compress_struct encoder = {0};
    jpeg_error_t error;
    uint8_t *row = NULL;
    unsigned char *jpeg = NULL;
    unsigned long jpeg_size = 0;
    encoder.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpeg_failure;
    if(setjmp(error.jump)) {
        free(row);
        free(jpeg);
        jpeg_destroy_compress(&encoder);
        return 0;
    }
    jpeg_create_compress(&encoder);
    jpeg_mem_dest(&encoder, &jpeg, &jpeg_size);
    encoder.image_width = camera->mode.width;
    encoder.image_height = camera->mode.height;
    encoder.input_components = 3;
    encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, 90, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    row = malloc((size_t)camera->mode.width * 3);
    if(!row) { jpeg_destroy_compress(&encoder); free(jpeg); return 0; }
    while(encoder.next_scanline < encoder.image_height) {
        for(uint32_t x = 0; x < camera->mode.width; x++)
            yuyv_pixel(camera->last_frame, camera->stride, x,
                       encoder.next_scanline, row + (size_t)x * 3);
        JSAMPROW scanline = row;
        jpeg_write_scanlines(&encoder, &scanline, 1);
    }
    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);
    free(row);
    *output = jpeg;
    *output_size = jpeg_size;
    if(jpeg_size == 0) {
        free(jpeg);
        *output = NULL;
        return 0;
    }
    return 1;
}
