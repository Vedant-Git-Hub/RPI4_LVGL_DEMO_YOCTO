# Raspberry Pi 4 photo viewer

This Yocto project ports the ESP32 Raven gallery workflow to an 800 × 480
Waveshare 4.3-inch DSI LCD on a Raspberry Pi 4. It uses LVGL on `/dev/fb0`
and Linux services for storage, Wi-Fi, DHCP, mDNS, and image decoding.

The stable 1.0 source release is tagged `stable-v1.0`. See
[CHANGELOG.md](CHANGELOG.md) for its scope and validation. Build images are
generated locally and are not stored in Git.

### Experimental 7-inch DSI LCD (C) profile

The `experimental/waveshare-7inch-1024x600` branch adds a separate profile for
the Waveshare **7inch DSI LCD (C)**, 1024 × 600 capacitive touch panel. It uses
the pinned Raspberry Pi kernel's `vc4-kms-dsi-waveshare-panel,7_0_inchC`
overlay, its Waveshare panel driver, and the Goodix touch driver. A kernel
overlay patch fixes this panel's touch range to 1024 × 600. The gallery,
full-screen image rendering, editor preview, and camera preview use the new
resolution. The default profiles below still target the 4.3-inch panel.
Waveshare specifies the `i2c1` overlay option for this model. On a Pi 4,
connect both the 15-pin DSI ribbon and the four-wire cable carrying 5 V,
ground, SDA1, and SCL1. Without the four-wire connection, the display's
controller may not probe and `/dev/fb0` may never appear.

Build the experimental USB or SD image:

```bash
./kas-container build kas/rpi4-photo-viewer-7inch.yml
./kas-container build kas/rpi4-photo-viewer-7inch-sd.yml
```

To build and flash the matching image in one step, use `./flash.sh sdc usb
7inch` for a USB drive, `./flash.sh sdc sd 7inch` for an SD card in a USB
reader, or `./flash.sh mmcblk0 sd 7inch` for a built-in SD reader. The script
asks for confirmation before erasing the selected drive. The hardware port
has not been tested on the panel yet; verify display output, touch edges,
camera preview, and gallery image fit after flashing.

## Gallery

The touchscreen library scans up to 64 images in
`/opt/photo-viewer/photos`. Select an image to preview it, open it full
screen, or start a slideshow. Each slide stays on screen for three seconds;
playback loops until the screen is touched. **Rescan** refreshes files copied
directly into the photo directory. Browser changes refresh the list
automatically.

After selecting a photo, tap **Settings → Image Settings** to rotate it
left or right by 90 degrees and choose **Fit Whole Image** or **Fill Display**.
Fit shows the entire image with black margins where needed. Fill covers the
screen and crops the edges. The editor previews the result before you save.
Image Settings is disabled until a photo is selected.

Saved choices apply to the preview, full-screen view, and slideshow after
reboot. They live in a hidden `.filename.pv` file alongside each original
photo, including photos on an SD card. The original image is not modified.
The browser manager keeps these settings when copying or renaming a photo and
removes them when deleting one.

## USB camera

Connect a USB UVC camera and tap **Camera** on the gallery screen. The camera
screen shows a live preview. Tap **Capture** to freeze a photo, then enter a
name and choose **Save** or **Cancel**. Saved photos are JPEGs in the active
gallery storage, including the SD card when mounted, and appear in the image
library. Back closes the camera.

**Settings → Camera Settings** lists resolutions reported by the camera.
Choose one and tap **Apply**; the choice is saved across boots in
`/opt/photo-viewer/camera.conf`.
The viewer supports USB cameras that expose MJPEG or YUYV through Video4Linux2.
Where a camera offers both formats at one resolution, Settings lists both so
you can choose the one that works best with your camera. Modes up to
4096 × 2160 are listed.
The image includes the Linux `uvcvideo` driver and libjpeg-turbo. If the
camera is disconnected, the screen reports it and retries automatically.

With USB boot, an optional FAT32, exFAT, or ext4 microSD card in the Pi's
card slot is mounted over the photo directory automatically. Its first
partition must appear as `/dev/mmcblk0p1`. The badge shows **SD** while the
card is mounted and **PI** while the internal photo directory is in use.
Insertion and removal are checked every five seconds; **Rescan** retries a
mount immediately. Browser file operations use whichever storage is active.
With SD boot, the SD card holds the boot and root partitions. Photos live in
`/opt/photo-viewer/photos` on its root partition; the app never mounts the
card's boot partition as photo storage.

The Pi keeps original files. ImageMagick decodes a selected image, honors its
orientation, and fits it into an 800 × 480 black frame for LVGL. The
application tries every regular file with an extension, so its file list is
not limited to BMP or a fixed set of image suffixes. ImageMagick determines
whether it can decode the file. Typical formats include BMP, JPEG, PNG, GIF,
WebP, TIFF, JPEG 2000, and camera RAW. GIF87a and GIF89a animations play in the
gallery preview and full-screen view, with their saved rotation and Fit/Fill
settings. The Edit Image screen shows the first frame while choosing settings.
If the animation decoder cannot open a GIF, the status line reports this and
the first frame remains visible. Available codecs
depend on the built image: this profile adds WebP, JPEG 2000, and LibRaw
support to ImageMagick's JPEG, PNG, and TIFF dependencies. AVIF, HEIC/HEIF,
JPEG XL, and other formats require a matching decoder on the Pi.

Files uploaded through the browser can be up to 64 MiB. Filenames may contain
ASCII letters, numbers, spaces, and `._-()[]'!~`, with an extension. The
server checks that ImageMagick can decode each upload. Directly copied files
can be larger, subject to available storage
and ImageMagick's resource limits.

## Wi-Fi and browser manager

Tap **Settings → Network Settings** on the gallery screen to scan nearby
networks, select an SSID, enter its password with the on-screen keyboard,
and connect. The connection
profile is saved in `/etc/photo-viewer-wpa.conf` and reconnects after reboot.
The keyboard opens when a text field is tapped and closes when you tap
outside it.
The status line shows the Pi's address after DHCP completes. Ethernet can
also reach the web manager.

Open `http://vedantsphotogallary.local/` from a computer on the same LAN.
The local name can be changed in Network Settings and is retained after reboot.
Avahi advertises it through mDNS; the computer must support `.local` names.
Port 8080 remains available for older bookmarks.
The page can upload original images;
download, copy, rename, or delete stored files. Upload progress is shown.
The manager has no login or TLS, so keep it on a trusted LAN.

## Build and boot

Connect the panel ribbon to the Pi 4's 15-pin DSI connector. The profile
enables the Waveshare `vc4-kms-dsi-waveshare-800x480` overlay; the GPIO
header is not used for video.

The first build fetches Poky, meta-openembedded, and meta-raspberrypi.
Docker and substantial free disk space are required. If needed, download
the KAS wrapper:

```bash
curl -fsSL https://raw.githubusercontent.com/siemens/kas/master/kas-container -o kas-container
chmod +x kas-container
```

Build a USB or SD image directly:

```bash
./kas-container build kas/rpi4-photo-viewer.yml
./kas-container build kas/rpi4-photo-viewer-sd.yml
```

The USB image boots from `/dev/sda2`; the SD image boots from
`/dev/mmcblk0p2`. Their output names differ in
`build/tmp/deploy/images/raspberrypi4-64/`; Yocto may remove the earlier
profile's image when switching profiles, so build the one you plan to flash.

For one-step build and flash, identify the target with `lsblk` and pass its
whole drive name to the script:

```bash
./flash.sh sdc usb      # USB stick
./flash.sh sdc sd       # SD card in a USB reader
./flash.sh mmcblk0      # SD card in a built-in reader
```

The second argument is optional. The script detects a direct SD reader and
recognized SD card readers; otherwise it asks whether the target is USB or SD.
It builds the matching image, checks its boot command line, shows the target,
asks you to type the drive name to confirm, unmounts its partitions, and
writes the image. All data on that drive is erased.
