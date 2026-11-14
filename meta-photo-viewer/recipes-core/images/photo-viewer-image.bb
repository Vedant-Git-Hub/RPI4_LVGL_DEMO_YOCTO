SUMMARY = "Minimal Raspberry Pi 4 image with an LVGL photo viewer"
LICENSE = "MIT"

require recipes-core/images/core-image-base.bb

IMAGE_INSTALL:append = " \
    fbset \
    photo-viewer \
    iw \
    wpa-supplicant \
    avahi-daemon \
"

# Network builds need the firmware/kernel deploy output even though they do
# not create a WIC image. The root filesystem is served separately by NFS.
do_image_tar[depends] += "${@'virtual/kernel:do_deploy rpi-bootfiles:do_deploy' if d.getVar('PHOTO_VIEWER_BOOT_MEDIA') == 'net' else ''}"

photo_viewer_net_fstab() {
    if [ "${PHOTO_VIEWER_BOOT_MEDIA}" = "net" ]; then
        sed -i '\|^/dev/root[[:space:]]|d' ${IMAGE_ROOTFS}/etc/fstab
        printf '10.42.0.1:/srv/photo-viewer-netboot/rootfs / nfs defaults,vers=3 0 0\n' >> ${IMAGE_ROOTFS}/etc/fstab
    fi
}
ROOTFS_POSTPROCESS_COMMAND:append = " photo_viewer_net_fstab;"

