# Aurora R36 — Native C / SDL2

A lightweight fullscreen music player for ArkOS-family R36 Ultra handhelds. The UI is C11 + SDL2; MPV handles audio playback and FFmpeg/ffprobe provide metadata and album-art extraction.

- Repository: https://github.com/aps456/Aurora-R36
- Source version: 0.3.0
- License: GNU GPL v3 (the repository's existing license is retained)

## Source features

- First-run controller mapping for UP, DOWN, LEFT, RIGHT, CONFIRM, BACK, NEXT and PREVIOUS; remap later in Settings → Button Setup.
- 720×720 Now Playing view with large square artwork, larger centered song title, centered `Album Artist | Album | Year`, codec/bit depth/sample rate, progress and timestamps, shuffle, previous, play/pause, next, repeat and three-dot affordance (no star icon).
- Background tint extracted from a dominant quantized album-art colour.
- Songs, Artists, Albums and Playlists; folder selection, metadata scanning and sorting.
- Settings → Check for Updates retrieves the matching ARM release package from this repository and replaces only the executable, preserving playlists, settings and music.

## Runtime dependencies on ArkOS

- SDL2 runtime compatible with the firmware's display backend
- MPV
- FFmpeg and ffprobe
- curl and unzip (for in-app updates)

The executable dynamically links SDL2. The correct binary depends on the device's userspace architecture, which can be checked with `uname -m` from a computer-side diagnostic or device terminal. The release workflow generates ARMv7 and AArch64 builds using Debian Bullseye ARM userspace. Physical testing on the exact R36 Ultra firmware is still required before claiming compatibility.

## Build locally

On Linux with SDL2 development headers, `pkg-config`, a C compiler and Make:

```sh
make clean all
./build/aurora --version
sh package-release.sh armv7   # or aarch64
```

The package script produces `Aurora_R36_<arch>.zip` with the required ArkOS `ports/` structure and executable permissions.

## GitHub Actions and releases

The workflow is at `.github/workflows/release.yml`.

- Push a tag matching `v*` to build and publish a release.
- Or open Actions → Build Aurora R36 ARM releases → Run workflow and provide a release tag (default `v0.3.0`).

Each release attaches:

- `Aurora_R36_armv7.zip`
- `Aurora_R36_aarch64.zip`

The workflow compiles in the matching ARM Debian Bullseye userspace, runs source/package checks, packages the binary, then verifies both the required Ports paths and executable ZIP modes before publishing.

## Install on the R36 Ultra without a terminal

1. Open the GitHub Releases page and download the architecture package that matches the console's OS userspace.
2. Extract the ZIP on a computer.
3. Copy the ZIP's `ports/` folder to the root of the SD card's ROMS partition, merging with the existing `ports/` folder.
4. Safely eject the SD card and boot the console.
5. Open Ports → Install Aurora. After the installer confirms, open Ports → Aurora.

The device ZIP includes:

```text
ports/
├── Aurora.sh
├── Install Aurora.sh
└── Aurora-R36/
    ├── aurora
    ├── Aurora.sh
    ├── install.sh
    ├── update.sh
    ├── github_repo.txt
    ├── build-info.txt
    ├── README.md
    └── LICENSE
```

No terminal is required on the handheld for installing a published prebuilt package.

## In-app updates

The repository is configured in `github_repo.txt` as `aps456/Aurora-R36`. Once a GitHub Release has assets attached, choose Settings → Check for Updates. The updater selects the ARM asset from `uname -m`, downloads and verifies that the binary starts, then atomically replaces the application executable. User configuration, cached metadata, playlists and music remain in place.

## Limitations and verification

- Crossfade options currently use different fade curves; beat detection, vocal separation and real dual-track overlap are not implemented.
- CI cross-build output still needs a successful GitHub Actions run and testing on the physical console. The local preparation environment does not have SDL2 development headers, so a local SDL2 link was not run here.
- SDL2 KMSDRM setup, physical controller mappings, actual audio output and minimum GLIBC compatibility vary by firmware image. If the binary does not start, do not modify firmware; capture the error/diagnostics and build against the matching device sysroot.
- Codec compatibility depends on the installed MPV/FFmpeg build; no player can guarantee every proprietary or uncommon format.
