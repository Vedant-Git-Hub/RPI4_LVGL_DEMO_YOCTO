SUMMARY = "One-time boot splash for the photo gallery"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
FILESEXTRAPATHS:prepend := "${THISDIR}/../photo-common/files:"
SRC_URI = "file://Makefile file://boot_splash.c file://boot-splash.bmp file://photo-viewer-splash.service"
S = "${WORKDIR}"
inherit systemd
SYSTEMD_SERVICE:${PN} = "photo-viewer-splash.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
do_configure[noexec] = "1"
do_compile() {
    oe_runmake photo-viewer-splash
}
do_install() {
    install -d ${D}${bindir} ${D}${datadir}/photo-viewer ${D}${systemd_system_unitdir}
    install -m 0755 photo-viewer-splash ${D}${bindir}/photo-viewer-splash
    install -m 0644 ${WORKDIR}/boot-splash.bmp ${D}${datadir}/photo-viewer/boot-splash.bmp
    install -m 0644 ${WORKDIR}/photo-viewer-splash.service \
        ${D}${systemd_system_unitdir}/photo-viewer-splash.service
}
FILES:${PN} += "${datadir}/photo-viewer/boot-splash.bmp"
