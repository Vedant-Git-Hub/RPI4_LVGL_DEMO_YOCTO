#!/usr/bin/env bash
set -Eeuo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ $# -gt 1 || ( $# -eq 1 && $1 != --remove ) ]]; then
    printf 'Usage: sudo ./netboot/diagnose.sh [--remove]\n' >&2
    exit 2
fi
if [[ ${EUID} -ne 0 ]]; then
    exec sudo -- "$0" "$@"
fi
root=/srv/photo-viewer-netboot/rootfs
if [[ ! -f $root/usr/lib/systemd/system/photo-viewer.service ]]; then
    printf 'Network boot root filesystem is missing. Deploy an image first.\n' >&2
    exit 1
fi
service="$root/etc/systemd/system/photo-viewer-boot-diagnostics.service"
script="$root/usr/local/sbin/photo-viewer-boot-diagnostics"
link="$root/etc/systemd/system/basic.target.wants/photo-viewer-boot-diagnostics.service"
if [[ ${1:-} == --remove ]]; then
    rm -f -- "$link" "$service" "$script"
    printf 'Diagnostic service removed from the network boot image.\n'
    exit
fi
install -d -m 0755 "$root/usr/local/sbin" "$root/etc/systemd/system/basic.target.wants"
install -m 0755 "$project_dir/netboot/photo-viewer-boot-diagnostics" "$script"
install -m 0644 "$project_dir/netboot/photo-viewer-boot-diagnostics.service" "$service"
ln -sfn ../photo-viewer-boot-diagnostics.service "$link"
printf 'Diagnostics installed. Reboot the Pi; after about 90 seconds read %s\n' \
    "$root/opt/photo-viewer/boot-diagnostics.log"
