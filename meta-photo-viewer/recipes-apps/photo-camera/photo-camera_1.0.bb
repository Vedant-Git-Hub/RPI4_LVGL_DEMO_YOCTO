SUMMARY = "LVGL USB camera application"
require recipes-apps/photo-common/photo-app-common.inc
RDEPENDS:${PN} = "photo-camera-service"
