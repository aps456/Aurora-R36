# Aurora R36 v0.4.0 — development preview

## Interface

- Reworked the home screen into a clean, Apple Music-inspired vertical library list.
- Removed the bottom navigation bar.
- Added Recently Added, Songs, Artists, Albums, Playlists, Folders and Settings entries.
- Added a consistent system status strip with local time, Wi-Fi state and battery percentage/charging state where exposed by the firmware.
- Reworked Now Playing with centered artwork, centered title and metadata, a clean transport row, progress/timing and file-format details.
- Removed the Now Playing heading, down-arrow, cast/TV, like/favourite and three-dot controls.
- Retained the cover-art-derived background accent.

## Controller

- Removed the first-run remapping wizard.
- Added SDL GameController handling and a raw R36-family joystick fallback.
- D-pad / analog sticks navigate menus; analog sticks are ignored while Now Playing is open.
- In Now Playing, D-pad left/right taps change tracks and holding seeks in 5-second steps.
- In Now Playing, D-pad up/down changes volume with a temporary volume-level overlay.
- A selects / toggles playback, B goes back, X toggles shuffle, Y cycles repeat.
- Select + Start exits Aurora. Shoulder buttons remain unused.

## LEDs and firmware integration

- Added Off, Album Tone and LED Wave settings when an existing supported LED interface is detected.
- ArkOS4Clone JoyLED support uses its documented preset values; Batocera's helper is used only when detected.
- Unsupported firmware reports that joystick LED control is unavailable.
- LED Wave is a firmware/controller animation, not a sound-reactive audio visualizer. True audio-reactive visualization remains future work.

## Fixes

- Fixed playlist rendering so playlist pseudo-entries are never treated as track indices.
- Added Recently Added sorting using file modification times.
- Improved input hold/repeat handling and Now Playing return navigation.
- Avoided writing settings to storage for every repeated volume step.

## Verification status

- Repository/package tests and shell syntax checks pass.
- C source passes a strict syntax/object compile against a temporary SDL interface stub.
- This environment does not have the SDL2 development package, so no final SDL2 link or ARM release binary was produced here.
- Controller indices, live status values, LED output and playback must still be tested on the target R36 Ultra and its exact firmware.
