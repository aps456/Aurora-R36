# Aurora R36 v0.4.1

## Fixes and changes
- Normalize face-button events so physical A selects/plays, B backs, X shuffles and Y changes repeat mode.
- Correct raw R36-family Select/Start handling and use D-pad hat events rather than treating button indices 8–11 as directions.
- Require MPV IPC readiness before reporting the audio engine as available; check process exit during startup and report an actionable audio-engine error.
- Load home, playback and transport icons from the bundled `icons/` directory as external BMP assets with transparent backgrounds.
- Replace the violet-heavy surfaces with neutral dark and light appearance modes; persist the selected appearance.
- Cache extracted full-size covers and thumbnails in `Aurora-R36/artwork/` instead of the temporary directory.
- Add a dedicated update screen with clearer checking, downloading, verification and installation status messages.
- Update version metadata and release workflow defaults to 0.4.1.

## Important validation note
The repository/package checks pass. This environment does not have SDL2 development headers/libraries, so it cannot produce or validate a real SDL2-linked ARM executable. MPV is still a required device runtime dependency. The final playback and physical-button behaviour must be verified with the ARM release on the R36 Ultra before calling this release hardware-tested.
