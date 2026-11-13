FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " ${@'file://0001-waveshare-7inch-c-touch-resolution.patch' if d.getVar('PHOTO_VIEWER_7INCH') == '1' else ''}"
