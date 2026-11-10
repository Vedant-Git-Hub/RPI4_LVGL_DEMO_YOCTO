SUMMARY = "Minimal Raspberry Pi 4 image with an LVGL photo viewer"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb

IMAGE_INSTALL:append = " \
    fbset \
    photo-viewer \
    iw \
    wpa-supplicant \
    avahi-daemon \
"

