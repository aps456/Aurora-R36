# Upload the corrected Aurora R36 repository files

This archive is a **source repository upload package**, not the install ZIP for the handheld.

1. Clone `https://github.com/aps456/Aurora-R36` on your computer with GitHub Desktop.
2. Extract every item in this archive into the cloned repository's root folder. Merge folders and replace files when asked. Do not place the contents inside another `Aurora-R36` subfolder.
3. Make sure the hidden `.github/workflows/release.yml` file is included. On some file managers, hidden folders are not shown by default.
4. In GitHub Desktop, review the changes, commit them to `main`, and push.
5. On GitHub, open Actions → Build Aurora R36 ARM releases → Run workflow. Keep the release tag as `v0.3.0` for the first release, unless that tag already exists.
6. Wait for both ARM build jobs and the publish job to complete. Then check the Releases page for `Aurora_R36_armv7.zip` and `Aurora_R36_aarch64.zip`.

The archive preserves Unix executable bits on shell scripts. The GitHub Actions workflow also applies executable permissions before packaging, so the final ArkOS ZIP has executable `.sh` launchers and an executable `aurora` binary.

Do not upload this repository ZIP as a single file and expect GitHub to unpack it. Extract it first, then commit its contents to the repository.
