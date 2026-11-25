"""Guard Panel's GX budget; optionally generate the host compilation header.

python tests/panel_command_test.py [--generate tests/build/panel_host.hpp]
The generated header uses the actual Panel.cpp and Framebuffer.hpp bodies with
device includes replaced by host stubs. panel_command_test.cpp asserts stock
totals (full 8, half 5, two-thirds 3-4) and zero panel GX at widescreen.
"""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent


def strip_includes(source):
    return re.sub(r'^\s*#(?:include[^\n]*|pragma once)\n', '', source, flags=re.M)


def balanced_end(code, start, opening='{', closing='}'):
    depth = 0
    for end in range(start, len(code)):
        if code[end] == opening:
            depth += 1
        elif code[end] == closing:
            depth -= 1
            if depth == 0:
                return end + 1
    raise AssertionError('unbalanced source')


def reject_gx_loops(source):
    code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', source, flags=re.S)
    definition = r'^[ \t]*(?:static\s+)?[\w:]+(?:\s*[&*])?\s+(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?\{'
    functions = {}
    for match in re.finditer(definition, code, re.M):
        begin = match.end() - 1
        functions[match[1]] = code[begin:balanced_end(code, begin)]
    calls = lambda text: set(re.findall(r'\b(\w+)\s*\(', text))
    gx = {'s_textureCopy', 's_displayTransfer'}
    while True:
        expanded = gx | {name for name, body in functions.items() if calls(body) & gx}
        if expanded == gx:
            break
        gx = expanded
    for name, body in functions.items():
        for loop in re.finditer(r'\b(for|while|do)\b', body):
            start = loop.end()
            if loop[1] != 'do':
                start = body.index('(', start)
                start = balanced_end(body, start, '(', ')')
            while body[start].isspace():
                start += 1
            end = balanced_end(body, start) if body[start] == '{' else body.index(';', start) + 1
            # Include loop conditions as well as bodies; follow helper calls to
            # catch CopyRuns -> s_textureCopy and Transfer -> s_displayTransfer.
            assert not calls(body[loop.start():end]) & gx, f'GX submission inside loop in {name}'
    return functions


def guard(source):
    functions = reject_gx_loops(source)
    required = {'CopyTopColumns', 'GpuFull', 'GpuHalf', 'GpuTwoThirds', 'DrawPanel', 'PanelCopyBottomRect'}
    assert required <= functions.keys(), 'guard did not recognize all panel entry points'
    # Only these low-level helpers may submit directly. The compiled host test
    # counts all transitive submissions, not just final framebuffer copies.
    allowed = {'Transfer', 'ToGpu', 'CopyRuns', 'CopyTopColumns', 'PanelCopyBottomRect'}
    for name, body in functions.items():
        if re.search(r'\bs_(?:textureCopy|displayTransfer)\s*\(', body):
            assert name in allowed, f'unexpected GX submission site in {name}'
    print('Panel GX source guard: PASS (no GX loops, including through helpers)')


def test_loop_guard():
    # The two original squeezed loop shapes must fail even if their dispatch
    # would currently be unreachable. Ordinary CPU loops must still pass.
    helpers = """
    static void CopyRuns() { s_textureCopy(); }
    static void Transfer() { s_displayTransfer(); }
    """
    for original_loop in (
        'static void CopyTopColumns() { for (u32 dx = 0; dx < TopWidth; dx++) { CopyRuns(); } }',
        'static void GpuFull() { for (u32 dx = 0; dx < TopWidth; dx++) { if (visible) Transfer(); } }',
        'static void CopyTopColumns() { for (int x = 0; x < 3; x++) s_textureCopy(); }',
    ):
        try:
            reject_gx_loops(helpers + original_loop)
        except AssertionError as error:
            assert 'GX submission inside loop' in str(error)
        else:
            raise AssertionError('guard accepted per-column GX loop')
    reject_gx_loops(helpers + 'static void CpuCopy() { for (int x = 0; x < 400; x++) EncodePixel(); }')
    print('Panel GX loop-guard regression checks: PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate', type=Path)
    args = parser.parse_args()
    source = (ROOT / 'Sources/Panel.cpp').read_text(encoding='utf-8')
    guard(source)
    test_loop_guard()
    if args.generate:
        args.generate.parent.mkdir(parents=True, exist_ok=True)
        framebuffer = (ROOT / 'Includes/Framebuffer.hpp').read_text(encoding='utf-8')
        args.generate.write_text(
            '#include "panel_host_stubs.hpp"\n' + strip_includes(framebuffer)
            + '\n' + strip_includes(source), encoding='utf-8')
