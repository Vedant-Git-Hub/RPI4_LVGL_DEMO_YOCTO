#!/usr/bin/env bash
set -Eeuo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

case ${1:-} in
    build)
        if [[ $# -ne 3 ]]; then
            printf 'Usage: %s build DISPLAY {usb|sd|net}\n' "$0" >&2
            exit 2
        fi
        exec "$project_dir/build" "$2" "$3"
        ;;
    flash)
        if [[ $# -ne 2 || ( $2 != net && $2 != removable ) ]]; then
            printf 'Usage: %s flash {net|removable}\n' "$0" >&2
            exit 2
        fi
        # Eclipse's External Tools console cannot reliably accept sudo and
        # drive confirmation input. Use a real terminal for flashing.
        exec x-terminal-emulator -- "$0" flash-terminal "$2"
        ;;
    flash-terminal)
        if [[ $# -ne 2 ]]; then exit 2; fi
        cd -- "$project_dir"
        if [[ $2 == net ]]; then
            ./flash.sh net || result=$?
        else
            lsblk -d -o NAME,SIZE,MODEL,TRAN
            read -r -p 'Whole drive name (for example sdc or mmcblk0): ' drive
            ./flash.sh "$drive" || result=$?
        fi
        printf '\nFlash command exited with status %s. Press Enter to close.\n' "${result:-0}"
        read -r _
        exit "${result:-0}"
        ;;
    *)
        printf 'Usage: %s build DISPLAY MEDIA | flash {net|removable}\n' "$0" >&2
        exit 2
        ;;
esac
