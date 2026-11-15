SUMMARY = "LVGL video player"
require recipes-apps/photo-common/photo-app-common.inc
RDEPENDS:${PN} = "ffmpeg alsa-utils-aplay kernel-module-snd-usb-audio libgles2-mesa libegl-mesa libgbm mesa-megadriver libdrm"
