SUMMARY = "LVGL camera, network, image, video and audio settings"
require recipes-apps/photo-common/photo-app-common.inc
RDEPENDS:${PN} = "wpa-supplicant-cli imagemagick ffmpeg"
