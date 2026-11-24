# Aurora R36 — Native C / SDL2 (v0.4.1)

Aurora is an offline, fullscreen music player for the R36 Ultra's 720×720 display. The interface is C11 + SDL2; MPV handles audio playback, while FFmpeg/ffprobe provide metadata and album-art extraction.

- Repository: https://github.com/aps456/Aurora-R36
- Source version: 0.4.1
- License: GNU GPL v3 (retained from the existing repository)

## v0.4.1 interface

- Apple Music-inspired interface with selectable dark and light appearances, scaled for a square 720×720 screen.
- Vertical home/library list: Recently Added, Songs, Artists, Albums, Playlists, Folders and Settings. No bottom tab/navigation bar.
- A compact Now Playing strip appears at the bottom of the home list while a track is active; selecting it returns to Now Playing.
- A consistent top status strip displays local time, Wi-Fi connection state and battery percentage/charging state when the firmware exposes those values through standard Linux sysfs paths.
- Now Playing has centered artwork, centered song title and `Album Artist | Album | Year`, progress/time, a codec/bit-depth/sample-rate line separated with vertical bars, and large playback controls. It has no Now Playing header, down-arrow, cast/TV, like/favourite or three-dot controls.
- Dark and light appearance modes replace the previous violet-heavy palette.
- Home/library icons are loaded from `Aurora-R36/icons/` as external BMP assets.
- Album artwork thumbnails and full covers are cached under `Aurora-R36/artwork/` for repeat-launch speed.
- A temporary iPhone-style volume overlay appears when the D-pad changes the volume.

## Controller layout

Aurora normalizes the face-button labels reported by the firmware before dispatching actions. The raw SDL joystick fallback uses the common R36 Ultra Linux order and D-pad hat events.

| Hardware control | Aurora action |
|---|---|
| D-pad | Navigate menus and lists |
| Left and right analog sticks | Navigate menus and lists; ignored on Now Playing |
| D-pad left/right tap on Now Playing | Previous/next track |
| D-pad left/right hold on Now Playing | Rewind/fast-forward in 5-second steps |
| D-pad up/down on Now Playing | Decrease/increase volume; hold repeats |
| A | Select; play/pause on Now Playing |
| B | Back |
| X | Toggle shuffle |
| Y | Cycle repeat: off, all, one |
| Select + Start together | Quit Aurora |
| L1/L2/R1/R2 and stick clicks | No action |

The SDL GameController layer uses the firmware's mapping when one is available. The raw fallback uses the R36-family joystick event order: A=0, B=1, X=2, Y=3, Select=8 and Start=9; the D-pad arrives through SDL hat events and axes 0–3 are the left/right stick X/Y axes. Aurora explicitly swaps the firmware-reported face-button labels where needed so the physical A/B and X/Y actions match the requested labels. This raw layout is documented for R36-family devices, but firmware and board revisions can differ. It has not yet been physically verified on every R36 Ultra revision, so the first hardware test must confirm that the face buttons and D-pad match the labels above. No first-run remapping wizard is shown.

## Joystick LED settings

Settings includes **Off**, **Album Tone** and **LED Wave** modes. Aurora detects an existing writable LED interface before enabling these options. It does not change system permissions, install kernel drivers or write to I²C directly.

- On ArkOS4Clone builds exposing `/sys/class/leds/joyled/brightness`, Album Tone chooses the nearest supported static hardware colour preset; LED Wave selects the controller's built-in wave effect (the documented `200` mode).
- On Batocera builds exposing `batocera-led-handheld`, Album Tone uses the current accent RGB colour. The fallback pulse is an ambient colour pulse, not an audio-spectrum analyser.
- Firmware without a supported writable LED interface shows **Not Available**. In particular, not every R36 Ultra firmware build supports the joystick LEDs.

The LED controller interfaces documented by firmware projects are not uniform. This release does not claim a live, music-reactive audio visualizer: that would require a separate audio-analysis path and matching hardware support.

## Library and playback

- Songs, Artists, Albums, Playlists and Recently Added views.
- Folder selector, recursive library scan, metadata cache and sorting.
- Embedded/folder artwork thumbnails and dominant-colour extraction.
- M3U playlist creation and playback.
- Supported audio extensions are detected by the scanner; playback requires a working MPV runtime on the device. Aurora now waits for MPV IPC readiness and reports clearly when the audio engine cannot start.
- Crossfade presets remain gain-curve hand-offs, not true overlapping dual-track mixing.

## Runtime dependencies on the device

- SDL2 runtime compatible with the firmware's display backend
- MPV
- FFmpeg and ffprobe
- curl and unzip for in-app updates

Build against the SDL2 headers and libraries matching the exact firmware image. Do not replace the system SDL2 library: some firmware images carry KMSDRM and panel-orientation patches.

## Build locally

On Linux with SDL2 development headers, `pkg-config`, a C compiler and Make:

```sh
make clean all
./build/aurora --version
sh package-release.sh armv7   # or aarch64
```

The package script produces `Aurora_R36_<arch>.zip` with the Ports structure and executable permissions. A generic desktop binary is not guaranteed to run on the handheld; architecture, ABI, glibc and SDL2 must match the device firmware.

## GitHub Actions and releases

The workflow is `.github/workflows/release.yml`.

- Push a tag matching `v*` to build and publish a release.
- Or open Actions → Build Aurora R36 ARM releases → Run workflow and use a new release tag (default `v0.4.1`).

Each release attaches `Aurora_R36_armv7.zip` and `Aurora_R36_aarch64.zip`. CI runs the source/package checks, builds both ARM variants and verifies the archive paths and executable bits.

## Install a published device package

1. Back up the ROMS SD card.
2. Download the ARM package matching the firmware userspace from GitHub Releases.
3. Extract the ZIP on a computer.
4. Copy its `ports/` folder to the root of the ROMS partition, merging with the existing `ports/` folder.
5. Safely eject the SD card, boot the handheld, then open Ports → Install Aurora.
6. After the installer confirms success, open Ports → Aurora.

No terminal is required on the handheld when installing a published prebuilt release package.

## Verification status and limitations

- The source can be syntax-checked independently, but a final SDL2 link requires the development package for the selected build userspace.
- CI cross-build success verifies compilation and packaging; it does not replace testing on the physical R36 Ultra and its exact firmware.
- Physical button indices, audio output, sysfs status reporting and LED hardware support vary by firmware revision.
- The v0.4.1 source must be built and tested on the target hardware before it can be called a verified device-ready release.
