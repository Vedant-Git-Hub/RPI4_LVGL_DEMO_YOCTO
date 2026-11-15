# Release history

## Unreleased — brightness settings

- Add a Brightness tab under Settings. It adjusts a detected Linux backlight
  immediately, saves the chosen level, and restores it when Home starts.
- Disable the slider when the display has no writable backlight. Network boot
  deployment preserves the saved level.

## Stable version 3.0 — 2026-11-15

- Add a four-app Home screen with separate Image Viewer, Video Player,
  Settings, and Camera executables. The launcher starts one LVGL child at a
  time and returns to Home when it exits or crashes.
- Add video browsing and full-screen playback with touch controls for Back,
  Play/Pause, and seeking. Save per-video rotation and Fit/Fill settings.
- Add browser video upload, playback, download, management, and saved display
  settings. Image and video uploads have separate progress indicators. Network
  boot deployment preserves uploaded videos and settings.
- Send audio to a connected USB headset when available, with the Pi 4's
  3.5 mm jack as fallback. Save volume, adjust it during playback, and accept
  standard USB headset volume and mute key events.
- Run the web server and USB camera owner as separate systemd services. The
  camera app and browser use a local Unix socket to access the camera service;
  Wi-Fi authentication continues in its own `wpa_supplicant` service.
- Give every app and service its own recipe and `files/` directory. Shared
  LVGL sources and build inputs live in `photo-common/`.
- Show the welcome splash once at boot, before the launcher draws Home.
  Opening a child app does not replay it.

The project owner confirmed video playback, USB headphone audio, its volume
slider and headset buttons, and opening the separate apps on the Pi. The
7-inch network image passed the local Yocto build. The final boot-only splash
change and separate camera service have not yet been confirmed on the Pi;
the 4.3-inch and HDMI profiles also remain untested with this release.

## Stable version 2.1 — 2026-11-15

- Merge network boot into the main branch. Add dedicated direct-Ethernet
  DHCP/TFTP and NFS services on the development host, with a stable host
  address and a single Pi lease.
- Add network build profiles for the 4.3-inch DSI, 7-inch DSI, and HDMI displays.
  `./build DISPLAY net` packages the root filesystem and matching boot files
  together; `./flash.sh net` deploys the verified bundle without rebuilding.
- Preserve gallery photos and saved camera, Wi-Fi, and hostname settings when
  replacing a network-boot image.
- Fix Pi 4 network boot discovery with the Raspberry Pi PXE service response;
  include `overlay_map.dtb` and `vc4-kms-v3d-pi4.dtbo` so the Pi creates the
  DSI framebuffer. Add a boot diagnostic collector for host-side debugging.
- Add a browser **Live camera** view using MJPEG streaming when the selected
  camera mode provides it. **Capture** saves the same JPEG to the Pi gallery
  and starts a download on the browser device.
- Add browser camera resolution and format controls. The web page and Pi
  touchscreen use the same saved setting across reboots.
- Add a bold yellow welcome splash from the first Linux framebuffer image
  until the LVGL gallery is ready. The Waveshare DSI panel remains blank
  during the Pi 4 EEPROM bootloader and firmware stages because its driver
  initializes under Linux. U-Boot is not used by this image.
- Add the illustrated [directory guide](DIRECTORY_STRUCTURE.md) and update
  the startup, camera, and network boot documentation.

The 7-inch profile was validated on a Pi 4: Ethernet DHCP, TFTP, NFS root,
gallery display and touch, and the browser manager all work. The 7-inch SD
image with the web camera controls and splash passed local Yocto builds; the
new browser camera workflow and splash appearance still need Pi hardware
confirmation. The 4.3-inch and HDMI network profiles have not yet been tested
on hardware.

## Stable version 2.0 — 2026-11-14

- Add separate `./build` and `flash.sh` commands. The build script records the
  latest successful image; flashing verifies its checksum and writes it
  without rebuilding.
- Add USB and SD build profiles for the 4.3-inch DSI panel and HDMI output.
  HDMI scales the LVGL interface to the detected mode and accepts a USB mouse.
- Use the SD root partition for photos when booting from SD, leaving the
  firmware boot partition untouched by the gallery.
- Add the Waveshare 7-inch DSI LCD (C) profile at 1024 × 600 with Goodix touch
  mapped to the framebuffer, including its required Pi 4 I2C1 wiring.
- Fix camera JPEG opening on the 7-inch profile and improve Wi-Fi connection
  feedback and saved-profile handling.

The project owner confirmed the 7-inch display, touch, camera, gallery image
opening, and Wi-Fi work on the Raspberry Pi 4. The 7-inch USB and SD images,
4.3-inch image, and HDMI USB and SD images passed local Yocto builds.

## Stable version 1.0 — 2026-11-13

- Raspberry Pi 4 LVGL gallery for the Waveshare 800 × 480 DSI display.
- Gallery preview, full-screen viewing, slideshow, and saved per-image rotation
  and Fit/Fill choices.
- ImageMagick decoding for the formats installed in the image, with GIF
  playback where the LVGL decoder can open the file.
- USB UVC camera preview, resolution settings, and JPEG capture into the
  active gallery storage.
- Wi-Fi setup, browser photo manager, and an editable `.local` hostname.
- A unified Settings screen and `flash.sh` for guided USB image flashing.
- LVGL heap increased to 512 KiB after reproducing and fixing a startup crash
  caused by the default 64 KiB heap.

The project owner confirmed the application displays and works on the Pi 4
after the heap fix. The Yocto image build and compressed-image integrity check
passed. Hardware and network combinations beyond that report remain subject
to testing on the connected devices.

Use `./kas-container build kas/rpi4-photo-viewer.yml` to build from this tag,
then `./flash.sh sdc` with your actual USB drive name to flash it. The script
shows the target and asks for confirmation before erasing the drive.
