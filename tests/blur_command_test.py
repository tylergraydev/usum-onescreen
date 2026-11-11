"""Source guard for actual GX submission sites; complements the host blur plan.

This does not emulate PICA/GSP or prove runtime visual correctness.
"""
from pathlib import Path
import re

source = (Path(__file__).resolve().parent.parent / "Sources/UI/GpuQuad.cpp").read_text()


def body(name):
    start = source.index("{", source.index("static " + name))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return re.sub(r"//[^\n]*", "", source[start:end])


hook = body("void DisplayTransferHook(")
assert hook.count("PushList(words)") == 1
assert "Done(t0, words, restored, OverlayGxCommands)" in hook
push = body("void PushList(")
assert push.count("s_processList(") == 1
for name in ("void WriteVertices(", "void EmitDraw(", "u32 BuildList(", "void DisplayTransferHook("):
    code = body(name)
    assert "s_displayTransfer(" not in code
    assert "s_processList(" not in code
    assert "TextureCopy(" not in code
    assert "GX_" not in code
build = body("u32 BuildList(")
assert build.index("for (u32 level = 0; level < BlurLevels; level++)") < build.index("EmitDraw(targetPa")
assert "OffTarget + BlurLevelOffset(level)" in build
assert "BlurLevelSize(level), BlurLevelSize(level), false, level" in build
assert source.count("static constexpr u32 ScratchSize = OffVbo + 0x2000;") == 1
assert hook.count("OriginalFunction<void>") == 1  # game's original transfer, unchanged
print("blur: one actual ProcessCommandList submission; zero added transfer/copy sites")
