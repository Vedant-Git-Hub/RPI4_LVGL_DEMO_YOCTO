LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
PHOTO_VIEWER_7INCH ??= "0"
DEPENDS = "lvgl libjpeg-turbo"
inherit pkgconfig
S = "${WORKDIR}"
# Each executable has its own recipe and files/ directory. Sources shared by
# several executables are unpacked from the sibling directories into WORKDIR.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:${THISDIR}/../photo-common/files:${THISDIR}/../photo-launcher/files:${THISDIR}/../photo-images/files:${THISDIR}/../photo-videos/files:${THISDIR}/../photo-settings/files:${THISDIR}/../photo-camera/files:${THISDIR}/../photo-camera-service/files:${THISDIR}/../photo-web/files:"
SRC_URI = " \
    file://Makefile file://display_layout.h file://main.c file://launcher.c \
    file://web_server_main.c file://camera_service.c \
    file://media_server.c file://media_server.h \
    file://camera_ipc.h file://camera_ipc_client.c file://camera_catalog.c \
    file://camera_backend.c file://camera_backend.h file://camera_ui.c file://camera_ui.h \
    file://video_backend.c file://video_backend.h file://video_ui.c file://video_ui.h \
    file://image_convert.c file://image_convert.h \
    file://image_settings.c file://image_settings.h \
    file://wifi_control.c file://wifi_control.h \
    file://hostname_settings.c file://hostname_settings.h \
    file://audio_control.c file://audio_control.h \
    file://selection_state.c file://selection_state.h \
"
do_configure[noexec] = "1"

do_compile() {
    oe_runmake ${BPN} PHOTO_VIEWER_7INCH="${PHOTO_VIEWER_7INCH}"
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${BPN} ${D}${bindir}/${BPN}
}
