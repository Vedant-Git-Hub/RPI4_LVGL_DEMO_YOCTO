SUMMARY = "Independent photo and video web server"
require recipes-apps/photo-common/photo-app-common.inc
SRC_URI:append = " file://photo-web.service file://index.html"
inherit systemd
SYSTEMD_SERVICE:${PN} = "photo-web.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
RDEPENDS:${PN} = "photo-camera-service imagemagick"
do_install:append() {
    install -d ${D}${systemd_system_unitdir} ${D}${datadir}/photo-viewer
    install -m 0644 ${WORKDIR}/photo-web.service ${D}${systemd_system_unitdir}/photo-web.service
    install -m 0644 ${WORKDIR}/index.html ${D}${datadir}/photo-viewer/index.html
}
FILES:${PN} += "${datadir}/photo-viewer/index.html"
