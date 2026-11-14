# Raspberry Pi 4 photo viewer

This Yocto project ports the ESP32 Raven gallery workflow to a Raspberry Pi 4.
The main branch supports the 800 × 480 Waveshare 4.3-inch DSI LCD, the
1024 × 600 Waveshare 7-inch DSI LCD (C), and HDMI monitors. It uses LVGL on `/dev/fb0`
and Linux services for storage, Wi-Fi, DHCP, mDNS, and image decoding.

See [DIRECTORY_STRUCTURE.md](DIRECTORY_STRUCTURE.md) for an illustrated map of
the source tree, build outputs, and files used by the running Pi.

The stable 2.0 source release is tagged `stable-v2.0`; 1.0 remains available as
`stable-v1.0`. See
[CHANGELOG.md](CHANGELOG.md) for its scope and validation. Build images are
generated locally and are not stored in Git.

### 7-inch DSI LCD (C) profile

The `7_display` profile supports the Waveshare **7inch DSI LCD (C)**,
1024 × 600 capacitive touch panel. It uses the Raspberry Pi kernel's
`vc4-kms-dsi-waveshare-panel,7_0_inchC`
overlay, its Waveshare panel driver, and the Goodix touch driver. The kernel
overlay exposes an 800 × 480 touch range; the application reads the Linux
input device's axis limits and maps them across the 1024 × 600 framebuffer.
The gallery, full-screen image rendering, editor preview, and camera preview
use the panel's native resolution.
Waveshare specifies the `i2c1` overlay option for this model. On a Pi 4,
connect both the 15-pin DSI ribbon and the four-wire cable carrying 5 V,
ground, SDA1, and SCL1. Without the four-wire connection, the display's
controller may not probe and `/dev/fb0` may never appear.

Build the 7-inch USB or SD image:

```bash
./build 7_display
./build 7_display sd
```

Then use `./flash.sh sdc` for the latest successful image (or replace `sdc`
with the actual drive). Flashing does not build. The script asks for
confirmation before erasing the selected drive. The project owner has
confirmed display, touch, camera capture, gallery opening, and Wi-Fi on this
panel.

## Experimental wired network boot

The `experimental/rpi4-netboot` branch adds a Pi 4 Ethernet boot profile for
all three displays. Connect the Pi's Ethernet port directly to this Ubuntu
host's `eno1` port. The host uses `10.42.0.1`; the Pi receives `10.42.0.100`.
The host's Wi-Fi connection and default route remain separate. The host must
stay powered on while the Pi uses its NFS root filesystem.

Configure the host once (this installs dnsmasq and the NFS server, and needs
your local sudo password):

```bash
sudo ./netboot/setup-host.sh
```

Set the Pi 4 EEPROM `BOOT_ORDER=0xf42` to try network boot, then USB storage,
then restart. Build for the attached display and deploy the latest successful
build after powering the Pi off:

```bash
./build 7_display net       # or 4_3_display / hdmi_display
./flash.sh net              # deploys the recorded build; does not rebuild
```

`./flash.sh net` checks the bundle checksum and replaces the host's TFTP
firmware and NFS root. It preserves photos, camera settings, Wi-Fi settings,
and the saved hostname from the previous NFS root. For USB/SD cards, continue
using `./flash.sh DRIVE`. A direct Ethernet connection is required for this
profile; Wi-Fi cannot provide the initial network boot. The 7-inch profile
has been booted on a Pi 4 with working gallery display, Goodix touch, NFS
root, and browser manager. The 4.3-inch and HDMI network profiles still need
hardware testing.

If a future network boot reaches Linux but the gallery stays black, run
`sudo ./netboot/diagnose.sh` on the host and reboot the Pi. The report appears
at `/srv/photo-viewer-netboot/rootfs/opt/photo-viewer/boot-diagnostics.log`
after about a minute. Run `sudo ./netboot/diagnose.sh --remove` when finished.

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
Captured JPEGs open in the gallery at the selected display resolution.
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
Use **Show** beside the password field to verify what was typed. The password
stays in the field until the Pi connects, so a failed attempt can be corrected.
The status line shows scanning, association, authentication, or a temporarily
rejected network when those states are reported by `wpa_supplicant`. When the
router rejects a connection, the viewer shows the reason from the Wi-Fi
service log if available. Retrying an SSID replaces older saved attempts for
that same SSID.
The status line shows the Pi's address after DHCP completes. Ethernet can
also reach the web manager.

Open `http://vedantsphotogallary.local/` from a computer on the same LAN.
The local name can be changed in Network Settings and is retained after reboot.
Avahi advertises it through mDNS; the computer must support `.local` names.
Port 8080 remains available for older bookmarks.
The page can upload original images;
download, copy, rename, or delete stored files. Upload progress is shown.
Tap **Live camera** to view the Pi's USB camera in the browser, then **Capture**
to save the current JPEG to the Pi gallery and download the same photo to the
browser device. The browser chooses its usual Downloads folder or asks where to
save, according to its settings. **Back** closes the stream. The web preview
uses the resolution saved in Camera Settings and forwards MJPEG frames directly
when the camera offers that format; YUYV cameras are encoded as JPEG. Only one
camera preview can own the USB camera at a time, either on the Pi or in a
browser. If the camera is busy, close the other preview and reopen Live camera.
The manager has no login or TLS, so keep it on a trusted LAN.

## Build and boot

For the 4.3-inch panel, connect its ribbon to the Pi 4's 15-pin DSI connector.
The `4_3_display` profile enables the Waveshare
`vc4-kms-dsi-waveshare-800x480` overlay. The `hdmi_display` profile leaves
the DSI overlay out and uses the HDMI monitor's detected mode. LVGL scales
the 800 × 480 interface to fit that mode without stretching it. Connect a
USB mouse to control the HDMI interface; the app displays a pointer when it
finds no touchscreen.

The first build fetches Poky, meta-openembedded, and meta-raspberrypi.
Docker and substantial free disk space are required. If needed, download
the KAS wrapper:

```bash
curl -fsSL https://raw.githubusercontent.com/siemens/kas/master/kas-container -o kas-container
chmod +x kas-container
```

Build for the selected display and boot medium. USB boot is the default:

```bash
./build 4_3_display
./build 4_3_display sd
./build 7_display
./build 7_display sd
./build hdmi_display
./build hdmi_display sd
```

The USB image boots from `/dev/sda2`; the SD image boots from
`/dev/mmcblk0p2`. Build artifacts are in
`.kas-work/build/tmp/deploy/images/raspberrypi4-64/`. The `./build` script records
the latest successful image and its checksum for `flash.sh`.

To flash that image later, identify the target with `lsblk` and pass its whole
drive name to the script:

```bash
./flash.sh sdc           # USB stick or SD card in a USB reader
./flash.sh sdc sd        # Also require that the latest image is for SD boot
./flash.sh mmcblk0       # SD card in a built-in reader
```

Flashing does not build. It checks the saved image checksum, rejects a USB
boot image for a built-in SD reader, shows the target, asks you to type its
drive name to confirm, unmounts its partitions, and writes the image. All data
on that drive is erased. The optional `usb` or `sd` argument checks the image
type. To run KAS directly, set `KAS_WORK_DIR="$PWD/.kas-work"` and
`KAS_BUILD_DIR="$PWD/.kas-work/build"`; the project's old `build/` output
directory was moved to `.kas-work/build/` so
`./build` can be a script.
If another checkout still has a `build/` directory, move it to
`.kas-work/build/` before pulling this change; Git cannot create the script
while a directory named `build` is in its place.
