#!/usr/bin/env bash
set -Eeuo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${EUID} -ne 0 ]]; then
    exec sudo -- "$0" "$@"
fi
if [[ $# -ne 0 ]]; then
    printf 'Usage: sudo ./netboot/setup-host.sh\n' >&2
    exit 2
fi
for command in nmcli apt-get install systemctl; do
    command -v "$command" >/dev/null || { printf 'Missing command: %s\n' "$command" >&2; exit 1; }
done
if [[ ! -d /sys/class/net/eno1 ]]; then
    printf 'The expected direct Ethernet interface eno1 is missing.\n' >&2
    exit 1
fi

packages=()
dpkg-query -W -f='${Status}' dnsmasq-base 2>/dev/null | grep -q 'install ok installed' || packages+=(dnsmasq-base)
dpkg-query -W -f='${Status}' nfs-kernel-server 2>/dev/null | grep -q 'install ok installed' || packages+=(nfs-kernel-server)
dpkg-query -W -f='${Status}' rpcbind 2>/dev/null | grep -q 'install ok installed' || packages+=(rpcbind)
if ((${#packages[@]})); then
    apt-get update
    apt-get install -y "${packages[@]}"
fi

if nmcli -t -f NAME connection show | grep -Fxq photo-viewer-netboot; then
    nmcli connection modify photo-viewer-netboot connection.interface-name eno1 \
        connection.autoconnect yes ipv4.method manual ipv4.addresses 10.42.0.1/24 \
        ipv4.never-default yes ipv6.method disabled
else
    nmcli connection add type ethernet ifname eno1 con-name photo-viewer-netboot \
        connection.autoconnect yes ipv4.method manual ipv4.addresses 10.42.0.1/24 \
        ipv4.never-default yes ipv6.method disabled
fi
# NetworkManager activates the profile automatically when the Pi is connected.
# Activate it now only when a physical link is already present.
if [[ -r /sys/class/net/eno1/carrier && $(cat /sys/class/net/eno1/carrier) == 1 ]]; then
    nmcli connection up photo-viewer-netboot ifname eno1
fi

install -d -m 0755 /etc/photo-viewer-netboot /etc/exports.d \
    /srv/photo-viewer-netboot/tftp /srv/photo-viewer-netboot/rootfs
install -m 0644 "$project_dir/netboot/dnsmasq.conf" /etc/photo-viewer-netboot/dnsmasq.conf
install -m 0644 "$project_dir/netboot/photo-viewer-netboot-dnsmasq.service" \
    /etc/systemd/system/photo-viewer-netboot-dnsmasq.service
printf '%s\n' '/srv/photo-viewer-netboot/rootfs 10.42.0.100(rw,sync,no_subtree_check,no_root_squash)' \
    > /etc/exports.d/photo-viewer-netboot.exports
dnsmasq --test --conf-file=/etc/photo-viewer-netboot/dnsmasq.conf
exportfs -ra
systemctl daemon-reload
systemctl enable --now rpcbind.service nfs-server.service photo-viewer-netboot-dnsmasq.service
if command -v ufw >/dev/null && ufw status | grep -q '^Status: active'; then
    ufw allow in on eno1 from 10.42.0.100 to any comment 'Photo Viewer netboot'
fi
printf 'Host configured. Build an image, power off the Pi, then run ./flash.sh net.\n'
