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


def save_manager(run, root):
    home = root / 'save-manager'
    save = home / 'azahar/user/sdmc/Nintendo 3DS/id0/id1/title/00040000/001b5000/data'
    run(['slot', 'save', 'empty', '--home', home], 1, 'No save yet')
    run(['export', root / 'empty.zip', '--home', home], 1, 'No save data')
    save.mkdir(parents=True)
    (save / 'main').write_bytes(b'slot original')
    (save / 'nested').mkdir()
    (save / 'nested/extra').write_bytes(b'nested save')
    extdata = save.parents[3] / 'extdata/untouched'
    extdata.parent.mkdir()
    extdata.write_bytes(b'not save data')
    slots = home / f'slots/{SUN:016X}'
    backups = home / f'backups/{SUN:016X}'

    def cli(args, code=0, *messages):
        return run([*args, '--home', home], code, *messages)

    cli(['slot', 'save', 'before Elite Four'])
    slot = next(slots.iterdir())
    assert len(slot.name) == 32 and (slot / 'name.txt').read_text() == 'before Elite Four'
    assert (slot / 'data/main').read_bytes() == b'slot original'
    assert not list(slot.rglob('extdata'))
    cli(['slot', 'list'], 0, 'before Elite Four', 'bytes')
    for bad in ('', ' ', ' trailing', 'trailing ', 'trailing.', '.', '..', 'a/b', r'a\b',
                'a:b', 'a*', 'a?', 'a"', 'a<', 'a>', 'a|', 'a\n', 'x' * 65):
        cli(['slot', 'save', bad], 1, 'Slot names')
    cli(['slot', 'save', 'BEFORE ELITE FOUR'], 1, 'already exists')
    cli(['slot', 'save', 'other'])
    cli(['slot', 'rename', 'other', 'BEFORE ELITE FOUR'], 1, 'already exists')
    cli(['slot', 'rename', 'other', 'OTHER'])  # Case-only rename.
    cli(['slot', 'rename', 'before Elite Four', 'Champion'])
    cli(['slot', 'load', 'before Elite Four'], 1, 'Slot not found')
    cli(['slot', 'delete', 'OTHER'])
    assert len(list(slots.iterdir())) == 1
    cli(['slot', 'delete', '../escape'], 1, 'Slot names')
    (save / 'main').write_bytes(b'current before load')
    (save / 'obsolete').write_bytes(b'remove me')
    cli(['slot', 'load', 'champion'])
    assert (save / 'main').read_bytes() == b'slot original'
    assert not (save / 'obsolete').exists()
    assert (next(backups.iterdir()) / 'data/main').read_bytes() == b'current before load'
    assert extdata.read_bytes() == b'not save data'
    for _ in range(12):
        cli(['backup'])
    assert len(list(backups.iterdir())) == 10 and slot.exists()
    assert (slot / 'data/main').read_bytes() == b'slot original'
    # Failure to write the safety backup must leave the current save untouched.
    shutil.rmtree(backups)
    backups.write_bytes(b'blocked backup directory')
    (save / 'main').write_bytes(b'must survive failed load')
    cli(['slot', 'load', 'Champion'], 1)
    assert (save / 'main').read_bytes() == b'must survive failed load'
    assert not list(save.parent.glob('*.restore-*'))
    backups.unlink()
    cli(['backup'])
    backup = next(backups.iterdir()).name
    archives = {}
    for kind, flags in [('current', []), ('slot', ['--slot', 'Champion']), ('backup', ['--backup', backup])]:
        archive = root / (kind + '.zip')
        cli(['export', archive, *flags], 0, 'Save exported')
        archives[kind] = archive
        with zipfile.ZipFile(archive) as z:
            manifest = dict(line.split('=', 1) for line in z.read('manifest.txt').decode().splitlines())
            assert manifest['title_id'] == f'{SUN:016X}' and manifest['format'] == '1'
            assert manifest['date'] and manifest['source'].startswith(kind) and manifest['launcher_version']
            assert z.read('data/nested/extra') == b'nested save'
            assert not any('extdata' in n for n in z.namelist())
        cli(['import', archive, '--name', 'imported ' + kind], 0, 'Slot imported')
    cli(['export', archives['slot']], 1, 'already exists')
    cli(['export', root / 'bad.zip', '--slot', 'Champion', '--backup', backup], 1, 'either')
    cli(['export', save / 'bad.zip'], 1, 'Export outside')
    cli(['import', archives['slot'], '--name', 'Champion'], 1, 'already exists')
    cli(['import', archives['slot']], 0, 'Slot imported: slot')
    fresh = root / 'import-without-identity'
    run(['import', archives['slot'], '--name', 'fresh', '--home', fresh], 0)
    run(['slot', 'load', 'fresh', '--home', fresh], 1, 'start the game once')
    assert not (fresh / 'azahar').exists()
    cli(['slot', 'load', 'imported slot'])
    assert (save / 'main').read_bytes() == b'slot original'
    # Imported slots restore to this home's identity, even with a missing save.
    shutil.rmtree(save)
    cli(['slot', 'load', 'imported current'])
    assert (save / 'main').read_bytes() == b'must survive failed load'
    before = sorted(p.name for p in slots.iterdir())
    before_save = (save / 'main').read_bytes()
    with zipfile.ZipFile(archives['slot']) as z:
        manifest = z.read('manifest.txt')
    def bad_archive(label, entries, message):
        archive = root / ('bad-' + label + '.zip')
        with zipfile.ZipFile(archive, 'w') as z:
            for path, contents in entries:
                z.writestr(path, contents)
        cli(['import', archive, '--name', 'refused'], 1, message)
        assert sorted(p.name for p in slots.iterdir()) == before
        assert (save / 'main').read_bytes() == before_save
    bad_archive('moon', [('manifest.txt', manifest.replace(f'{SUN:016X}'.encode(), f'{MOON:016X}'.encode())),
                         ('data/main', b'wrong game')], 'title id')
    bad_archive('missing-title', [('manifest.txt', manifest.replace(f'title_id={SUN:016X}\n'.encode(), b'')),
                                  ('data/main', b'unknown')], 'title id')
    for i, path in enumerate(('../escape', 'data/../../escape', '/absolute', 'data/..\\escape',
                              'C:/escape', 'data/main:stream', 'data/NUL', 'data/main.', 'data/main ')):
        bad_archive(str(i), [('manifest.txt', manifest), ('data/main', b'save'), (path, b'escape')], 'Unsafe zip path')
    assert not (root / 'escape').exists() and not (home / 'escape').exists()
    bad_archive('case-alias', [('manifest.txt', manifest), ('data/main', b'a'), ('data/MAIN', b'b')], 'Duplicate')
    link = zipfile.ZipInfo('data/link')
    link.create_system = 3
    link.external_attr = 0o120777 << 16
    bad_archive('symlink', [('manifest.txt', manifest), (link, b'../../escape')], 'linked zip')
    bad_archive('incomplete', [('manifest.txt', manifest.replace(b'launcher_version=', b'missing_version=')),
                               ('data/main', b'a')], 'Incomplete save manifest')
    bad_archive('directory-conflict', [('manifest.txt', manifest), ('data/file', b'a'),
                                      ('data/file/nested', b'b')], '')
    bad_archive('no-manifest', [('data/main', b'raw')], 'manifest')
    bad_archive('empty', [('manifest.txt', manifest)], 'no save data')
    bad_archive('unexpected', [('manifest.txt', manifest), ('source.txt', b'unsafe'), ('data/main', b'a')], 'only manifest')
    raw = root / 'raw-main'
    raw.write_bytes(b'not an archive')
    cli(['import', raw, '--name', 'raw'], 1, 'Invalid save zip')
    # Per-game slots and imports are isolated; identical names are allowed across games.
    (home / 'usum-onescreen-app.ini').write_text(f'title_id={MOON:016X}\n')
    assert 'Champion' not in cli(['slot', 'list'])
    cli(['slot', 'load', 'Champion'], 1, 'Slot not found')
    cli(['import', archives['slot'], '--name', 'Champion'], 1, 'title id')
    moon_save = save.parent.parent / '001b5100/data'
    moon_save.mkdir(parents=True)
    (moon_save / 'main').write_bytes(b'moon save')
    cli(['slot', 'save', 'Champion'])
    assert (slot / 'data/main').read_bytes() == b'slot original'
    assert extdata.read_bytes() == b'not save data'
    cli(['slot', 'delete', 'Champion'])
    assert slot.exists()


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
        # A source-only build may omit the optional embedded plugin.
        release_plugin = PLAYER.parent / 'usum-onescreen.3gx'
        adjacent = app / 'usum-onescreen.3gx'
        if not release_plugin.exists():
            run(['setup', '--home', ziphome, '--rom', archive, '--azahar', fallback], 1, 'plugin is missing')
            adjacent.write_bytes(b'synthetic adjacent plugin')
        run(['setup', '--home', ziphome, '--rom', archive, '--azahar', fallback],
            0, 'Ultra Sun update v1.2', 'Ready to play')
        assert f'rom={extracted / "game.3ds"}' in (ziphome / 'usum-onescreen-app.ini').read_text()
        # The copied launcher has no adjacent plugin: require actual embedded bytes.
        embedded = ziphome / 'azahar/user/sdmc/luma/plugins/00040000001B5000/usum-onescreen.3gx'
        assert embedded.read_bytes() == (release_plugin if release_plugin.exists() else adjacent).read_bytes()
        assert not list(ziphome.glob('plugin-*.tmp'))
        # Adjacent and explicit plugin sources override the embedded one.
        adjacent = app / 'usum-onescreen.3gx'
        adjacent.write_bytes(b'adjacent marker')
        run(['setup', '--home', ziphome, '--rom', games[SUN]], 0, 'Ready to play')
        assert embedded.read_bytes() == adjacent.read_bytes()
        run(['setup', '--home', ziphome, '--rom', games[SUN], '--plugin', plugin], 0, 'Ready to play')
        assert embedded.read_bytes() == plugin.read_bytes()
        if release_plugin.exists():
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
        # Portable saves: discovery, no-save skip, retention, restoration and isolation.
        options = root / 'options'
        installed_update(options)
        run(['setup', '--home', options, '--rom', games[SUN], '--azahar', fallback,
             '--plugin', plugin], 0, 'Ready to play')
        run(['backup', '--home', options], 0, 'No save yet')
        assert not (options / 'backups').exists()
        save = options / 'azahar/user/sdmc/Nintendo 3DS/custom-id0/custom-id1/title/00040000/001b5000/data'
        save.mkdir(parents=True)
        (save / 'nested').mkdir()
        (save / 'nested/extra').write_bytes(b'synthetic nested save')
        for number in range(12):
            (save / 'main').write_bytes(f'synthetic save {number}'.encode())
            run(['backup', '--home', options], 0)
        backups = options / f'backups/{SUN:016X}'
        names = sorted(p.name for p in backups.iterdir())
        assert len(names) == 10 and len(set(names)) == 10
        run(['backups', '--home', options], 0, names[0], 'bytes')
        oldest = (backups / names[0] / 'data/main').read_bytes()
        (save / 'obsolete').write_bytes(b'remove on restore')
        run(['restore', '--home', options, names[0]], 0, 'Save restored')
        assert (save / 'main').read_bytes() == oldest
        assert not (save / 'obsolete').exists()
        newest = sorted(backups.iterdir())[-1]
        assert (newest / 'data/main').read_bytes() == b'synthetic save 11'
        assert len(list(backups.iterdir())) == 10
        run(['restore', '--home', options, '../escape'], 1, 'backup name')
        metadata = newest / 'source.txt'
        original = metadata.read_text()
        metadata.write_text(r'..\outside')
        run(['restore', '--home', options, newest.name], 1, 'Invalid backup save path')
        metadata.write_text(original)
        # Missing current save is recovered using the recorded portable relative path.
        shutil.rmtree(save)
        run(['restore', '--home', options, newest.name], 0)
        assert (save / 'main').read_bytes() == b'synthetic save 11'
        second = options / 'azahar/user/sdmc/Nintendo 3DS/other/id/title/00040000/001b5000/data'
        second.mkdir(parents=True)
        run(['backup', '--home', options], 1, 'Multiple save folders')
        second.rmdir()

        op_plugin, op_qt = widescreen_files(options)
        # Keep does not touch Qt config, including existing default flags.
        op_qt.write_text('[Renderer]\nresolution_factor\\default=true\nresolution_factor=7\n')
        original = op_qt.read_bytes()
        run(['set', '--home', options, 'graphics=keep'], 0)
        assert op_qt.read_bytes() == original
        for choice, factor in [('native', 1), ('high', 3), ('ultra', 5)]:
            custom = options / 'azahar/user/config/custom/game.ini'
            custom.parent.mkdir(exist_ok=True)
            custom.write_text('[Renderer]\nresolution_factor\\default=false\nresolution_factor=2\nkeep=1\n')
            run(['set', '--home', options, f'graphics={choice}'], 0)
            config = configparser.ConfigParser()
            config.read(op_qt)
            assert config['Renderer']['resolution_factor'] == str(factor)
            assert config['Renderer'][r'resolution_factor\default'] == 'false'
            assert 'resolution_factor' not in custom.read_text() and 'keep=1' in custom.read_text()
        for bad in ('invalid', '2'):
            before = op_qt.read_bytes()
            run(['set', '--home', options, 'graphics=' + bad], 1, 'bad graphics')
            assert op_qt.read_bytes() == before
        # Post-processing is in Layout (not Renderer), and user names are bare stems.
        state = options / 'usum-onescreen-app.ini'
        shader_dir = options / 'azahar/user/shaders'
        op_qt.write_text(op_qt.read_text() + '\n[Layout]\npp_shader_name\\default=true\n'
                         'pp_shader_name=external\nanaglyph_shader_name=Dubois (builtin)\n')
        original = op_qt.read_bytes()
        run(['set', '--home', options, 'visual_quality=keep'], 0)
        assert op_qt.read_bytes() == original and not shader_dir.exists()
        source = (PLAYER / 'shaders/post-process.glsl').read_text()
        for choice, name, aa, sharpen in [('sharpen', 'usum-sharpen', 0, 1),
                                          ('aa', 'usum-aa', 1, 0),
                                          ('sharpen-aa', 'usum-sharpen-aa', 1, 1),
                                          ('off', 'None (builtin)', None, None)]:
            custom.write_text('[Layout]\npp_shader_name\\default=false\npp_shader_name=external\n'
                              'pp_shader_name\\use_global=false\nanaglyph_shader_name=leave me\n')
            run(['set', '--home', options, 'visual_quality=' + choice], 0)
            config.read(op_qt)
            assert config['Layout']['pp_shader_name'] == name
            assert config['Layout'][r'pp_shader_name\default'] == 'false'
            assert config['Layout']['anaglyph_shader_name'] == 'Dubois (builtin)'
            assert 'pp_shader_name' not in custom.read_text()
            assert 'anaglyph_shader_name=leave me' in custom.read_text()
            assert 'visual_quality=' + choice in state.read_text()
            if aa is not None:
                installed = shader_dir / (name + '.glsl')
                assert installed.read_text() == f'#define USUM_AA {aa}\n#define USUM_SHARPEN {sharpen}\n' + source
        # Refuse unsupported APIs before writing anything, including mixed CLI requests.
        gl_config = op_qt.read_text()
        for api in ('2', '0', 'unknown'):
            op_qt.write_text(gl_config.replace('[Renderer]', '[Renderer]\ngraphics_api\\default=false\ngraphics_api=' + api))
            before = (op_qt.read_bytes(), state.read_bytes(), op_plugin.read_bytes())
            run(['set', '--home', options, 'layout=rail', 'visual_quality=aa'], 1, 'requires OpenGL')
            assert before == (op_qt.read_bytes(), state.read_bytes(), op_plugin.read_bytes())
            run(['set', '--home', options, 'visual_quality=off'], 0)
            config.read(op_qt)
            assert config['Renderer']['graphics_api'] == api
            assert config['Layout']['pp_shader_name'] == 'None (builtin)'
            assert config['Layout'][r'pp_shader_name\default'] == 'false'
        op_qt.write_text(gl_config)
        # A per-title API override matters; default=true still means Windows OpenGL.
        title_config = options / f'azahar/user/config/custom/{SUN:016X}.ini'
        title_config.write_text('[Renderer]\ngraphics_api\\use_global=false\ngraphics_api=2\n')
        run(['set', '--home', options, 'visual_quality=aa'], 1, 'requires OpenGL')
        title_config.write_text('[Renderer]\ngraphics_api\\use_global=false\ngraphics_api\\default=true\ngraphics_api=2\n')
        run(['set', '--home', options, 'visual_quality=aa'], 0)
        title_config.unlink()
        # External API changes suspend sync without changing the saved selection or API.
        gl_config = op_qt.read_text()
        op_qt.write_text(gl_config.replace('[Renderer]', '[Renderer]\ngraphics_api\\default=false\ngraphics_api=2'))
        before = op_qt.read_bytes()
        run(['set', '--home', options, 'glyphs=auto', 'graphics=keep'], 0)
        assert op_qt.read_bytes() == before and 'visual_quality=aa' in state.read_text()
        op_qt.write_text(gl_config)
        # Stereo modes use a different shader path and must not be silently changed.
        op_qt.write_text(gl_config.replace('[Layout]', '[Layout]\nrender_3d=3\nrender_3d\\default=false'))
        before = (op_qt.read_bytes(), state.read_bytes())
        run(['set', '--home', options, 'visual_quality=sharpen'], 1, 'stereoscopic 3D')
        assert before == (op_qt.read_bytes(), state.read_bytes())
        op_qt.write_text(gl_config)
        for bad in ('invalid', '../escape'):
            before = (op_qt.read_bytes(), state.read_bytes())
            run(['set', '--home', options, 'visual_quality=' + bad], 1, 'bad visual_quality')
            assert before == (op_qt.read_bytes(), state.read_bytes())
        run(['set', '--home', options, 'graphics=ultra', 'visual_quality=sharpen-aa'], 0)
        # Plugin comments and sections are retained; CLI values load in the window.
        op_plugin.write_text('[battle]\nlayout = rail ; keep battle comment\n[speed]\ninstant_text = on # keep speed comment\nbattle_messages = fast\nbattle_bars = fast\n[controls]\nglyphs = nintendo\n')
        run(['set', '--home', options, 'layout=bar', 'glyphs=playstation'], 0)
        assert '; keep battle comment' in op_plugin.read_text()
        assert '# keep speed comment' in op_plugin.read_text()

        script = root / 'options-test.ps1'
        script.write_text(r"""param($executable, $target)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$assembly = [Reflection.Assembly]::LoadFile($executable)
$presetType = $assembly.GetType('UsumOneScreen.ControlPreset')
$resolve = $assembly.GetType('UsumOneScreen.Win32').GetMethod('GlyphsForVendors')
foreach ($case in @(@(0x045E, 'xbox'), @(0x054C, 'playstation'), @(0x057E, 'nintendo'), @(0xFFFF, '3ds'))) {
    $actual = $resolve.Invoke($null, [object[]]@([System.UInt32[]]@($case[0]), [Enum]::Parse($presetType, 'Keyboard')))
    if ($actual -ne $case[1]) { throw "Bad vendor mapping: $actual" }
}
foreach ($case in @(@('Keyboard', '3ds'), @('Gamepad', 'xbox'), @('Custom', 'xbox'))) {
    $actual = $resolve.Invoke($null, [object[]]@([System.UInt32[]]@(), [Enum]::Parse($presetType, $case[0])))
    if ($actual -ne $case[1]) { throw "Bad no-device fallback: $actual" }
}
$launcherHome = [Activator]::CreateInstance($assembly.GetType('UsumOneScreen.Home'), [object[]]@($target))
$form = [Activator]::CreateInstance($assembly.GetType('UsumOneScreen.MainForm'), [object[]]@($launcherHome))
$flags = [Reflection.BindingFlags]'Instance, NonPublic'
function Field($name) { $form.GetType().GetField($name, $flags).GetValue($form) }
try {
    foreach ($case in @(@('graphics', 3), @('visualQuality', 4), @('battle', 1), @('speed', 1), @('glyphs', 2))) {
        if ((Field $case[0]).SelectedIndex -ne $case[1]) { throw "Wrong ini readback: $($case[0])" }
    }
    $qtType = $assembly.GetType('UsumOneScreen.QtIni')
    function SetApi($api) {
        $qt = $qtType.GetMethod('Load').Invoke($null, @($launcherHome.ConfigPath))
        $qt.Set('Renderer', 'graphics_api', $api)
        $qt.Save($launcherHome.ConfigPath)
        $form.GetType().GetMethod('UpdateVisualQuality', $flags).Invoke($form, @())
    }
    SetApi '2'
    if ((Field 'visualQuality').Enabled -or (Field 'visualQualityHint').Text -notlike '*requires OpenGL*') { throw 'Vulkan not explained/disabled' }
    SetApi '1'
    if (!(Field 'visualQuality').Enabled) { throw 'OpenGL visual quality unavailable' }
    # Exercise every UI choice, including Off and its QSettings default flag.
    foreach ($case in @(@(1, 'None (builtin)'), @(2, 'usum-sharpen'), @(3, 'usum-aa'), @(4, 'usum-sharpen-aa'))) {
        (Field 'visualQuality').SelectedIndex = $case[0]
        if (!$form.GetType().GetMethod('SavePluginSettings', $flags).Invoke($form, @())) { throw 'Visual quality save failed' }
        $qt = $qtType.GetMethod('Load').Invoke($null, @($launcherHome.ConfigPath))
        if ($qt.Get('Layout', 'pp_shader_name') -ne $case[1] -or $qt.Get('Layout', 'pp_shader_name\default') -ne 'false') { throw 'Visual quality UI wrote wrong shader/default' }
    }
    foreach ($index in @(0, 1, 2)) {
        (Field 'speed').SelectedIndex = $index
        if (!$form.GetType().GetMethod('SavePluginSettings', $flags).Invoke($form, @())) { throw 'Save failed' }
        $ini = $assembly.GetType('UsumOneScreen.PluginIni').GetMethod('Load').Invoke($null, @($launcherHome.PluginIniPath))
        $expected = @('normal','fast','instant')[$index]
        foreach ($key in @('battle_messages', 'battle_bars')) {
            if ($ini.Get($key) -ne $expected) { throw "Speed not saved: $key" }
        }
        if ($ini.Get('instant_text') -ne @('off','on','on')[$index]) { throw 'Wrong instant text' }
    }
    # An explicit default-valued icon choice must survive a missing glyph key.
    $content = [IO.File]::ReadAllText($launcherHome.PluginIniPath) -replace '(?m)^glyphs[^\r\n]*[\r\n]*', ''
    [IO.File]::WriteAllText($launcherHome.PluginIniPath, $content)
    $launcherHome.SetState('glyphs', '3ds')
    # Run the exact preparation used by Play, without any Process.Start.
    $before = [IO.Directory]::GetDirectories([IO.Path]::Combine($target, 'backups', '00040000001B5000'))
    $previous = [IO.File]::ReadAllText([IO.Path]::Combine(($before | Sort-Object | Select-Object -Last 1), 'source.txt'))
    $source = [IO.Path]::Combine($launcherHome.Sdmc, $previous, 'main')
    [IO.File]::WriteAllText($source, 'before Play synthetic save')
    # Delete immediately before Play preparation; no earlier sync may restore it.
    [IO.File]::Delete([IO.Path]::Combine($launcherHome.User, 'shaders', 'usum-sharpen-aa.glsl'))
    $assembly.GetType('UsumOneScreen.Home').GetMethod('PrepareLaunch').Invoke($launcherHome, @())
    if (![IO.File]::Exists([IO.Path]::Combine($launcherHome.User, 'shaders', 'usum-sharpen-aa.glsl'))) { throw 'Play did not restore shader' }
    $qt = $qtType.GetMethod('Load').Invoke($null, @($launcherHome.ConfigPath))
    if ($qt.Get('Layout', 'pp_shader_name') -ne 'usum-sharpen-aa' -or $qt.Get('Layout', 'pp_shader_name\default') -ne 'false') { throw 'Play did not sync shader' }
    $after = [IO.Directory]::GetDirectories([IO.Path]::Combine($target, 'backups', '00040000001B5000')) | Sort-Object
    if ([IO.File]::ReadAllText([IO.Path]::Combine($after[-1], 'data', 'main')) -ne 'before Play synthetic save') { throw 'Play did not back up' }
    $ini = $assembly.GetType('UsumOneScreen.PluginIni').GetMethod('Load').Invoke($null, @($launcherHome.PluginIniPath))
    if (!$ini.Has('glyphs') -or $ini.Get('glyphs') -ne '3ds') { throw 'Explicit 3DS reverted to Auto' }
    $launcherHome.SetState('glyphs', 'auto')
    $assembly.GetType('UsumOneScreen.Home').GetMethod('PrepareLaunch').Invoke($launcherHome, @())
    $ini = $assembly.GetType('UsumOneScreen.PluginIni').GetMethod('Load').Invoke($null, @($launcherHome.PluginIniPath))
    $detected = $assembly.GetType('UsumOneScreen.Win32').GetMethod('DetectGlyphs').Invoke($null, @($launcherHome.CurrentControls()))
    if ($ini.Get('glyphs') -ne $detected) { throw 'Auto did not refresh at Play' }
    $form.Show()
    [Windows.Forms.Application]::DoEvents()
    foreach ($scale in @(1.0, 1.5)) {
        if ($scale -gt 1) { $form.Scale([Drawing.SizeF]::new(1.5, 1.5)) }
        $form.PerformLayout()
        foreach ($name in @('graphics','visualQuality','battle','speed','glyphs','widescreen')) {
            $c = Field $name
            if (!$c.Visible -or $c.Width -lt 140 * $scale -or $c.Right -gt $c.Parent.ClientSize.Width) { throw "Clipped control at scale ${scale}: $name" }
        }
    }
} finally { $form.Dispose() }
""")
        result = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(script), str(exe), str(options)],
                                capture_output=True, encoding='utf-8', env=env, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr

        # Embedded own codes never change: only enabled markers may change.
        def cheat_sections(path):
            result, name = {}, None
            for line in path.read_text(encoding='utf-8-sig').splitlines():
                if line.startswith('['):
                    name = line[1:-1]
                    result[name] = []
                elif name and line.strip() and line != '*citra_enabled':
                    result[name].append(line)
            return result
        drafted = PLAYER / f'cheats/{SUN:016X}.txt'
        names = list(cheat_sections(drafted))
        output = run(['cheats', '--home', options], 0)
        assert output.count('off ') == len(names) == 6
        shiny, undo = [name for name in names if name.startswith('Always shiny')]
        cheat_file = options / f'azahar/user/cheats/{SUN:016X}.txt'
        for name in names:
            run(['cheats', '--home', options, 'on', name], 0, 'on  ' + name)
            assert cheat_sections(cheat_file) == cheat_sections(drafted)
        output = run(['cheats', '--home', options], 0, 'off ' + shiny, 'on  ' + undo)
        run(['cheats', '--home', options, 'on', shiny], 0, 'off ' + undo)
        run(['cheats', '--home', options, 'off', shiny], 0, 'off ' + shiny)
        run(['cheats', '--home', options, 'on', 'nonexistent'], 1, 'Unknown cheat')
        sun_bytes = cheat_file.read_bytes()
        moon_content = options / 'azahar/user/sdmc/Nintendo 3DS/0/0/title/0004000e/001b5100/content'
        moon_content.mkdir(parents=True)
        (moon_content / '0.tmd').write_bytes(tmd(MOON | 0xE00000000, 0x0820))
        run(['setup', '--home', options, '--rom', games[MOON], '--plugin', plugin], 0)
        moon_cheats = options / f'azahar/user/cheats/{MOON:016X}.txt'
        assert moon_cheats.exists() and '*citra_enabled' not in moon_cheats.read_text()
        assert cheat_file.read_bytes() == sun_bytes
        run(['backup', '--home', options], 0, 'No save yet')
        assert not (options / f'backups/{MOON:016X}').exists()
        save_manager(run, root)
    print('PASS: named slots, validation, safety backup failure/retention/isolation, ZIP round trips and title/path/link refusal; save discovery/retention/restore/isolation; graphics keep/presets; visual quality config/default flags, Off/Keep, original shader installation/restoration, API/stereo refusal, per-game overrides and UI availability/readback; cheat resources, code preservation and mutual exclusion; controller vendor/fallback mapping; options ini readback and DPI layout; battle/speed writes; validation, cross-game rejection, title state/paths, bundled source priority and immutability, installed-update checks, fallback source; offline pinned download and hash refusal; zip classification, traversal safety, reuse and state; embedded/adjacent/explicit plugin priority; widescreen choices, Auto thresholds, custom UI preservation, comment retention, invalid-value immutability, stretch sync, stale-state preservation, numeric-zero/bad-value handling and per-game cleanup.')


if __name__ == '__main__':
    main()
