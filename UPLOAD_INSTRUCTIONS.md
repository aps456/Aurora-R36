# Upload Aurora R36 v0.4.1 source to GitHub

1. Extract this ZIP.
2. Open your existing `aps456/Aurora-R36` repository in GitHub Desktop.
3. Copy the extracted repository contents into the local repository, replacing older source files. Keep your `.git` directory untouched.
4. Commit the changes with message `Aurora v0.4.1 playback, controls, themes and assets` and push to `main`.
5. In GitHub, open **Actions → Build Aurora R36 ARM releases → Run workflow**.
6. Enter release tag `v0.4.1` and run the workflow.
7. Wait for both ARMv7 and AArch64 jobs and the release-publishing job to finish successfully.
8. Download the package matching your device architecture from the GitHub Release.

Do not install a package until the ARM workflow has completed successfully. The source ZIP is not an installable handheld package. MPV must be available in the handheld firmware for audio playback.
