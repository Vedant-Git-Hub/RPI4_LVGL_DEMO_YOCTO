# Eclipse workspace

The `RPI4_LVGL_DEMO_YOCTO` Eclipse project links to the source tree one level
above this directory. Editing a linked file changes the real project file.
Generated `.kas-work` files stay out of Eclipse's Project Explorer.

On this host, **Eclipse IDE for C/C++ Developers 2026-09 R** is installed at
`~/.local/opt/eclipse`. Start **Eclipse C/C++ (Photo Viewer)** from the desktop
application menu or run:

```bash
~/.local/bin/eclipse -data ~/eclipse-workspace-photo-viewer
```

The project has already been imported into that workspace. If you choose a
different workspace, use **File → Import → General → Existing Projects into
Workspace**, select this `eclipse/` directory, and leave **Copy projects into
workspace** unchecked.

## Build

In Eclipse, open **Run → External Tools → External Tools Configurations…**.
The eleven project launchers appear under **Program**. Choose a **Build**
launcher for the display and boot medium, then click **Run**. The nine profiles
cover 4.3-inch DSI, 7-inch DSI, and HDMI with USB, SD card, and network boot.
The build output appears in Eclipse's Console. Build results are written under
the repository's `.kas-work/`; `./build` records the latest successful image.

Eclipse invokes the existing `./build` script, which runs Yocto in the kas
container. Eclipse's local C compiler is not used to build the Pi image.
Building does not flash or deploy anything.
CDT may mark target-only headers as unresolved because Yocto keeps its
compiler sysroot inside the kas work directory. The Eclipse Console build
result is the authoritative cross-build result.

## Flash or network deploy

Run **Flash - latest removable image** or **Deploy - latest network image**
under **External Tools → Program**. These launchers open a terminal for sudo
and any drive confirmation. The flash script verifies the latest image's
checksum and does not rebuild. For a removable drive, enter its whole-device
name (for example `sdc` or `mmcblk0`); the existing flash script displays the
target and requires you to type its name again before erasing it.

Power off and disconnect the Pi before **Deploy - latest network image**;
the deployment script refuses to replace a live NFS root. The launcher uses
the latest completed network image, so build the desired display profile first.

If a launcher is not listed yet, refresh the Eclipse project (`F5`) or use
**Run → External Tools → External Tools Configurations… → Program** and select
the `.launch` file from this project's `launches/` directory.
