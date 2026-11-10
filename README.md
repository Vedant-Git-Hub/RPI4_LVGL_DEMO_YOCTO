# Raspberry Pi 4 photo viewer

This Yocto project ports the ESP32 Raven gallery workflow to an 800 × 480
Waveshare 4.3-inch DSI LCD on a Raspberry Pi 4. It uses LVGL on `/dev/fb0`
and Linux services for storage, Wi-Fi, DHCP, mDNS, and image decoding.

## Gallery

The touchscreen library scans up to 64 images in
`/opt/photo-viewer/photos`. Select an image to preview it, open it full
screen, or start a slideshow. Each slide stays on screen for three seconds;
playback loops until the screen is touched. **Rescan** refreshes files copied
directly into the photo directory. Browser changes refresh the list
automatically.

After selecting a photo, tap **Edit Image** beside the preview to rotate it
left or right by 90 degrees and choose **Fit Whole Image** or **Fill Display**.
Fit shows the entire image with black margins where needed. Fill covers the
screen and crops the edges. The editor previews the result before you save.
The Edit Image button is disabled until a photo is selected.

Saved choices apply to the preview, full-screen view, and slideshow after
reboot. They live in a hidden `.filename.pv` file alongside each original
photo, including photos on an SD card. The original image is not modified.
The browser manager keeps these settings when copying or renaming a photo and
removes them when deleting one.

With USB boot, an optional FAT32, exFAT, or ext4 microSD card in the Pi's
card slot is mounted over the photo directory automatically. Its first
partition must appear as `/dev/mmcblk0p1`. The badge shows **SD** while the
card is mounted and **PI** while the internal photo directory is in use.
Insertion and removal are checked every five seconds; **Rescan** retries a
mount immediately. Browser file operations use whichever storage is active.

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

Tap **Wi-Fi** on the gallery screen to scan nearby networks, select an SSID,
enter its password with the on-screen keyboard, and connect. The connection
profile is saved in `/etc/photo-viewer-wpa.conf` and reconnects after reboot.
The keyboard opens when a text field is tapped and closes when you tap
outside it.
The status line shows the Pi's address after DHCP completes. Ethernet can
also reach the web manager.

Open `http://rpi-photo-viewer.local:8080/` from a computer on the same LAN,
or use the IP address shown on the Pi. The page can upload original images;
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

Build:

```bash
./kas-container build kas/rpi4-photo-viewer.yml
```

The compressed USB-bootable image is
`build/tmp/deploy/images/raspberrypi4-64/photo-viewer-image-raspberrypi4-64.rootfs.wic.bz2`.
Flash it to USB mass storage and boot the Pi 4 from USB. The profile sets
the root partition to `/dev/sda2`.
