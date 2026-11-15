SUMMARY = "USB camera owner and Unix socket IPC service"
require photo-app-common.inc
SRC_URI:append = " file://photo-camera-service.service"
inherit systemd
SYSTEMD_SERVICE:${PN} = "photo-camera-service.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
RDEPENDS:${PN} = "kernel-module-uvcvideo"
do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/photo-camera-service.service \
        ${D}${systemd_system_unitdir}/photo-camera-service.service
}
