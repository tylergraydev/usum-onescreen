"""Build host tests directly from Makefile recipes, without make or WSL.

Windows: python tests/check_motion.py
Maintainer baseline capture (clean HEAD): add --capture-baseline.
Exact compiler invocations are printed and saved in build/motion-commands.json.
MSVC equivalent of CXXFLAGS: /std:c++17 /EHsc /I ../Includes.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

TESTS = Path(__file__).resolve().parent
ROOT = TESTS.parent
BUILD = TESTS / 'build'
BUILD.mkdir(exist_ok=True)
capture = '--capture-baseline' in sys.argv
source = ROOT
if capture:
    source = BUILD / 'motion-baseline'
    source.mkdir(exist_ok=True)
    archive = subprocess.check_output(['git', 'archive', 'HEAD', 'Includes', 'Sources', 'tests'], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(source, filter='data')

env = os.environ.copy()
vc = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC')
vc = sorted(vc.iterdir())[-1]
sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
version = sorted((sdk / 'Include').iterdir())[-1].name
compiler = vc / 'bin/Hostx64/x64/cl.exe'
env['PATH'] = str(compiler.parent) + os.pathsep + env['PATH']
env['INCLUDE'] = ';'.join(map(str, [vc / 'include'] + [sdk / 'Include' / version / p for p in ['ucrt', 'shared', 'um']]))
env['LIB'] = ';'.join(map(str, [vc / 'lib/x64'] + [sdk / 'Lib' / version / p / 'x64' for p in ['ucrt', 'um']]))

makefile = (source / 'tests/Makefile').read_text()
variables = {}
for line in makefile.splitlines():
    match = re.match(r'(\w+)\s*:?=\s*(.*)', line)
    if match:
        variables[match[1]] = match[2]

def expand(text):
    while '$(' in text:
        old = text
        text = re.sub(r'\$\(addprefix ([^,]+),([^()]*)\)',
                      lambda m: ' '.join(m[1] + s for s in m[2].split()), text)
        text = re.sub(r'\$\((\w+)\)', lambda m: variables.get(m[1], ''), text)
        if text == old:
            raise ValueError(text)
    return text

commands = []
names = ['xmenu_test', 'party_menu_test', 'bag_test']
if not capture:
    names += ['motion_test', 'settings_test', 'settings_file_test']
preview_dir = BUILD / ('motion-before' if capture else 'motion-off')
preview_dir.mkdir(exist_ok=True)
for name in names:
    recipe = re.search(r'^\$\(OUT\)/' + name + r':.*?\n\t@mkdir[^\n]*\n\t([^\n]+)', makefile, re.M)[1]
    expanded = expand(recipe)
    sources = [str((source / 'tests' / p).resolve()) for p in expanded.split() if p.endswith('.cpp')]
    out = BUILD / (('baseline-' if capture else '') + name + '.exe')
    # MSVC lacks GCC's diagnostic-only printf-format attribute.
    command = [str(compiler), '/nologo', '/std:c++17', '/EHsc', '/D_CRT_SECURE_NO_WARNINGS', '/FI' + str(TESTS / 'msvc_host.hpp'),
               '/I' + str(source / 'Includes'), '/Fo' + str(BUILD) + '\\', '/Fe' + str(out)] + sources
    commands.append(command)
    print(subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=BUILD, env=env, check=True)
    args = [str(out)]
    if name == 'party_menu_test':
        args += [str(preview_dir / 'party-preview.ppm'), str(preview_dir / 'summary-preview.ppm')]
    elif name in ('xmenu_test', 'bag_test'):
        args += [str(preview_dir / (name.replace('_test', '') + '-preview.ppm'))]
    commands.append(args)
    subprocess.run(args, cwd=BUILD, env=env, check=True)

(BUILD / 'motion-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(preview_dir.glob('*.ppm'))}
baseline = TESTS / 'motion_preview_baselines.json'
if capture:
    baseline.write_text(json.dumps(hashes, indent=2) + '\n')
    print('Captured HEAD preview SHA-256 baselines:', len(hashes))
else:
    assert hashes == json.loads(baseline.read_text()), 'motion-off previews differ from HEAD baselines'
    print('PASS: motion-off previews byte-identical to HEAD baselines:', len(hashes))
