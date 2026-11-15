# Release history

## Experimental video apps — 2026-11-15

### Modular process architecture

- Package the drawer, four LVGL apps, web server, and camera owner separately.
  The drawer launches one child app at a time and survives an app crash.
- Move the web server out of the UI process. Give the camera app and web server
  a Unix socket client for a single service that owns the USB camera.
- Share the selected image or video path through `/run/photo-viewer` so the
  settings app can offer the corresponding saved edit controls.
- Show the welcome splash only at boot, before the launcher appears.

- Add a four-app home screen for the image viewer, video player, settings, and
  camera. Move camera and settings entry points out of the image gallery.
- Add a video library with FFmpeg playback, optional ALSA audio, and persistent
  per-video rotation and Fit/Fill settings.
- Add browser video upload, playback with byte-range requests, management, and
  saved display settings. Preserve videos across network boot deployments.
- Include FFmpeg and ALSA playback utilities in the Yocto image.

This work is isolated on `experimental/video-apps` and is not part of a
stable release. The project owner confirmed video playback on the Pi. The
new full-screen controls and headphone audio still need hardware validation.

### Follow-up player and audio changes

- Open selected videos full screen with tap-to-show Back, Play/Pause, and seek
  controls.
- Route video audio to the Pi 4's 3.5 mm headphone jack and save a hardware
  mixer level from Settings → Volume.
- Replace the large home tiles with compact icons ordered by app name.
- Give image and video uploads separate progress and completion indicators.
- Override the Pi 4 VC4 overlay's disabled-headphone boot parameter, load the
  analogue sound driver, and keep ALSA errors in the service journal. Return
  to Home when camera capture completes or the camera app exits.
- Prefer connected USB audio devices, apply volume to PCM samples while playing,
  and respond to standard USB headset volume and mute keys. Keep the 3.5 mm
  jack as the fallback output.

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
