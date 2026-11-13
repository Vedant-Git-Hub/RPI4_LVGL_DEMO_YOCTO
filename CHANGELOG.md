# Release history

## Unreleased

- Add an SD boot profile and make `flash.sh` build the matching USB or SD
  image before flashing. It asks for the medium when a USB card reader cannot
  be distinguished from a USB stick.
- Use the SD root partition for photos when booting from SD, leaving the
  firmware boot partition untouched by the gallery.

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
