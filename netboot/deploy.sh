#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 1 || ! -f $1 ]]; then
    printf 'Usage: ./netboot/deploy.sh IMAGE.netboot.tar\n' >&2
    exit 2
fi
bundle=$(readlink -f -- "$1")
if [[ ! -f /etc/photo-viewer-netboot/dnsmasq.conf ||
      ! -f /etc/exports.d/photo-viewer-netboot.exports ]]; then
    printf 'Host is not configured. Run sudo ./netboot/setup-host.sh first.\n' >&2
    exit 1
fi
if [[ ${EUID} -ne 0 ]]; then
    exec sudo -- "$0" "$bundle"
fi
base=/srv/photo-viewer-netboot
if [[ -r /sys/class/net/eno1/carrier && $(cat /sys/class/net/eno1/carrier) == 1 ]]; then
    printf 'Power off and disconnect the Pi before replacing its live NFS root.\n' >&2
    exit 1
fi
stage=$(mktemp -d "$base/.staging.XXXXXX")
cleanup() { rm -rf -- "$stage"; }
trap cleanup EXIT
tar -C "$stage" -xf "$bundle"
if [[ ! -f $stage/rootfs.tar.bz2 || ! -f $stage/tftp/start4.elf ||
      ! -f $stage/tftp/config.txt || ! -f $stage/tftp/cmdline.txt ||
      ! -f $stage/tftp/kernel8.img || ! -f $stage/tftp/bcm2711-rpi-4-b.dtb ||
      ! -f $stage/tftp/overlays/overlay_map.dtb ||
      ! -f $stage/tftp/overlays/vc4-kms-v3d-pi4.dtbo ]]; then
    printf 'Network boot bundle is incomplete.\n' >&2
    exit 1
fi
cmdline=$(cat "$stage/tftp/cmdline.txt")
if [[ " $cmdline " != *' root=/dev/nfs '* ||
      " $cmdline " != *' nfsroot=10.42.0.1:/srv/photo-viewer-netboot/rootfs,vers=3,tcp '* ||
      " $cmdline " != *' ip=dhcp '* ]]; then
    printf 'Bundle has the wrong NFS kernel command line.\n' >&2
    exit 1
fi
mkdir "$stage/rootfs"
tar --numeric-owner -C "$stage/rootfs" -xjf "$stage/rootfs.tar.bz2"
# Gallery content and device settings survive an image redeployment.
for path in opt/photo-viewer/photos opt/photo-viewer/camera.conf \
    etc/photo-viewer-wpa.conf etc/hostname \
    etc/systemd/system/photo-viewer-boot-diagnostics.service \
    etc/systemd/system/basic.target.wants/photo-viewer-boot-diagnostics.service \
    usr/local/sbin/photo-viewer-boot-diagnostics; do
    if [[ -e $base/rootfs/$path || -L $base/rootfs/$path ]]; then
        mkdir -p -- "$stage/rootfs/$(dirname "$path")"
        rm -rf -- "$stage/rootfs/$path"
        cp -a -- "$base/rootfs/$path" "$stage/rootfs/$path"
    fi
done
rm -f -- "$stage/rootfs.tar.bz2"
if [[ -e $base/rootfs.previous || -e $base/tftp.previous ]]; then
    printf 'Previous deployment backup exists. Resolve it before deploying again.\n' >&2
    exit 1
fi
rollback() {
    if [[ -e $base/rootfs.previous ]]; then
        rm -rf -- "$base/rootfs"
        mv -- "$base/rootfs.previous" "$base/rootfs"
    fi
    if [[ -e $base/tftp.previous ]]; then
        rm -rf -- "$base/tftp"
        mv -- "$base/tftp.previous" "$base/tftp"
    fi
}
trap 'rollback; cleanup' ERR
mv -- "$base/rootfs" "$base/rootfs.previous"
mv -- "$base/tftp" "$base/tftp.previous"
mv -- "$stage/rootfs" "$base/rootfs"
mv -- "$stage/tftp" "$base/tftp"
exportfs -ra
rm -rf -- "$base/rootfs.previous" "$base/tftp.previous"
trap cleanup EXIT
printf 'Network boot image deployed. Connect and power on the Pi.\n'
