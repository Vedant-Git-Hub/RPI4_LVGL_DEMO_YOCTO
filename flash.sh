#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
image="$script_dir/build/tmp/deploy/images/raspberrypi4-64/photo-viewer-image-raspberrypi4-64.rootfs.wic.bz2"

usage() {
    printf 'Usage: %s sdc\n' "${0##*/}" >&2
    printf 'Pass the whole USB drive name shown by lsblk (for example sda, sdb, or sdc).\n' >&2
}

if [[ $# -ne 1 || ! $1 =~ ^sd[a-z]+$ ]]; then
    usage
    exit 2
fi

drive=$1
device="/dev/$drive"

for command in lsblk findmnt bzcat sudo dd sync; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required command is missing: %s\n' "$command" >&2
        exit 1
    fi
done

if [[ ! -r $image ]]; then
    printf 'Image not found. Build it first with: ./kas-container build kas/rpi4-photo-viewer.yml\n' >&2
    exit 1
fi

if [[ ! -b $device || $(lsblk -dn -o TYPE -- "$device") != disk ]]; then
    printf '%s is not a whole block device. Check the name with lsblk.\n' "$device" >&2
    exit 1
fi

printf 'Image: %s\n' "$image"
printf 'Target (all data will be erased):\n'
lsblk -dn -o NAME,SIZE,MODEL,TRAN -- "$device"
printf 'Partitions and mount points:\n'
lsblk -nrpo NAME,SIZE,MOUNTPOINTS -- "$device"
printf 'Type %s to confirm: ' "$drive"
read -r confirmation
if [[ $confirmation != "$drive" ]]; then
    printf 'Cancelled. Nothing was written.\n' >&2
    exit 1
fi

sudo -v
# Abort if a partition cannot be unmounted; writing a mounted filesystem
# would leave the running system and the flashed image inconsistent.
while IFS= read -r partition; do
    if findmnt -rn -S "$partition" >/dev/null; then
        printf 'Unmounting %s\n' "$partition"
        sudo umount -- "$partition"
    fi
done < <(lsblk -nrpo NAME -- "$device" | tail -n +2)

if [[ -n $(lsblk -nrpo MOUNTPOINTS -- "$device") ]]; then
    printf 'A partition on %s is still mounted. No image was written.\n' "$device" >&2
    exit 1
fi

printf 'Flashing %s...\n' "$device"
bzcat -- "$image" | sudo dd of="$device" bs=4M iflag=fullblock status=progress conv=fsync
sync
printf 'Flash complete. You can safely remove %s.\n' "$device"
