#!/usr/bin/env bash
set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

usage() {
    printf 'Usage: %s DRIVE [usb|sd] [7inch]\n' "${0##*/}" >&2
    printf 'Examples: %s sdc usb, %s sdc sd 7inch, %s mmcblk0 sd 7inch\n' "${0##*/}" "${0##*/}" "${0##*/}" >&2
    printf 'Use the whole drive name shown by lsblk, without /dev/ or a partition number.\n' >&2
}

if [[ $# -lt 1 || $# -gt 3 || ! $1 =~ ^(sd[a-z]+|mmcblk[0-9]+)$ ]]; then
    usage
    exit 2
fi

drive=$1
device="/dev/$drive"
media=${2:-}
display=${3:-}
if [[ -n $media && $media != usb && $media != sd ]]; then
    usage
    exit 2
fi
if [[ -n $display && $display != 7inch ]]; then
    usage
    exit 2
fi
if [[ $drive == mmcblk* && $media == usb ]]; then
    printf '%s is an SD/MMC device; choose the SD image.\n' "$device" >&2
    exit 2
fi

for command in lsblk findmnt bzcat sudo dd sync tail; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required command is missing: %s\n' "$command" >&2
        exit 1
    fi
done

if [[ ! -b $device || $(lsblk -dn -o TYPE -- "$device") != disk ]]; then
    printf '%s is not a whole block device. Check the name with lsblk.\n' "$device" >&2
    exit 1
fi
target_identity=$(lsblk -dn -o SIZE,MODEL,SERIAL -- "$device")

if [[ -z $media && $drive == mmcblk* ]]; then
    media=sd
fi
if [[ -z $media && $drive == sd* ]] && command -v udevadm >/dev/null 2>&1; then
    properties=$(udevadm info --query=property --name="$device" 2>/dev/null || true)
    if [[ $properties == *ID_DRIVE_MEDIA_FLASH_SD=1* ||
          $properties == *ID_DRIVE_FLASH_SD=1* ]]; then
        media=sd
    fi
fi
if [[ -z $media ]]; then
    printf 'Is %s a USB drive or an SD card in a USB reader? Type usb or sd: ' "$device"
    read -r media
    if [[ $media != usb && $media != sd ]]; then
        printf 'Choose usb or sd. Nothing was written.\n' >&2
        exit 2
    fi
fi

if [[ $media == sd ]]; then
    if [[ $display == 7inch ]]; then
        profile="$script_dir/kas/rpi4-photo-viewer-7inch-sd.yml"
        image="$script_dir/build/tmp/deploy/images/raspberrypi4-64/photo-viewer-image-7inch-sd-raspberrypi4-64.rootfs.wic.bz2"
    else
        profile="$script_dir/kas/rpi4-photo-viewer-sd.yml"
        image="$script_dir/build/tmp/deploy/images/raspberrypi4-64/photo-viewer-image-sd-raspberrypi4-64.rootfs.wic.bz2"
    fi
    root_partition=/dev/mmcblk0p2
else
    if [[ $display == 7inch ]]; then
        profile="$script_dir/kas/rpi4-photo-viewer-7inch.yml"
        image="$script_dir/build/tmp/deploy/images/raspberrypi4-64/photo-viewer-image-7inch-raspberrypi4-64.rootfs.wic.bz2"
    else
        profile="$script_dir/kas/rpi4-photo-viewer.yml"
        image="$script_dir/build/tmp/deploy/images/raspberrypi4-64/photo-viewer-image-raspberrypi4-64.rootfs.wic.bz2"
    fi
    root_partition=/dev/sda2
fi

printf 'Target drive:\n'
lsblk -dn -o NAME,SIZE,MODEL,TRAN -- "$device"
printf 'Building the %s boot image. Existing drive contents are untouched during the build.\n' "$media"
cd -- "$script_dir"
./kas-container build "$profile"

if [[ ! -r $image ]]; then
    printf 'Build completed without the expected %s image: %s\n' "$media" "$image" >&2
    exit 1
fi
cmdline="$script_dir/build/tmp/deploy/images/raspberrypi4-64/bootfiles/cmdline.txt"
if [[ ! -r $cmdline ]]; then
    printf 'Boot command line is missing. Nothing was written.\n' >&2
    exit 1
fi
cmdline_text=$(<"$cmdline")
if [[ " $cmdline_text " != *" root=$root_partition "* ]]; then
    printf 'Boot command line does not match the selected %s image. Nothing was written.\n' "$media" >&2
    exit 1
fi
if [[ ! -b $device || $(lsblk -dn -o TYPE -- "$device") != disk ]]; then
    printf '%s is no longer the expected whole block device. Nothing was written.\n' "$device" >&2
    exit 1
fi
if [[ $(lsblk -dn -o SIZE,MODEL,SERIAL -- "$device") != "$target_identity" ]]; then
    printf '%s changed during the build. Nothing was written.\n' "$device" >&2
    exit 1
fi

printf 'Image: %s\n' "$image"
printf 'Target (all data will be erased):\n'
lsblk -dn -o NAME,SIZE,MODEL,TRAN -- "$device"
lsblk -nrpo NAME,SIZE,MOUNTPOINTS -- "$device"
printf 'Type %s to erase this drive: ' "$drive"
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
