#!/usr/bin/env bash
set -Eeuo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
deploy_dir="$project_dir/.kas-work/build/tmp/deploy/images/raspberrypi4-64"
record="$project_dir/.kas-work/build/latest-image.tsv"

usage() {
    printf 'Usage: %s DRIVE [usb|sd] | %s net\n' "${0##*/}" "${0##*/}" >&2
    printf 'Examples: %s sdc, %s sdc sd, %s mmcblk0, %s net\n' "${0##*/}" "${0##*/}" "${0##*/}" "${0##*/}" >&2
    printf 'The latest successful ./build image is used; no build runs during flashing.\n' >&2
}

if [[ $# -lt 1 || $# -gt 2 || ( $1 != net && ! $1 =~ ^(sd[a-z]+|mmcblk[0-9]+)$ ) ||
      ( $1 == net && $# != 1 ) ]]; then usage; exit 2; fi
drive=$1
expected_media=${2:-}
if [[ -n $expected_media && $expected_media != usb && $expected_media != sd ]]; then usage; exit 2; fi
device="/dev/$drive"

for command in lsblk findmnt bzcat sha256sum sudo dd sync tail; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required command is missing: %s\n' "$command" >&2
        exit 1
    fi
done
if [[ ! -r $record ]]; then
    printf 'No completed build is recorded. Run ./build DISPLAY [usb|sd] first.\n' >&2
    exit 1
fi
IFS=$'\t' read -r display media image_file checksum < "$record"
case "$display:$media" in
    4_3_display:usb) image_base=photo-viewer-image ;;
    4_3_display:sd) image_base=photo-viewer-image-sd ;;
    hdmi_display:usb) image_base=photo-viewer-image-hdmi ;;
    hdmi_display:sd) image_base=photo-viewer-image-hdmi-sd ;;
    7_display:usb) image_base=photo-viewer-image-7inch ;;
    7_display:sd) image_base=photo-viewer-image-7inch-sd ;;
    4_3_display:net) image_base=photo-viewer-image-net; overlay=vc4-kms-dsi-waveshare-800x480 ;;
    7_display:net) image_base=photo-viewer-image-7inch-net; overlay=vc4-kms-dsi-waveshare-panel ;;
    hdmi_display:net) image_base=photo-viewer-image-hdmi-net; overlay= ;;
    *) printf 'The latest build record has an unsupported display or boot medium.\n' >&2; exit 1 ;;
esac
extension=wic.bz2
if [[ $media == net ]]; then extension=netboot.tar; fi
if [[ ! $checksum =~ ^[0-9a-f]{64}$ || $image_file != "$image_base-raspberrypi4-64.rootfs-"*.$extension || $image_file == */* ]]; then
    printf 'The latest build record is invalid. Run ./build again.\n' >&2
    exit 1
fi
image="$deploy_dir/$image_file"
if [[ ! -r $image ]]; then
    printf 'The latest built image is missing: %s\n' "$image" >&2
    exit 1
fi
actual_checksum=$(sha256sum -- "$image")
if [[ ${actual_checksum%% *} != "$checksum" ]]; then
    printf 'The built image changed after ./build. Run ./build again.\n' >&2
    exit 1
fi
if [[ -n $expected_media && $expected_media != "$media" ]]; then
    printf 'Latest image is for %s boot, but %s was requested. Run ./build for the intended medium.\n' "$media" "$expected_media" >&2
    exit 2
fi
if [[ $drive == net ]]; then
    if [[ $media != net ]]; then
        printf 'Latest image is for %s boot. Run ./build DISPLAY net first.\n' "$media" >&2
        exit 2
    fi
    "$project_dir/netboot/deploy.sh" "$image"
    exit
fi
if [[ $media == net ]]; then
    printf 'Latest image is for network boot. Deploy it with ./flash.sh net.\n' >&2
    exit 2
fi
if [[ $drive == mmcblk* && $media != sd ]]; then
    printf '%s is an SD/MMC device, but the latest image boots from USB.\n' "$device" >&2
    exit 2
fi
if [[ ! -b $device || $(lsblk -dn -o TYPE -- "$device") != disk ]]; then
    printf '%s is not a whole block device. Check the name with lsblk.\n' "$device" >&2
    exit 1
fi
target_identity=$(lsblk -dn -o SIZE,MODEL,SERIAL -- "$device")

printf 'Latest built image: %s (%s boot)\n' "$display" "$media"
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
if [[ ! -b $device || $(lsblk -dn -o TYPE -- "$device") != disk ||
      $(lsblk -dn -o SIZE,MODEL,SERIAL -- "$device") != "$target_identity" ]]; then
    printf 'Target drive changed. Nothing was written.\n' >&2
    exit 1
fi

sudo -v
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
