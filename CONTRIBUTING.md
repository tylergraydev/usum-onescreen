# Contributing

Thanks for helping. Bug reports, tested fixes and new native screens are all welcome.

## Ground rules

- **Never commit or attach game files, keys or saves**: ROMs, CIAs, `code.bin`, `.cro` files, `aes_keys.txt`, save files, extracted assets or text dumps. Keep them in `analysis/private/`, which git ignores. Pull requests that contain any of these are closed.
- Don't ask for or link to game dumps in issues or pull requests.
- Be kind; see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Reporting a bug

Use the bug report form under **Issues**. Include:

- the game (Ultra Sun or Ultra Moon), the update's title version, and your Azahar version;
- `usum-onescreen.log` from the root of the emulated SD card;
- what you did, what you expected and what happened; a screenshot helps.

## Building and testing

See [README.md](README.md#building-from-source) for the plugin, the host tests and the launcher. Before opening a pull request:

1. `make` builds the plugin without warnings you added.
2. `make -C tests` passes.
3. If you changed `player/`, `dotnet build player/UsumOneScreen.csproj -c Release` builds.
4. If you changed in-game behaviour, test it in Azahar with your own dump and say what you tested. The scripted emulator tests are described in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Pull requests

- `main` is protected: every change goes through a pull request, and the CI checks (`build`, `host-tests`, `launcher`) must pass before it can merge.
- Branch from `main`, keep each pull request to one change, and fill in the template.
- Add a line to `CHANGELOG.md` under `## [Unreleased]` for anything a player would notice.
- Update the matching doc in `docs/` when you change how a screen or setting works.
- The maintainer reviews and merges.

## Licence

By contributing, you agree that your contribution is licensed under this repository's licence (see `LICENSE`).
