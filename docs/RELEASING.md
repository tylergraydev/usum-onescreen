# Cutting a release

`.github/workflows/release.yml` runs when a tag starting with `v` is pushed. It builds the plugin the same way as `build.yml` and attaches `usum-onescreen.3gx` to a **draft** GitHub Release for that tag. Nothing is published until you publish the draft.

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

If the build fails, fix it on `main`, then move the tag (`git tag -d v0.1.0`, `git push origin :refs/tags/v0.1.0`, tag again and push). Re-running the workflow for a tag whose draft already exists replaces the attached `.3gx` instead of creating a second release.

Before the first public release, see `docs/LICENSING.md`. Known loose ends in this process (the version in `Sources/main.cpp` is not checked by the workflow, the toolchain is unpinned, the release body is generic) are listed in `docs/RELEASE-NOTES-v0.1.md`.
