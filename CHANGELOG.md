# Release history

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
