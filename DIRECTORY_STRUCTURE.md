# Project directory guide

This guide maps the **source repository**, the **generated build files**, and
the **files used when the Raspberry Pi boots**. Paths are relative to the
repository root unless they begin with `/`.

## At a glance

```mermaid
flowchart LR
    A["./build<br/>display + boot medium"] --> B["kas/*.yml<br/>Yocto configuration"]
    B --> C["meta-photo-viewer/<br/>application + image recipes"]
    C --> D[".kas-work/build/tmp/deploy/<br/>generated images and boot files"]
    D --> E["./flash.sh DRIVE<br/>USB or SD card"]
    D --> F["./flash.sh net<br/>host TFTP + NFS root"]
    F --> G["Raspberry Pi 4<br/>Ethernet boot"]
    E --> G
```

## Tracked source tree

```text
photo_viewer/
├── README.md                     Project overview, usage, hardware notes
├── DIRECTORY_STRUCTURE.md        This map
├── CHANGELOG.md                  Release and validation history
├── LICENSE                       Project license
├── build                         Select display/medium and create an image
├── flash.sh                      Deploy the last successful build
├── kas/                          Build profiles (see matrix below)
│   ├── rpi4-photo-viewer.yml     Base 4.3-inch DSI, USB profile
│   ├── rpi4-photo-viewer-sd.yml  4.3-inch DSI, SD profile
│   ├── rpi4-photo-viewer-net.yml 4.3-inch DSI, network profile
│   ├── rpi4-photo-viewer-7inch.yml
│   ├── rpi4-photo-viewer-7inch-sd.yml
│   ├── rpi4-photo-viewer-7inch-net.yml
│   ├── rpi4-photo-viewer-hdmi.yml
│   ├── rpi4-photo-viewer-hdmi-sd.yml
│   └── rpi4-photo-viewer-hdmi-net.yml
├── meta-photo-viewer/            Custom Yocto layer
│   ├── conf/layer.conf           Register this layer with BitBake
│   ├── recipes-apps/
│   │   ├── photo-common/         Shared LVGL shell, layout, build inputs
│   │   ├── photo-launcher/       App drawer recipe and source
│   │   ├── photo-images/         Image recipe and image support files
│   │   ├── photo-videos/         Video recipe and player files
│   │   ├── photo-settings/       Settings recipe and network/audio/backlight files
│   │   ├── photo-camera/         Camera app recipe and UI files
│   │   ├── photo-camera-service/ Camera service recipe and IPC files
│   │   ├── photo-web/            Web service recipe, HTML, HTTP server
│   │   ├── photo-splash/         One-time boot splash recipe and artwork
│   │   ├── photo-wifi/           Wi-Fi service recipe and configuration
│   │   └── photo-viewer/         Launcher integration and panel setup
│   ├── recipes-core/images/
│   │   └── photo-viewer-image.bb Define packages in the Pi root filesystem
│   ├── recipes-graphics/lvgl/    LVGL recipe additions and patches
│   ├── recipes-support/imagemagick/
│   │                              ImageMagick format support additions
│   └── wic/usb-raspberrypi.wks   USB image partition layout
└── netboot/                      Host-side network boot setup and deployment
    ├── setup-host.sh             Configure host Ethernet, DHCP/TFTP, NFS
    ├── dnsmasq.conf              Private DHCP/TFTP configuration for eno1
    ├── photo-viewer-netboot-dnsmasq.service
    ├── deploy.sh                 Install a built network boot bundle
    ├── diagnose.sh               Enable/remove Pi boot log collection
    ├── photo-viewer-boot-diagnostics
    └── photo-viewer-boot-diagnostics.service
```

The 4.3-inch profile is the base KAS file. The other KAS profiles include it
or another display profile, then change the display, boot medium, image name,
and kernel command line as needed. `./build` chooses the matching profile:

| Command | KAS profile suffix | Output |
| --- | --- | --- |
| `./build 4_3_display [usb\|sd\|net]` | base, `-sd`, `-net` | 800 × 480 DSI |
| `./build 7_display [usb\|sd\|net]` | `-7inch`, `-7inch-sd`, `-7inch-net` | 1024 × 600 DSI |
| `./build hdmi_display [usb\|sd\|net]` | `-hdmi`, `-hdmi-sd`, `-hdmi-net` | HDMI monitor |

`usb` is the default medium when the second argument is omitted. USB and SD
builds create compressed WIC disk images. Network builds create a bundle with
the root filesystem archive and matching TFTP boot files.

## Application files inside the Yocto layer

```mermaid
flowchart LR
    L["photo-launcher<br/>systemd starts at boot"] --> I["photo-images"]
    L --> V["photo-videos"]
    L --> S["photo-settings"]
    L --> C["photo-camera"]
    C -->|Unix socket| D["photo-camera-service"]
    W["photo-web<br/>systemd service"] -->|Unix socket| D
    S -->|wpa_cli| N["wpa_supplicant<br/>systemd service"]
    I -.->|selected path in /run| S
    V -.->|selected path in /run| S
```

Only one LVGL child owns the touchscreen and framebuffer at a time. The
launcher waits for that child to exit, then redraws Home. The camera service
opens the USB device on demand and serializes clients; it does not keep the
camera streaming while no client needs it.

Each app or service directory contains its own `.bb` recipe and `files/`
directory. `photo-common/` holds the sources currently compiled into multiple
LVGL app binaries. The image and settings screens still share `main.c`; their
image, network, and audio helpers live in their named directories.

| Directory | Main files |
| --- | --- |
| `photo-common/` | `main.c`, `display_layout.h`, `selection_state.c`, shared `Makefile`, `photo-app-common.inc` |
| `photo-launcher/` | `launcher.c` |
| `photo-images/` | `image_convert.c`, `image_settings.c` |
| `photo-videos/` | `video_ui.c`, `video_backend.c` |
| `photo-settings/` | `wifi_control.c`, `hostname_settings.c`, `audio_control.c`, `brightness_control.c` |
| `photo-camera/` | `camera_ui.c` |
| `photo-camera-service/` | `camera_service.c`, V4L2 backend, camera IPC client and protocol, service unit |
| `photo-web/` | `media_server.c`, `web_server_main.c`, `index.html`, service unit |
| `photo-splash/` | `boot_splash.c`, welcome artwork, boot service unit |
| `photo-wifi/` | `wpa_supplicant` service unit, initial configuration, DHCP network file |
| `photo-viewer/` | Launcher service unit and panel configuration |

The `.bb` recipes install these files into the Pi image; editing `files/`
alone does not update an already deployed Pi. Rebuild and redeploy the image
to apply application changes.

## Generated files and Pi storage

These paths are produced by tools or by the running Pi. They are outside Git
and should not be confused with the source files above.

| Path | Purpose |
| --- | --- |
| `.kas-work/` | Downloaded Yocto layers, build state, caches, and artifacts. |
| `.kas-work/build/latest-image.tsv` | Display, medium, filename, and checksum of the latest successful `./build`; `flash.sh` reads it. |
| `.kas-work/build/tmp/deploy/images/raspberrypi4-64/` | Compressed USB/SD images, network boot bundles, kernel, firmware, and device-tree files. |
| `/srv/photo-viewer-netboot/tftp/` on the host | Pi firmware, kernel, command line, and display overlays served by TFTP. |
| `/srv/photo-viewer-netboot/rootfs/` on the host | Pi root filesystem exported over NFS. |
| `/opt/photo-viewer/photos/` on the Pi | Gallery photos; network boot stores them in the host NFS root. |
| `/opt/photo-viewer/videos/` on the Pi | Uploaded videos and saved per-video display settings. |
| `/opt/photo-viewer/volume.conf` on the Pi | Saved headphone volume, preserved by network deployment. |
| `/opt/photo-viewer/brightness.conf` on the Pi | Saved display backlight level, preserved by network deployment. |
| `/opt/photo-viewer/camera.conf` on the Pi | Camera mode shared by the Pi screen and web controls. |
| `/etc/photo-viewer-wpa.conf` on the Pi | Saved Wi-Fi profiles. |

The `netboot/setup-host.sh` script installs the host service configuration
under `/etc/photo-viewer-netboot/`, `/etc/exports.d/`, and systemd. The
`netboot/deploy.sh` script replaces the host TFTP and NFS directories while
preserving gallery photos, videos, and saved device settings. Keep the host running
while the Pi uses network boot.

## Boot display handoff

```mermaid
flowchart LR
    A["Pi 4 EEPROM bootloader"] --> B["Raspberry Pi firmware"]
    B --> C["Linux DSI driver creates /dev/fb0"]
    C --> D["photo-viewer-splash.service draws boot-splash.bmp"]
    D --> E["LVGL launcher draws Home"]
```

The 7-inch DSI screen is blank before Linux initializes its panel driver.
This image boots the kernel directly through Raspberry Pi firmware; U-Boot is
not in the boot chain. The splash service runs when the framebuffer appears,
then the LVGL launcher draws its home screen.

The repository ignores `.kas-work/`, downloaded layers, build artifacts, and
the downloaded `kas-container` wrapper. A fresh checkout regenerates them on
the first build; Git contains the recipes and scripts needed to do so.
