"""Launcher regression checks using synthetic headers only; no game content or emulator runs.

Run after building: python player/test.py
"""
from pathlib import Path
from contextlib import contextmanager
import configparser
import hashlib
import math
import os
import zipfile
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
        # Never inherit network download settings from the caller.
        env = dict(os.environ)
        env.update(USUM_AZAHAR_URL=(root / 'missing.zip').as_uri(),
                   USUM_AZAHAR_LICENSE_URL=(root / 'missing-license.txt').as_uri(),
                   USUM_AZAHAR_SHA256='0' * 64, USUM_AZAHAR_LICENSE_SHA1='0' * 40)

        def fixture(data):
            nonlocal counter
            counter += 1
            path = root / f'{counter}.bin'
            path.write_bytes(data)
            return path

        def run(args, code, *messages):
            result = subprocess.run([str(exe), *map(str, args)], capture_output=True,
                                    encoding='utf-8', timeout=30, env=env)
            output = result.stdout + result.stderr
            assert result.returncode == code, output
            for message in messages:
                assert message in output, output
            return output

        def refused_download(target, message):
            # Exercise the downloader directly: Setup intentionally falls back to
            # machine installs, which would make this check host-dependent.
            script = root / 'download-test.ps1'
            script.write_text("""param($executable, $target)
$assembly = [Reflection.Assembly]::LoadFile($executable)
$homeType = $assembly.GetType('UsumOneScreen.Home')
$launcherHome = [Activator]::CreateInstance($homeType, [object[]]@($target))
$logger = [Action[string]] { param($line) [Console]::WriteLine($line) }
try {
    $assembly.GetType('UsumOneScreen.AzaharDownload').GetMethod('Install').Invoke($null, [object[]]@($launcherHome, $logger))
    exit 0
} catch {
    [Console]::WriteLine($_.Exception.ToString())
    exit 1
}
""")
            result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(script), str(exe), str(target)],
                                    capture_output=True, encoding='utf-8', env=env, timeout=30)
            assert result.returncode == 1, result.stdout + result.stderr
            assert message in result.stdout, result.stdout + result.stderr

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
        # Widescreen changes both INIs, preserving plugin comments.
        def widescreen_files(target):
            return (target / 'azahar/user/sdmc/usum-onescreen.ini',
                    target / 'azahar/user/config/qt-config.ini')

        def is_widescreen(value):
            if value in ('16:9', '21:9'):
                return True
            try:
                ratio = float(value)
                return math.isfinite(ratio) and ratio > 0
            except ValueError:
                return False

        def check_widescreen(target, expected):
            plugin_ini, qt_ini = widescreen_files(target)
            plugin_config = configparser.ConfigParser(inline_comment_prefixes=(';', '#'))
            plugin_config.read(plugin_ini)
            aspect = plugin_config['display']['aspect']
            assert aspect in expected, aspect
            qt_config = configparser.ConfigParser()
            qt_config.read(qt_ini)
            stretch = 'true' if is_widescreen(aspect) else 'false'
            assert qt_config['Layout'][r'screen_top_stretch\default'] == 'false'
            assert qt_config['Layout']['screen_top_stretch'] == stretch
            lines = qt_ini.read_text().splitlines()
            assert lines.count(r'screen_top_stretch\default=false') == 1
            assert lines.count('screen_top_stretch=' + stretch) == 1

        plugin_ini, qt_ini = widescreen_files(home)
        comment = '; Experimental widescreen: 0/off, 16:9, 21:9, or a positive float.'
        trailing = '; keep this aspect comment'
        plugin_ini.write_text('[display]\n' + comment + '\naspect = 0  ' + trailing + '\n')
        for choice in ('16:9', '21:9', '0', 'off', 'auto'):
            run(['set', '--home', home, 'aspect=' + choice, 'size=2/3'], 0)
            expected = {'0', '16:9', '21:9'} if choice == 'auto' else {'0' if choice == 'off' else choice}
            check_widescreen(home, expected)
            assert comment in plugin_ini.read_text() and trailing in plugin_ini.read_text()
            assert 'aspect=' + choice + '\n' in (home / 'usum-onescreen-app.ini').read_text()
            if choice == 'auto':
                run(['status', '--home', home], 0, '(auto)')
            # Setup repairs stretch and strips per-game overrides every time.
            qt_ini.write_text('[Layout]\nscreen_top_stretch\\default=true\nscreen_top_stretch=false\n')
            custom = home / 'azahar/user/config/custom/game.ini'
            custom.parent.mkdir(exist_ok=True)
            custom.write_text('[Layout]\nscreen_top_stretch\\default=false\nscreen_top_stretch=true\nkeep=1\n')
            run(['setup', '--home', home, '--rom', games[MOON], '--plugin', plugin], 0, 'Ready to play')
            check_widescreen(home, expected)
            assert 'screen_top_stretch' not in custom.read_text()
            assert 'keep=1' in custom.read_text()
            assert comment in plugin_ini.read_text() and trailing in plugin_ini.read_text()

        tracked = (plugin_ini, qt_ini, home / 'usum-onescreen-app.ini')
        for bad in ('wide', '-2.1', '0.0', 'NaN', 'Infinity', '1e40', '2,1'):
            before = {p: p.read_bytes() for p in tracked}
            run(['set', '--home', home, 'size=1/2', 'aspect=' + bad], 1, 'bad aspect')
            assert {p: p.read_bytes() for p in tracked} == before
        for choice in ('2.1', '1e-1'):
            run(['set', '--home', home, 'aspect=' + choice], 0)
            check_widescreen(home, {choice})

        # Auto thresholds are monitor-independent; custom UI values survive save.
        script = root / 'aspect-test.ps1'
        script.write_text(r"""param($executable, $target, $expectedAspect)
$ErrorActionPreference = 'Stop'
$assembly = [Reflection.Assembly]::LoadFile($executable)
$resolve = $assembly.GetType('UsumOneScreen.PluginSettings').GetMethod('ResolveAspect')
foreach ($case in @(@(1600, 1000, '0'), @(1700, 1000, '16:9'), @(2199, 1000, '16:9'), @(2200, 1000, '21:9'), @(3440, 1440, '21:9'))) {
    $actual = $resolve.Invoke($null, [object[]]@('auto', [int]$case[0], [int]$case[1]))
    if ($actual -ne $case[2]) { throw "Bad Auto resolution: $actual" }
}
$isWide = $assembly.GetType('UsumOneScreen.PluginSettings').GetMethod('IsWidescreen')
foreach ($value in @('16:9', '21:9', '2.1', '1e-1')) {
    if (!$isWide.Invoke($null, [object[]]@($value))) { throw "Expected widescreen: $value" }
}
foreach ($value in @('0', 'off', '0.0', '', 'wide', 'NaN', 'Infinity', '-2.1', '2,1')) {
    if ($isWide.Invoke($null, [object[]]@($value))) { throw "Expected widescreen off: $value" }
}
$launcherHome = [Activator]::CreateInstance($assembly.GetType('UsumOneScreen.Home'), [object[]]@($target))
$form = [Activator]::CreateInstance($assembly.GetType('UsumOneScreen.MainForm'), [object[]]@($launcherHome))
try {
    $flags = [Reflection.BindingFlags]'Instance, NonPublic'
    $dropdown = $form.GetType().GetField('widescreen', $flags).GetValue($form)
    if ($dropdown.Text -ne $expectedAspect) { throw "Wrong aspect loaded: $($dropdown.Text)" }
    if (!$form.GetType().GetMethod('SavePluginSettings', $flags).Invoke($form, @())) { throw 'Save failed' }
} finally { $form.Dispose() }
""")
        run(['set', '--home', home, 'aspect=2.1', 'controls=custom'], 0)
        # An unlisted aspect read from the plugin INI must survive unchanged too.
        state = home / 'usum-onescreen-app.ini'
        state.write_text('\n'.join(line for line in state.read_text().splitlines()
                                   if not line.startswith('aspect=')) + '\n')
        def save_form(expected):
            result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(script), str(exe), str(home), expected],
                                    capture_output=True, encoding='utf-8', env=env, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr

        save_form('Custom (2.1)')
        check_widescreen(home, {'2.1'})
        assert comment in plugin_ini.read_text() and trailing in plugin_ini.read_text()

        # Concrete state must not overwrite a hand-edited plugin aspect.
        run(['set', '--home', home, 'aspect=16:9'], 0)
        plugin_ini.write_text('[display]\naspect = 21:9\n')
        save_form('21:9')
        check_widescreen(home, {'21:9'})

        # Numeric zero and bad hand-written values disable stretch on setup.
        for value in ('0.0', 'off', 'wide', 'NaN', 'Infinity', ''):
            plugin_ini.write_text('[display]\naspect = ' + value + '\n')
            run(['setup', '--home', home, '--rom', games[MOON], '--plugin', plugin], 0, 'Ready to play')
            check_widescreen(home, {value})

        # A choice saved before Azahar is copied applies on later setup.
        earlyhome = root / 'earlyhome'
        run(['set', '--home', earlyhome, 'aspect=16:9'], 0)
        early_plugin, early_qt = widescreen_files(earlyhome)
        assert 'aspect = 16:9' in early_plugin.read_text()
        assert not early_qt.exists()

        # Headers in nested zip entries classify both files; backslash traversal
        # is flattened to a basename inside games, never written outside it.
        archive = root / 'games.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('nested/deep/game.3ds', ncch(SUN))
            z.writestr('nested/deep/update.cia', cia(SUN | 0xE00000000, 0x0820))
            z.writestr(r'..\evil.cia', cia(SUN | 0xE00000000, 0x0820))
            z.writestr('ignored.txt', b'not a game')
        ziphome = root / 'ziphome'
        run(['check', '--home', ziphome, archive], 0, 'Ultra Sun', 'update v1.2')
        extracted = ziphome / 'games'
        assert {p.name for p in extracted.iterdir()} == {'game.3ds', 'update.cia', 'evil.cia'}
        assert not (ziphome / 'evil.cia').exists()
        before_extract = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in extracted.iterdir()}
        run(['check', '--home', ziphome, archive], 0, 'Using existing')
        assert {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in extracted.iterdir()} == before_extract
        # Preinstall only synthetic metadata, so setup must never launch the dummy exe.
        def installed_update(target):
            content = target / 'azahar/user/sdmc/Nintendo 3DS/0/0/title/0004000e/001b5000/content'
            content.mkdir(parents=True, exist_ok=True)
            (content / '0.tmd').write_bytes(tmd(SUN | 0xE00000000, 0x0820))

        installed_update(earlyhome)
        for _ in range(2):
            run(['setup', '--home', earlyhome, '--rom', games[SUN], '--azahar', fallback,
                 '--plugin', plugin], 0, 'Ready to play')
            check_widescreen(earlyhome, {'16:9'})

        installed_update(ziphome)
        run(['setup', '--home', ziphome, '--rom', archive, '--azahar', fallback],
            0, 'Ultra Sun update v1.2', 'Ready to play')
        assert f'rom={extracted / "game.3ds"}' in (ziphome / 'usum-onescreen-app.ini').read_text()
        # The copied launcher has no adjacent plugin: require actual embedded bytes.
        embedded = ziphome / 'azahar/user/sdmc/luma/plugins/00040000001B5000/usum-onescreen.3gx'
        assert embedded.read_bytes() == (PLAYER.parent / 'usum-onescreen.3gx').read_bytes()
        assert not list(ziphome.glob('plugin-*.tmp'))
        # Adjacent and explicit plugin sources override the embedded one.
        adjacent = app / 'usum-onescreen.3gx'
        adjacent.write_bytes(b'adjacent marker')
        run(['setup', '--home', ziphome, '--rom', games[SUN]], 0, 'Ready to play')
        assert embedded.read_bytes() == adjacent.read_bytes()
        run(['setup', '--home', ziphome, '--rom', games[SUN], '--plugin', plugin], 0, 'Ready to play')
        assert embedded.read_bytes() == plugin.read_bytes()
        adjacent.unlink()

        download = root / 'download.zip'
        with zipfile.ZipFile(download, 'w') as z:
            z.writestr('azahar-2126.2/azahar.exe', b'synthetic executable marker')
            z.writestr('azahar-2126.2/support.dll', b'synthetic support marker')
        license = root / 'license.txt'
        license.write_bytes(b'synthetic licence\n')
        license_bytes = license.read_bytes()
        env.update(USUM_AZAHAR_URL=download.as_uri(),
                   USUM_AZAHAR_SHA256=hashlib.sha256(download.read_bytes()).hexdigest(),
                   USUM_AZAHAR_LICENSE_URL=license.as_uri(),
                   USUM_AZAHAR_LICENSE_SHA1=hashlib.sha1(
                       f'blob {len(license_bytes)}\0'.encode('ascii') + license_bytes).hexdigest())
        downloadhome = root / 'downloadhome'
        installed_update(downloadhome)
        run(['setup', '--home', downloadhome, '--rom', games[SUN]], 0,
            'Downloading tested Azahar 2126.2', 'Ready to play')
        assert (downloadhome / 'azahar/azahar.exe').read_bytes() == b'synthetic executable marker'
        assert (downloadhome / 'azahar/LICENSE-azahar.txt').read_bytes() == license_bytes
        assert not list(downloadhome.glob('azahar-download-*'))
        env['USUM_AZAHAR_SHA256'] = '0' * 64
        badhome = root / 'bad-hash'
        refused_download(badhome, 'SHA-256 mismatch')
        assert not (badhome / 'azahar').exists()
        assert not list(badhome.iterdir())
        # A failed download can still use a discovered installed source.
        discovered = root / 'discovered'
        discovered.mkdir()
        (discovered / 'usum-onescreen-app.ini').write_text(f'azahar_source={fallback}\n')
        installed_update(discovered)
        run(['setup', '--home', discovered, '--rom', games[SUN]], 0,
            'SHA-256 mismatch', 'Ready to play')
        env['USUM_AZAHAR_SHA256'] = hashlib.sha256(download.read_bytes()).hexdigest()
        env['USUM_AZAHAR_LICENSE_SHA1'] = '0' * 40
        badlicense = root / 'bad-license'
        refused_download(badlicense, 'licence git blob SHA-1 mismatch')
        assert not (badlicense / 'azahar').exists()
        assert not list(badlicense.iterdir())
        env['USUM_AZAHAR_LICENSE_SHA1'] = hashlib.sha1(
            f'blob {len(license_bytes)}\0'.encode('ascii') + license_bytes).hexdigest()
        with zipfile.ZipFile(download, 'w') as z:
            z.writestr('a/azahar.exe', b'fake')
            z.writestr('b/azahar.exe', b'fake')
        env['USUM_AZAHAR_SHA256'] = hashlib.sha256(download.read_bytes()).hexdigest()
        badlayout = root / 'bad-layout'
        refused_download(badlayout, 'Expected exactly one azahar.exe')
        assert not list(badlayout.iterdir())
        env['USUM_AZAHAR_URL'] = (root / 'missing.zip').as_uri()
        run(['setup', '--home', discovered, '--rom', games[SUN], '--azahar', fallback],
            0, 'Ready to play')
        # Different headers with a colliding name get a suffix, preserving both.
        collision = root / 'collision.zip'
        with zipfile.ZipFile(collision, 'w') as z:
            z.writestr('game.3ds', ncch(MOON))
        original = (extracted / 'game.3ds').read_bytes()
        run(['check', '--home', ziphome, collision], 0, 'Ultra Moon')
        assert (extracted / 'game.3ds').read_bytes() == original
        assert (extracted / 'game-1.3ds').read_bytes() == ncch(MOON)
        # Explicit --update zip works independently of a plain ROM.
        updatezip = root / 'update.zip'
        with zipfile.ZipFile(updatezip, 'w') as z:
            z.writestr('deep/update.cia', cia(SUN | 0xE00000000, 0x0820))
        run(['setup', '--home', ziphome, '--rom', games[SUN], '--update', updatezip], 0, 'Ready to play')
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
    print('PASS: validation, cross-game rejection, title state/paths, bundled source priority and immutability, installed-update checks, fallback source; offline pinned download and hash refusal; zip classification, traversal safety, reuse and state; embedded/adjacent/explicit plugin priority; widescreen choices, Auto thresholds, custom UI preservation, comment retention, invalid-value immutability, stretch sync, stale-state preservation, numeric-zero/bad-value handling and per-game cleanup.')


if __name__ == '__main__':
    main()
