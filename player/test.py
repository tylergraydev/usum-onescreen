"""Launcher regression checks using synthetic headers only; no game content or emulator runs.

Run after building: python player/test.py
"""
from pathlib import Path
from contextlib import contextmanager
import shutil
import struct
import subprocess
import uuid


PLAYER = Path(__file__).resolve().parent
EXE = PLAYER / 'bin/Release/net48/UsumOneScreen.exe'
SUN = 0x00040000001B5000
MOON = 0x00040000001B5100


@contextmanager
def workspace():
    # Default inherited permissions also work in a restricted Windows sandbox.
    root = PLAYER / 'obj' / ('launcher-test-' + uuid.uuid4().hex)
    root.mkdir()
    try:
        yield root
    finally:
        shutil.rmtree(root)


def ncch(title, encrypted=False):
    data = bytearray(0x400)
    data[0x100:0x104] = b'NCCH'
    struct.pack_into('<Q', data, 0x118, title)
    data[0x18F] = 0 if encrypted else 4
    return data


def tmd(title, version):
    data = bytearray(0xB40)
    struct.pack_into('>I', data, 0, 0x10004)
    struct.pack_into('>Q', data, 0x140 + 0x4C, title)
    struct.pack_into('>HH', data, 0x140 + 0x9C, version, 1)
    return data


def cia(title, version, encrypted=False):
    data = bytearray(0x2020)
    struct.pack_into('<I', data, 0, 0x2020)
    metadata = tmd(title, version)
    struct.pack_into('<I', data, 0x10, len(metadata))
    data += bytes(0x20) + metadata + ncch(title, encrypted)
    return data


def main():
    if not EXE.exists():
        raise SystemExit('Build player/UsumOneScreen.csproj in Release first.')
    with workspace() as root:
        app = root / 'app'
        app.mkdir()
        exe = app / EXE.name
        shutil.copy2(EXE, exe)
        counter = 0

        def fixture(data):
            nonlocal counter
            counter += 1
            path = root / f'{counter}.bin'
            path.write_bytes(data)
            return path

        def run(args, code, *messages):
            result = subprocess.run([str(exe), *map(str, args)], capture_output=True,
                                    encoding='utf-8', timeout=30)
            output = result.stdout + result.stderr
            assert result.returncode == code, output
            for message in messages:
                assert message in output, output
            return output

        games = {title: fixture(ncch(title)) for title in (SUN, MOON)}
        updates = {}
        for title, name in ((SUN, 'Ultra Sun'), (MOON, 'Ultra Moon')):
            run(['check', games[title]], 0, name)
            run(['check', fixture(ncch(title, True))], 1, 'encrypted', '2126.2')
            for version in (0x0820, 0x0800, 0x0810):
                update = fixture(cia(title | 0xE00000000, version))
                updates[title, version] = update
                accepted = version == 0x0820 or (title == SUN and version == 0x0800)
                output = run(['check', update], 0 if accepted else 1, name)
                assert ('warning:' in output) == (title == SUN and version == 0x0800)
            run(['check', fixture(cia(title | 0xE00000000, 0x0820, True))], 1, 'encrypted')
        run(['check', fixture(ncch(0))], 1, 'Pick')

        for title, other in ((SUN, MOON), (MOON, SUN)):
            run(['setup', '--home', root / 'unused', '--rom', games[title], '--update',
                 updates[other, 0x0820]], 1, 'matching v1.2 update')
        assert not (root / 'unused').exists(), 'Mismatch must fail before modifying the home'

        # A deliberately inert source lets setup copy files without executing Azahar.
        bundle = app / 'azahar-bundled'
        bundle.mkdir()
        (bundle / 'azahar.exe').write_bytes(b'synthetic executable marker')
        (bundle / 'license.txt').write_text('synthetic licence marker')
        before = {p.name: p.read_bytes() for p in bundle.iterdir()}
        plugin = fixture(b'synthetic plugin marker')
        home = root / 'home'
        for title, name in ((SUN, 'Ultra Sun'), (MOON, 'Ultra Moon'), (SUN, 'Ultra Sun')):
            content = home / 'azahar/user/sdmc/Nintendo 3DS/0/0/title/0004000e' / f'{title & 0xFFFFFFFF:08x}' / 'content'
            content.mkdir(parents=True, exist_ok=True)
            (content / '0.tmd').write_bytes(tmd(title | 0xE00000000, 0x0820))
            run(['setup', '--home', home, '--rom', games[title], '--plugin', plugin,
                 '--azahar', root / 'missing'], 0, f'Ready to play {name}')
            assert f'title_id={title:016X}' in (home / 'usum-onescreen-app.ini').read_text()
            installed = home / 'azahar/user/sdmc/luma/plugins' / f'{title:016X}' / 'usum-onescreen.3gx'
            assert installed.read_bytes() == plugin.read_bytes()
            run(['status', '--home', home], 0, f'game: {name}', 'ready to play')
        assert {p.name: p.read_bytes() for p in bundle.iterdir()} == before

        # CIA base games must resolve their own installed title and boot file.
        content = home / 'azahar/user/sdmc/Nintendo 3DS/0/0/title/00040000/001b5100/content'
        content.mkdir(parents=True)
        (content / '0.tmd').write_bytes(tmd(MOON, 0))
        boot = content / '00000000.app'
        boot.write_bytes(b'synthetic content marker')
        run(['setup', '--home', home, '--rom', fixture(cia(MOON, 0)),
             '--plugin', plugin], 0, 'Ultra Moon', str(boot))

        # Without a bundle, explicit source selection still works.
        fallback = root / 'source'
        bundle.rename(fallback)
        run(['setup', '--home', home, '--rom', games[MOON], '--plugin', plugin,
             '--azahar', fallback], 0, 'Ultra Moon')
        # Installed Moon 0x0800 must not pass readiness checks.
        content = home / 'azahar/user/sdmc/Nintendo 3DS/0/0/title/0004000e/001b5100/content'
        (content / '0.tmd').write_bytes(tmd(MOON | 0xE00000000, 0x0800))
        run(['status', '--home', home], 0, 'not ready: The Ultra Moon v1.2 update')
        run(['setup', '--home', home, '--rom', games[MOON], '--plugin', plugin],
            1, 'Pick the Ultra Moon v1.2 update')
        # An old Sun-only state file needs no migration before play.
        state = home / 'usum-onescreen-app.ini'
        state.write_text('\n'.join(line for line in state.read_text().splitlines()
                                   if not line.startswith('title_id=')))
        run(['status', '--home', home], 0, 'game: Ultra Sun', 'ready to play')
    print('PASS: validation, cross-game rejection, title state/paths, bundled source priority and immutability, installed-update checks, fallback source.')


if __name__ == '__main__':
    main()
