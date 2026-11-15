SUMMARY = "Wi-Fi authentication and DHCP configuration for the gallery"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://photo-viewer-wifi.service file://photo-viewer-wpa.conf file://20-photo-viewer-wlan.network"
S = "${WORKDIR}"
inherit systemd
SYSTEMD_SERVICE:${PN} = "photo-viewer-wifi.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
do_compile[noexec] = "1"
do_install() {
    install -d ${D}${systemd_system_unitdir} ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/photo-viewer-wifi.service \
        ${D}${systemd_system_unitdir}/photo-viewer-wifi.service
    install -m 0644 ${WORKDIR}/20-photo-viewer-wlan.network \
        ${D}${sysconfdir}/systemd/network/20-photo-viewer-wlan.network
    install -m 0600 ${WORKDIR}/photo-viewer-wpa.conf ${D}${sysconfdir}/photo-viewer-wpa.conf
}
RDEPENDS:${PN} = "wpa-supplicant"
FILES:${PN} += "${sysconfdir}/systemd/network/20-photo-viewer-wlan.network ${sysconfdir}/photo-viewer-wpa.conf"
CONFFILES:${PN} += "${sysconfdir}/photo-viewer-wpa.conf"
