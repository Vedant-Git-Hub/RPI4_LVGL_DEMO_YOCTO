SUMMARY = "LVGL touchscreen photo gallery and browser media server"
LICENSE = "MIT"
PR = "r10"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = "lvgl"

SRC_URI = " \
    file://main.c \
    file://media_server.c \
    file://media_server.h \
    file://image_convert.c \
    file://image_convert.h \
    file://image_settings.c \
    file://image_settings.h \
    file://wifi_control.c \
    file://wifi_control.h \
    file://index.html \
    file://Makefile \
    file://panel-simple.conf \
    file://photo-viewer.service \
    file://photo-viewer-wifi.service \
    file://photo-viewer-wpa.conf \
    file://20-photo-viewer-wlan.network \
"

S = "${WORKDIR}"

inherit pkgconfig systemd

SYSTEMD_SERVICE:${PN} = "photo-viewer.service photo-viewer-wifi.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

# This single-source Makefile has no generated configuration or clean target.
do_configure[noexec] = "1"

do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 photo-viewer ${D}${bindir}/photo-viewer

    # Original photos live separately from application files.
    install -d ${D}/opt/photo-viewer/photos
    install -d ${D}${datadir}/photo-viewer
    install -m 0644 ${WORKDIR}/index.html ${D}${datadir}/photo-viewer/index.html

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/photo-viewer.service \
        ${D}${systemd_system_unitdir}/photo-viewer.service
    install -m 0644 ${WORKDIR}/photo-viewer-wifi.service \
        ${D}${systemd_system_unitdir}/photo-viewer-wifi.service
    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/20-photo-viewer-wlan.network \
        ${D}${sysconfdir}/systemd/network/20-photo-viewer-wlan.network
    install -m 0600 ${WORKDIR}/photo-viewer-wpa.conf ${D}${sysconfdir}/photo-viewer-wpa.conf

    # This is a full-screen appliance.  Mask the local virtual-console getty
    # so it cannot draw a login prompt over the LVGL framebuffer.  Do not
    # mask serial gettys: they remain available for service/debug access.
    install -d ${D}${sysconfdir}/systemd/system
    ln -s /dev/null ${D}${sysconfdir}/systemd/system/getty@tty1.service

    # The Waveshare overlay falls back to the generic "panel-dsi" compatible.
    # Its first compatible does not trigger module autoload, so load the
    # generic panel driver explicitly before the display service starts.
    install -d ${D}${sysconfdir}/modules-load.d
    install -m 0644 ${WORKDIR}/panel-simple.conf \
        ${D}${sysconfdir}/modules-load.d/photo-viewer-panel.conf
}

RDEPENDS:${PN} += "lvgl kernel-module-panel-simple imagemagick wpa-supplicant wpa-supplicant-cli"
FILES:${PN} += " \
    /opt/photo-viewer/photos \
    ${datadir}/photo-viewer/index.html \
    ${sysconfdir}/systemd/network/20-photo-viewer-wlan.network \
    ${sysconfdir}/photo-viewer-wpa.conf \
    ${sysconfdir}/modules-load.d/photo-viewer-panel.conf \
    ${sysconfdir}/systemd/system/getty@tty1.service \
"

CONFFILES:${PN} += "${sysconfdir}/photo-viewer-wpa.conf"

