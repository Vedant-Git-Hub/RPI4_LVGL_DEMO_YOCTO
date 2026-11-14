/* Draw the startup artwork as soon as Linux exposes the panel framebuffer.
 * The LVGL application uses the same BMP until its gallery is ready. */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define SPLASH_FILE "/usr/share/photo-viewer/boot-splash.bmp"

static uint16_t read16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t read32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static uint8_t *load_artwork(uint32_t *width, uint32_t *height, size_t *stride)
{
    FILE *file = fopen(SPLASH_FILE, "rb");
    if(!file) return NULL;
    uint8_t header[54];
    uint8_t *pixels = NULL;
    if(fread(header, 1, sizeof(header), file) != sizeof(header) ||
       header[0] != 'B' || header[1] != 'M' || read32(header + 14) < 40 ||
       read16(header + 26) != 1 || read16(header + 28) != 24 ||
       read32(header + 30) != 0) goto done;
    *width = read32(header + 18);
    *height = read32(header + 22);
    if(*width < 1 || *width > 1024 || *height < 1 || *height > 512) goto done;
    *stride = ((size_t)*width * 3 + 3) & ~(size_t)3;
    pixels = malloc(*stride * *height);
    if(!pixels || fseek(file, (long)read32(header + 10), SEEK_SET) ||
       fread(pixels, *stride, *height, file) != *height) {
        free(pixels);
        pixels = NULL;
    }
done:
    fclose(file);
    return pixels;
}

static uint32_t channel(uint8_t value, struct fb_bitfield field)
{
    if(!field.length) return 0;
    uint32_t maximum = (1u << field.length) - 1;
    return ((uint32_t)value * maximum / 255) << field.offset;
}

static void write_pixel(uint8_t *destination, uint32_t value, unsigned int bytes)
{
    for(unsigned int i = 0; i < bytes; i++)
        destination[i] = (uint8_t)(value >> (8 * i));
}

static int paint(int fd, const uint8_t *artwork, uint32_t image_width,
                 uint32_t image_height, size_t image_stride)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    if(ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) return 0;
    unsigned int bytes = var.bits_per_pixel / 8;
    if((bytes != 2 && bytes != 3 && bytes != 4) ||
       var.xres < 320 || var.yres < 240 || var.xres > 4096 || var.yres > 2160 ||
       var.xoffset + var.xres > var.xres_virtual ||
       var.yoffset + var.yres > var.yres_virtual ||
       var.red.length > 10 || var.green.length > 10 || var.blue.length > 10 ||
       var.red.offset + var.red.length > var.bits_per_pixel ||
       var.green.offset + var.green.length > var.bits_per_pixel ||
       var.blue.offset + var.blue.length > var.bits_per_pixel ||
       var.transp.length > 10 ||
       var.transp.offset + var.transp.length > var.bits_per_pixel ||
       ((uint64_t)(var.yoffset + var.yres - 1) * fix.line_length +
        (uint64_t)(var.xoffset + var.xres) * bytes > fix.smem_len)) return 0;

    uint8_t *framebuffer = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, 0);
    if(framebuffer == MAP_FAILED) return 0;
    uint32_t opaque = var.transp.length ?
                      ((1u << var.transp.length) - 1) << var.transp.offset : 0;
    for(uint32_t y = 0; y < var.yres; y++) {
        uint8_t *row = framebuffer + (size_t)(y + var.yoffset) * fix.line_length +
                       (size_t)var.xoffset * bytes;
        for(uint32_t x = 0; x < var.xres; x++) write_pixel(row + (size_t)x * bytes, opaque, bytes);
    }

    uint32_t scale_x = var.xres * 256 / 800;
    uint32_t scale_y = var.yres * 256 / 480;
    uint32_t scale = scale_x < scale_y ? scale_x : scale_y;
    uint32_t draw_width = image_width * scale / 256;
    uint32_t draw_height = image_height * scale / 256;
    if(!draw_width || !draw_height || draw_width > var.xres || draw_height > var.yres) {
        munmap(framebuffer, fix.smem_len);
        return 0;
    }
    uint32_t left = (var.xres - draw_width) / 2;
    uint32_t top = (var.yres - draw_height) / 2;
    for(uint32_t y = 0; y < draw_height; y++) {
        uint32_t source_y = (uint32_t)((uint64_t)y * image_height / draw_height);
        const uint8_t *source_row = artwork + (size_t)(image_height - source_y - 1) * image_stride;
        uint8_t *target_row = framebuffer + (size_t)(top + y + var.yoffset) * fix.line_length +
                              (size_t)(left + var.xoffset) * bytes;
        for(uint32_t x = 0; x < draw_width; x++) {
            uint32_t source_x = (uint32_t)((uint64_t)x * image_width / draw_width);
            const uint8_t *bgr = source_row + (size_t)source_x * 3;
            uint32_t color = opaque | channel(bgr[2], var.red) |
                             channel(bgr[1], var.green) | channel(bgr[0], var.blue);
            write_pixel(target_row + (size_t)x * bytes, color, bytes);
        }
    }
    munmap(framebuffer, fix.smem_len);
    return 1;
}

int main(void)
{
    uint32_t width = 0, height = 0;
    size_t stride = 0;
    uint8_t *artwork = load_artwork(&width, &height, &stride);
    if(!artwork) {
        fprintf(stderr, "photo-viewer-splash: could not read splash artwork\n");
        return 1;
    }
    int fd = -1;
    for(int attempt = 0; attempt < 300; attempt++) {
        fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
        if(fd >= 0) break;
        usleep(100000);
    }
    if(fd < 0) {
        fprintf(stderr, "photo-viewer-splash: framebuffer unavailable: %s\n", strerror(errno));
        free(artwork);
        return 1;
    }
    int okay = paint(fd, artwork, width, height, stride);
    if(!okay) fprintf(stderr, "photo-viewer-splash: unsupported framebuffer format\n");
    close(fd);
    free(artwork);
    return okay ? 0 : 1;
}
