# Keep fbdev for the stable launcher and apps. The experimental video player
# uses SDL's KMSDRM/GLES renderer with two screen-sized LVGL draw buffers.
PACKAGECONFIG:remove = "drm"
PACKAGECONFIG:append = " fbdev sdl"
LVGL_CONFIG_SDL_FULLSCREEN = "1"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://0001-bmp-fix-file-open-result.patch file://0002-gif-accept-87a-and-local-palettes.patch file://0003-sdl-require-accelerated-renderer.patch file://0004-link-sdl-runtime.patch"

# The gallery, settings, and camera keyboards exhaust LVGL's default 64 KiB
# object heap during startup. Keep enough headroom for menus and image views.
# ImageMagick renders originals into temporary BMP frames for LVGL.
do_configure:append() {
    sed -i \
        -e 's/^    #define LV_MEM_SIZE (64 \* 1024U)/    #define LV_MEM_SIZE (512 * 1024U)/' \
        -e 's/^#define LV_USE_FS_STDIO 0/#define LV_USE_FS_STDIO 1/' \
        -e "s/^    #define LV_FS_STDIO_LETTER .*$/    #define LV_FS_STDIO_LETTER 'A'/" \
        -e 's/^#define LV_USE_BMP 0/#define LV_USE_BMP 1/' \
        -e 's/^#define LV_USE_GIF 0/#define LV_USE_GIF 1/' \
        -e 's/^#define LV_USE_DRAW_SDL 1/#define LV_USE_DRAW_SDL 0/' \
        "${S}/lv_conf.h"
}

