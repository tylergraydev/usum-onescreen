# Cutting a release

`.github/workflows/release.yml` runs when a tag starting with `v` is pushed. It builds the plugin the same way as `build.yml` then builds the Windows launcher and attaches both `usum-onescreen.3gx` and `usum-onescreen-<tag>-windows.zip` to a **draft** GitHub Release for that tag. Nothing is published until you publish the draft.

1. On `main`, set the version in `usum-onescreen.plgInfo` (`Major`, `Minor`, `Revision`) and in the `PluginMenu` line in `Sources/main.cpp`. The workflow fails if the tag doesn't equal `v<Major>.<Minor>.<Revision>` from `usum-onescreen.plgInfo`.
2. In `CHANGELOG.md`, turn the release's `### vX.Y.Z` heading (nested under `## [Unreleased]`) into `## [X.Y.Z] - DATE`, leaving `## [Unreleased]` empty above it. Commit and push, and wait for the Build workflow (build, host tests, scenario parse) to pass on that commit. Before the first release, also work through the checklist in `docs/RELEASE-NOTES-v0.1.md` (scenario pass, controller test).
3. Tag that commit and push the tag:

   ```bash
   git tag -a v0.1.0 -m "v0.1.0"
   ```

   ```bash
   git push origin v0.1.0
   ```

4. Watch the Release workflow in the Actions tab. When it finishes, open Releases, edit the draft "USUM One Screen v0.1.0": paste the changelog section into the notes (for v0.1, the "Release notes text" in `docs/RELEASE-NOTES-v0.1.md`), tick "Set as a pre-release" for betas, and publish.

If the build fails, fix it on `main`, then move the tag (`git tag -d v0.1.0`, `git push origin :refs/tags/v0.1.0`, tag again and push). Re-running the workflow for a tag whose draft already exists replaces both attached artifacts instead of creating a second release.

Before the first public release, see `docs/LICENSING.md`. Known loose ends in this process (the version in `Sources/main.cpp` is not checked by the workflow, the toolchain is unpinned, the release body is generic) are listed in `docs/RELEASE-NOTES-v0.1.md`.

## Windows player job

The `windows` job runs on `windows-latest` after the plugin build/tag check succeeds. It downloads that plugin artifact and the official [Azahar 2126.2 Windows MSVC zip](https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar-windows-msvc-2126.2.zip). Before extraction, SHA-256 must equal `4412eac8d13123c8840f11890fc26e5b0e4c76d16aa28a608941377b948a9db0`; any mismatch fails the job. It finds the folder containing `azahar.exe` and calls `player/build.ps1 -AzaharDir PATH -AzaharLicense FILE`, then zips the contents of `player/dist/` as `usum-onescreen-<tag>-windows.zip`.

The Windows distribution may omit the licence. CI separately downloads [license.txt at tag 2126.2](https://raw.githubusercontent.com/azahar-emu/azahar/2126.2/license.txt) (19,281 bytes), verifies its git blob hash is exactly `7b0e5ad1bddc73ac44aca523174d22ee0d0dcc56`, and passes it as `-AzaharLicense`. The hash is SHA-1 of the ASCII header `blob <byte count>` followed by a NUL byte and the original file bytes, without newline conversion. A mismatch fails the job. The supplied file is included as `azahar-bundled/LICENSE-azahar.txt` and at the zip root; any licences already in the Azahar folder are kept. Local bundling requires either a licence in that folder (`COPYING` / `license*`) or `-AzaharLicense FILE`.

The zip includes the launcher, plugin, player README, unmodified `azahar-bundled/`, upstream licence text and `AZAHAR-SOURCE.txt`. The source notice identifies Azahar 2126.2, GPL, and [the corresponding upstream release](https://github.com/azahar-emu/azahar/releases/tag/2126.2): `azahar-unified-source-2126.2.tar.xz`, SHA-256 `85c8b56fac703df04720125044b8ff887997911858a6cde545dfb1107ba4cd26`. Use only a clean emulator distribution, without game files, keys or a portable user folder.

The `release` job waits for both builds, downloads both artifacts and creates a draft with both attachments. Publication remains manual (`--draft`); inspect the zip and test a fresh setup for each game before publishing. Local packaging uses `player/build.ps1 -AzaharDir PATH -AzaharLicense FILE`; omit `-AzaharLicense` if the folder already has a licence. Omitting `-AzaharDir` builds the launcher for an existing Azahar installation.
