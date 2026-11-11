"""Guard real submission sites; static GX budget proof, not a GPU emulator."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
source = (root / "Sources/UI/GpuQuad.cpp").read_text()
def body(name):
    start = source.index("{", source.index("static " + name))
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return re.sub(r"//[^\n]*", "", source[start:end])
hook = body("void DisplayTransferHook(")
assert hook.count("PushList(words)") == 1
assert hook.index("PushList(words)") < hook.index("OriginalFunction<void>")
assert "HiResReady() || FilterReady()" in hook
assert "Done(t0, words, restored, OverlayGxCommands)" in hook
assert "static constexpr u32 OverlayGxCommands = 1;" in (root / "Includes/UI/ColumnMapping.hpp").read_text()
assert body("void PushList(").count("s_processList(") == 1
for name in ("void WriteVertices(", "void EmitDraw(", "u32 BuildList(", "void DisplayTransferHook("):
    code = body(name)
    for forbidden in ("s_displayTransfer(", "s_processList(", "TextureCopy(", "GX_"):
        assert forbidden not in code
vertices = body("void WriteVertices(")
assert "i < ScreenFilterQuadCount(s_activeFilter)" in vertices
assert "write(Rect{q.x, q.y, q.w, q.h}, Rect{0, 0, 1, 1}, 1.f)" in vertices
assert vertices.index("s_filterBase = s_vertexQuads") < vertices.index("s_mipBase = s_vertexQuads")
assert "if (s_vertexQuads == 0) return 0;" in body("u32 BuildList(")
draw = body("void EmitDraw(")
assert "if (!reducing && quad >= s_filterBase)" in draw
assert "GPU_CONSTANT | (GPU_CONSTANT << 16)" in draw
assert "ScreenFilterQuad(s_activeFilter, quad - s_filterBase).colour" in draw
assert "GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG, 0)" in draw
assert "(GPU_ALWAYS << 4) | (7 << 8)" in draw
ready = body("bool FilterReady(")
for required in ("s_shadow", "s_restore", "s_inTarget", "!s_test.load()", "s_scratch != 0"):
    assert required in ready
assert "s_hiResEnabled" not in ready
assert "static constexpr u32 ScratchSize = OffVbo + 0x2000;" in source
print("filter GX: off adds 0; enabled shares one list (0 added with overlays, otherwise 1); no copies/allocations")

legacy = (root / "Sources/UI/LayerGpu.cpp").read_text()
assert legacy.index('if (GetScreenFilter() != ScreenFilter::Off)') < legacy.index('DT(fbLines')
assert 'return Fallback("screen filter reserves GX budget")' in legacy
print("filter GX: legacy 6-8-command presenter takes CPU fallback while a filter is selected")
