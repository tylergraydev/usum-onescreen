#include "Widescreen.hpp"
#include "Settings.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace OneScreen;

int main()
{
    float scale = 1.1f;
    for (const char *s : {"0.8", "1", "1.5", " 1.25 ", "1e0"})
        assert(ParseFieldFov(s, scale));
    for (const char *s : {"", "off", "0", "-1", "0.7999", "1.5001", "nan", "inf", "1e99", "1e-99", "1.2x", "1:1"})
    {
        scale = 1.1f;
        assert(!ParseFieldFov(s, scale) && scale == 1.1f);
        std::vector<std::string> warnings;
        assert(ParseSettings(std::string("[display]\nfield_fov=") + s, warnings).fieldFov == 1.0f);
        assert(warnings.size() == 1);
    }
    std::vector<std::string> warnings;
    assert(ParseSettings("[display]\nfield_fov=1.5", warnings).fieldFov == 1.5f && warnings.empty());
    assert(ParseSettings("[display]\nfield_fov=0.8", warnings).fieldFov == 0.8f && warnings.empty());
    assert(ParseSettings("[display]\nfield_fov=1.2\nfield_fov=bad", warnings).fieldFov == 1.2f);
    warnings.clear();
    assert(ParseSettings(DefaultSettingsText(), warnings).fieldFov == 1.0f && warnings.empty());
    assert(DescribeSettings(Settings{}).find("field_fov 1.000000") != std::string::npos);

    PcWorld w;
    w.scene = "FieldRo"; w.fieldState = 0;
    assert(FieldFovAllowed(w, false));
    assert(!FieldFovAllowed(w, true));
    for (const char *scene : {"", "Battle", "SequencePlayer", "Kawaigari", "TownMap", "Zukan", "TitleMenu", "main:00647FCC"})
    { w.scene = scene; assert(!FieldFovAllowed(w, false)); }
    w.scene = "FieldRo";
    for (int state : {-1, 4, 0x11, 0x1e, 1})
    { w.fieldState = state; assert(!FieldFovAllowed(w, false)); }
    w.fieldState = 0;
    for (bool PcWorld::*flag : {&PcWorld::eventRunning, &PcWorld::festivalPlaza, &PcWorld::actionRunning, &PcWorld::pending})
    { w.*flag = true; assert(!FieldFovAllowed(w, false)); w.*flag = false; }

    constexpr float stock = 400.0f / 240.0f;
    for (float aspect : {0.0f, stock, 16.0f/9.0f, 21.0f/9.0f})
    {
        for (float factor : {0.8f, 1.0f, 1.2f, 1.5f})
        {
            const float y = FieldFovY(0.8f, factor, stock);
            const float a = PerspectiveAspect(stock, aspect);
            assert(std::fabs(y - 0.8f * factor) < 0.000001f);
            assert(a == (aspect == 0 ? stock : aspect));
            // Perspective scales: y=cot(fovy/2), x=y/aspect; both use the
            // same scaled angle and the independently rewritten aspect.
            const float sy = 1.0f / std::tan(y / 2);
            const float sx = sy / a;
            assert(std::fabs(sy / sx - a) < 0.000001f);
        }
    }
    assert(FieldFovY(0.8f, 1.5f, 4.0f/3.0f) == 0.8f);
    assert(PerspectiveAspect(4.0f/3.0f, 21.0f/9.0f) == 4.0f/3.0f);
    assert(!TopPerspective(stock + 0.002f));
    assert(TopPerspective(stock + 0.0009f));
    assert(FieldFovY(2.5f, 1.5f, stock) == 2.5f);
    assert(FieldFovY(-1, 1.5f, stock) == -1);
    assert(FieldFovY(0, 1.5f, stock) == 0);
    assert(FieldFovY(0.8f, std::numeric_limits<float>::infinity(), stock) == 0.8f);
    assert(FieldFovY(0.8f, 1.6f, stock) == 0.8f);
    std::puts("field FOV: parse/range, scene gating and projection math passed");
}
