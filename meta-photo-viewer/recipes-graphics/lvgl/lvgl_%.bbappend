# Build only the framebuffer backend used by this appliance. The upstream
# recipe defaults to DRM too, which would add an unnecessary libdrm link
# dependency to this framebuffer-only application.
PACKAGECONFIG:remove = "drm"
PACKAGECONFIG:append = " fbdev"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://0001-bmp-fix-file-open-result.patch"

# ImageMagick renders originals into temporary BMP frames for LVGL.
do_configure:append() {
    sed -i \
        -e 's/^#define LV_USE_FS_STDIO 0/#define LV_USE_FS_STDIO 1/' \
        -e "s/^    #define LV_FS_STDIO_LETTER .*$/    #define LV_FS_STDIO_LETTER 'A'/" \
        -e 's/^#define LV_USE_BMP 0/#define LV_USE_BMP 1/' \
        "${S}/lv_conf.h"
}

