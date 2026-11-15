SUMMARY = "Photo viewer system integration and boot assets"
LICENSE = "MIT"
PR = "r0"
PHOTO_VIEWER_7INCH ??= "0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://panel-simple.conf \
    file://photo-viewer.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "photo-viewer.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    # Original photos live separately from application files.
    install -d ${D}/opt/photo-viewer/photos
    install -d ${D}/opt/photo-viewer/videos

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/photo-viewer.service \
        ${D}${systemd_system_unitdir}/photo-viewer.service

    # This is a full-screen appliance.  Mask the local virtual-console getty
    # so it cannot draw a login prompt over the LVGL framebuffer.  Do not
    # mask serial gettys: they remain available for service/debug access.
    install -d ${D}${sysconfdir}/systemd/system
    ln -s /dev/null ${D}${sysconfdir}/systemd/system/getty@tty1.service

    # Load the selected panel driver before the display service starts.
    # The 4.3-inch overlay needs panel-simple; the 7-inch C panel uses the
    # dedicated Waveshare panel driver and Goodix touch controller.
    install -d ${D}${sysconfdir}/modules-load.d
    if [ "${PHOTO_VIEWER_7INCH}" = "1" ]; then
        printf 'panel-waveshare-dsi\ngoodix_ts\n' > ${D}${sysconfdir}/modules-load.d/photo-viewer-panel.conf
    else
        install -m 0644 ${WORKDIR}/panel-simple.conf \
            ${D}${sysconfdir}/modules-load.d/photo-viewer-panel.conf
    fi
    printf 'snd_bcm2835\n' > ${D}${sysconfdir}/modules-load.d/photo-viewer-audio.conf
    install -d ${D}${sysconfdir}/modprobe.d
    printf 'options snd_bcm2835 enable_headphones=1 enable_hdmi=0\n' > \
        ${D}${sysconfdir}/modprobe.d/photo-viewer-audio.conf
}

RDEPENDS:${PN} += "photo-launcher photo-web photo-camera-service photo-splash photo-wifi lvgl kernel-module-panel-simple kernel-module-uvcvideo kernel-module-snd-bcm2835 kernel-module-snd-usb-audio imagemagick wpa-supplicant wpa-supplicant-cli ffmpeg alsa-utils-aplay alsa-utils-amixer"
RDEPENDS:${PN}:append = " ${@'kernel-module-panel-waveshare-dsi kernel-module-goodix-ts' if d.getVar('PHOTO_VIEWER_7INCH') == '1' else ''}"
FILES:${PN} += " \
    /opt/photo-viewer/photos \
    /opt/photo-viewer/videos \
    ${sysconfdir}/modules-load.d/photo-viewer-panel.conf \
    ${sysconfdir}/modules-load.d/photo-viewer-audio.conf \
    ${sysconfdir}/modprobe.d/photo-viewer-audio.conf \
    ${sysconfdir}/systemd/system/getty@tty1.service \
"
