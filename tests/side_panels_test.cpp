#include "SidePanels.hpp"
#include "BattleWidescreen.hpp"
#include "UI/ColumnMapping.hpp"
#include "UI/Text.hpp"
#include "UI/QuadAllocator.hpp"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
using namespace OneScreen;
using namespace OneScreen::UI;

static void Preview(const char *path, float squeeze, float scale)
{
    const int width = (int)(400 * scale), height = (int)(240 * scale);
    std::vector<u8> pixels((size_t)width * height * 4, 0);
    Surface surface{pixels.data(), 4, width*4, PixelFormat::RGBA8, width, height};
    Canvas canvas(surface, 0, 0, scale);
    canvas.FillRect({0, 0, 400, 240}, Rgb(65, 87, 103));
    const auto centre = SqueezedColumns(0, 400, squeeze);
    canvas.FillRect({centre.begin, 0, centre.end-centre.begin, 240}, Rgb(95, 116, 128));
    // Synthetic game HUD extending to +/-232 in authored coordinates. The
    // existing ortho squeeze plus bounded fit must put it entirely in the centre.
    const float hudFit=SidePanelsHudFit(-232,232);
    const auto screenX = [&](float authored) { return (int)std::ceil(200.f+authored*hudFit/squeeze); };
    const Rect playerHp{screenX(-232),200,screenX(-90)-screenX(-232),32};
    const Rect foeHp{screenX(65),8,screenX(232)-screenX(65)-1,26};
    const TextStyle label{Font::Small,1,Rgb(235,240,229),Align::Left};
    canvas.FillRoundRect(playerHp,3,Rgb(24,68,105));
    DrawText(canvas,"Player HP",{playerHp.x+3,201,playerHp.w-6,10},label);
    canvas.FillRect({playerHp.x+3,215,playerHp.w-6,4},Rgb(87,205,132));
    canvas.FillRoundRect(foeHp,3,Rgb(30,75,52));
    DrawText(canvas,"Foe HP",{foeHp.x+3,9,foeHp.w-6,10},label);
    canvas.FillRect({foeHp.x+3,22,foeHp.w-6,4},Rgb(87,205,132));
    assert(playerHp.x>=centre.begin && foeHp.Right()<=centre.end);
    SidePanelView view;
    view.battle = true;
    const char *names[] = {"Torracat", "Zorua", "Pikipek", "Pichu", "Ledyba", "Buneary"};
    for (int i=0; i<6; ++i)
    {
        auto &row = view.rows[i];
        std::snprintf(row.name, sizeof(row.name), "%s", names[i]);
        row.present = true; row.maxHp = 100; row.hp = (unsigned short)(100-i*20);
        row.statusKnown = i != 4; row.status = i;
    }
    view.foe = view.rows[2];
    std::strcpy(view.foe.name,"Rattata"); view.foe.level=7;
    const auto area = SidePanelArea(squeeze);
    const auto right = SidePanelArea(squeeze,true);
    const auto before = pixels;
    DrawSidePanel(canvas,area,view);
    DrawSidePanel(canvas,right,view,true);
    // Both full-height margins must be covered; the centre remains untouched.
    for (int y=0; y<height; ++y)
        for (int x=(int)std::ceil(area.Right()*scale); x<(int)std::floor(right.x*scale); ++x)
        {
            assert(std::memcmp(pixels.data()+(y*width+x)*4, before.data()+(y*width+x)*4,4)==0);
        }
    for (const auto margin : {area,right})
    {
        if(scale==2.5f)
        {
            const int pw=(int)std::lround(margin.w*scale), ph=600;
            std::vector<u8> staging((size_t)pw*ph*4,0);
            Surface target{staging.data(),4,pw*4,PixelFormat::RGBA8,pw,ph};
            target.transparent=true;
            Canvas physical(target);
            physical.FillRect({0,0,pw,ph},Rgb(20,29,32));
            Canvas logical(target,margin.x,0,scale);
            DrawSidePanel(logical,margin,view,margin.x==right.x);
            for(size_t offset=0;offset<staging.size();offset+=4) assert(staging[offset]==255);
        }
        // Bottom corners prove these are full-height opaque backgrounds.
        for(int y : {0,height-1})
        {
            u8 r,g,b;
            ReadPixel(pixels.data()+(y*width+(int)((margin.x+margin.w/2.f)*scale))*4,PixelFormat::RGBA8,r,g,b);
            assert(r==20 && g==29 && b==32);
        }
        // 1x fallback caches just the first 96 rows; all text must fit there.
        if(scale==1.f)
        {
            const auto small=SidePanelContent(margin);
            std::vector<u8> cache((size_t)small.w*small.h*4);
            Surface cached{cache.data(),4,small.w*4,PixelFormat::RGBA8,small.w,small.h};
            Canvas local(cached,small.x,0);
            DrawSidePanel(local,small,view,margin.x==right.x);
            for(int y=0;y<small.h;++y)
                assert(std::memcmp(cache.data()+y*small.w*4,pixels.data()+(y*width+small.x)*4,small.w*4)==0);
        }
    }
    if(scale==1.f && area.w>=32)
    {
        // PAR is a visible glyph at the row's right edge; healthy has no OK label.
        const int glyphX=SidePanelContent(area).Right()-7;
        bool statusPixel=false;
        for(int y=29;y<38;++y) for(int x=glyphX;x<glyphX+6;++x)
        {
            u8 r,g,b; ReadPixel(pixels.data()+(y*width+x)*4,PixelFormat::RGBA8,r,g,b);
            statusPixel |= r==232 && g==194 && b==72;
        }
        assert(statusPixel);
    }
    if(area.w>=32)
    {
        // Removed debug line: the right panel below its level is solid background.
        for(int y=(int)(45*scale);y<(int)(60*scale);++y)
            for(int x=(int)std::ceil(right.x*scale);x<(int)std::floor(right.Right()*scale);++x)
            {
                u8 r,g,b; ReadPixel(pixels.data()+(y*width+x)*4,PixelFormat::RGBA8,r,g,b);
                assert(r==20 && g==29 && b==32);
            }
        // The foe row contains both its HP green and its SLP status color.
        bool hpPixel=false,statusPixel=false,levelPixel=false;
        for(int y=(int)(18*scale);y<(int)(41*scale);++y)
            for(int x=(int)std::ceil(right.x*scale);x<(int)std::floor(right.Right()*scale);++x)
            {
                u8 r,g,b; ReadPixel(pixels.data()+(y*width+x)*4,PixelFormat::RGBA8,r,g,b);
                hpPixel |= r==87 && g==205 && b==132;
                statusPixel |= r>g+15 && b>g+35;
                levelPixel |= y>=(int)(32*scale) && r>120 && g>120 && b>120
                    && std::max({r,g,b})-std::min({r,g,b})<30;
            }
        assert(hpPixel && statusPixel && levelPixel);
    }
    std::ofstream out(path, std::ios::binary);
    assert(out.good());
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y=0;y<height;++y) for(int x=0;x<width;++x)
    {
        u8 r,g,b; ReadPixel(pixels.data()+(y*width+x)*4,PixelFormat::RGBA8,r,g,b);
        out.put((char)r);out.put((char)g);out.put((char)b);
    }
}

int main(int argc, char **argv)
{
    // Match the actual 21:9 row widths, reserving HP and a condition glyph.
    assert(SidePanelName("Torracat",37,2.5f)=="Torracat");
    assert(SidePanelName("Torracat",46,1.f)=="Torracat");
    assert(SidePanelName("Torracat",39,1.f)=="Torracat");
    assert(SidePanelName("Torracat",34,1.f)=="Torraca");
    for(const char *name : {"Pikipek","Buneary","Rattata"})
    {
        assert(SidePanelName(name,37,2.5f)==name);
        assert(SidePanelName(name,39,1.f)==name);
    }
    assert(SidePanelName("Torracat",0,2.5f).empty());
    // UTF-8 is kept or dropped as whole characters (bitmap fallback is '?').
    assert(SidePanelName("A\xC3\xA9",5,1.f)=="A");
    assert(GetSettings().sidePanels == SidePanelMode::Off);
    for (auto mode : {SidePanelMode::Off, SidePanelMode::Battle, SidePanelMode::Field, SidePanelMode::Both})
    {
        SidePanelMode parsed = SidePanelMode::Off;
        assert(ParseSidePanelMode(SidePanelModeName(mode), parsed) && parsed == mode);
        std::vector<std::string> warnings;
        const auto settings = ParseSettings(std::string("[display]\nside_panels = ")+SidePanelModeName(mode), warnings);
        assert(warnings.empty() && settings.sidePanels == mode);
        const bool battle = mode == SidePanelMode::Battle || mode == SidePanelMode::Both;
        const bool field = mode == SidePanelMode::Field || mode == SidePanelMode::Both;
        assert(SidePanelsBattleHook(1.4f));
        assert(!SidePanelsBattleHook(1.f));
        assert(SidePanelsBattleHook(.9f));
        assert(SidePanelsBattleProjection(1.4f,mode,400.f/240.f,0)==battle);
        assert(!SidePanelsBattleProjection(1.4f,mode,4.f/3.f,0));
        assert(!SidePanelsBattleProjection(1.f,mode,400.f/240.f,0));
        assert(!SidePanelsBattleProjection(1.4f,mode,400.f/240.f,1));
        assert(SidePanelsVisible(mode,1.4f,true,false,false)==battle);
        assert(SidePanelsVisible(mode,1.4f,false,true,false)==field);
        assert(!SidePanelsVisible(mode,1.4f,true,false,true));
        assert(!SidePanelsVisible(mode,1.4f,false,false,false));
    }
    // Exercise the production gate through the real hook lifetime model.
    namespace B = BattleWidescreen;
    B::Lifetime lifetime;
    constexpr uint32_t base = 0x800000;
    const auto read = [=](uint32_t address, uint32_t &word) {
        if (address == base+B::PerspectiveOffset) word=0xE92D4030;
        else if (address == base+B::PerspectiveOffset+4) word=0xE1A05001;
        else if (address == base+B::DivideOffset) word=0xEEC01AA9;
        else return false;
        return true;
    };
    unsigned installs=0, removes=0;
    const auto install = [&](uint32_t) { ++installs; return true; };
    const auto remove = [&] { ++removes; return true; };
    assert(lifetime.Update(base,SidePanelsBattleHook(1.4f),read,install,remove)==B::Event::Installed);
    assert(lifetime.Update(base,SidePanelsBattleHook(1.4f),read,install,remove)==B::Event::None);
    assert(lifetime.Update(base,SidePanelsBattleHook(1.4f),read,install,remove)==B::Event::None);
    assert(lifetime.Update(base,SidePanelsBattleHook(1.4f),read,install,remove)==B::Event::None);
    assert(installs==1 && removes==0);
    assert(lifetime.Update(base,SidePanelsBattleHook(1.f),read,install,remove)==B::Event::Removed);
    float projection[16];
    for(unsigned i=0;i<16;++i) projection[i]=(float)(i+1);
    SidePanelsScaleProjection(projection,1.4f);
    for(unsigned i=0;i<16;++i) assert(std::fabs(projection[i]-(i<4 ? (i+1)/1.4f : (float)(i+1)))<.0001f);
    // Stock clip edges map to the central ~286 pixels, with unchanged Y/Z/W.
    assert(std::fabs(200.f + 200.f/1.4f - 342.85714f)<.001f);
    assert(SidePanelsTopRoot("RootPane",0,400,240));
    assert(!SidePanelsTopRoot("RootPane",0,320,240));
    assert(!SidePanelsTopRoot("RootPane",12,400,240));
    assert(!SidePanelsTopRoot("Other",0,400,240));
    assert(SidePanelsHudFit(-200,200)==1.f);
    const float fit=SidePanelsHudFit(-232,232);
    assert(std::fabs(fit*232.f-200.f)<.0001f);
    float hud[12]={1,2,3,-232,5,6,7,8,9,10,11,12};
    SidePanelsScaleHud(hud,fit);
    assert(std::fabs(hud[3]+200.f)<.0001f);
    assert(hud[4]==5 && hud[11]==12);
    // Rebuilding from local values before each fit prevents cumulative squeeze.
    for(int frame=0;frame<100;++frame)
    {
        float fresh[12]={1,0,0,-232,0,1,0,0,0,0,1,0};
        SidePanelsScaleHud(fresh,fit);
        assert(std::fabs(fresh[3]+200.f)<.0001f);
    }
    // Real intrusive-list traversal: siblings, nested children and corrupt cycles.
    std::map<uintptr_t,uintptr_t> links = {
        {0x1014,0x2004},{0x2014,0x3004},{0x3014,0x3014},{0x4014,0x4014},
        {0x200C,0x1000},{0x300C,0x2000},{0x400C,0x1000},
        {0x2004,0x4004},{0x3004,0x2014},{0x4004,0x1014}};
    std::vector<uintptr_t> visited;
    const auto readLink = [&](uintptr_t address) { return links.at(address); };
    const auto visit = [&](uintptr_t pane) { visited.push_back(pane); };
    assert(VisitSidePanelPanes(0x1000,readLink,visit));
    assert((visited==std::vector<uintptr_t>{0x1000,0x2000,0x3000,0x4000}));
    links[0x4004]=0x2004; visited.clear();
    assert(!VisitSidePanelPanes(0x1000,readLink,visit) && visited.size()==1024);
    links[0x4004]=0x1014; links[0x400C]=0; visited.clear();
    assert(!VisitSidePanelPanes(0x1000,readLink,visit));
    // Both 21:9 full-height textures fit beside command/moves, in either order.
    for (int menuWidth : {525,565}) for(bool menuFirst : {false,true})
    {
        QuadAllocator atlas;
        if(menuFirst) assert(atlas.Allocate(0,menuWidth,505));
        assert(atlas.Allocate(2,143,600));
        assert(atlas.Allocate(1,143,600));
        if(!menuFirst) assert(atlas.Allocate(0,menuWidth,505));
    }
    SidePanelMode mode = SidePanelMode::Field;
    assert(ParseSidePanelMode(" BoTh ",mode) && mode==SidePanelMode::Both);
    assert(!ParseSidePanelMode("on",mode) && mode==SidePanelMode::Both);
    std::vector<std::string> warnings;
    assert(ParseSettings("[display]\nside_panels = invalid",warnings).sidePanels==SidePanelMode::Off && warnings.size()==1);
    warnings.clear();
    assert(ParseSettings(DefaultSettingsText(),warnings).sidePanels==SidePanelMode::Off && warnings.empty());
    assert(SidePanelArea(0).Empty() && SidePanelArea(1).Empty());
    for(float k : {16.f/15.f, 1.4f})
    {
        const auto r = SidePanelArea(k);
        const auto centre = SqueezedColumns(0,400,k);
        assert(r.x==0 && r.y==0 && r.h==240 && r.Right()==centre.begin);
        assert(r.w==(k<1.1f ? 13 : 57));
        const auto right=SidePanelArea(k,true);
        assert(right.x==centre.end && right.Right()==400 && right.h==240);
        assert(SidePanelContent(r).w<=56 && SidePanelContent(r).h==96);
        const auto gpu = SidePanelGpuDestination(r,k);
        assert(std::abs((200.f+(gpu.x-200.f)/k)-r.x)<.51f);
        assert(std::abs((200.f+(gpu.Right()-200.f)/k)-r.Right())<.51f);
    }
    assert(SidePanelsBattleOverlayAllows("battle-ui: on, capture 0; page command, cursor 0; move-learn: hidden"));
    assert(SidePanelsBattleOverlayAllows("battle-ui: on; page moves, cursor 0; move-learn: hidden"));
    assert(SidePanelsBattleOverlayAllows("battle-ui: on; page hidden; move-learn: hidden"));
    assert(!SidePanelsBattleOverlayAllows("battle-ui: on; page party, cursor 0; move-learn: hidden"));
    assert(!SidePanelsBattleOverlayAllows("battle-ui: on; page bag, cursor 0; move-learn: hidden"));
    assert(!SidePanelsBattleOverlayAllows("battle-ui: changed"));
    SidePanelView a,b;
    assert(SameSidePanelView(a,b));
    b.foe.level=8; assert(!SameSidePanelView(a,b)); b=a;
    b.rows[5].hp=1; assert(!SameSidePanelView(a,b)); b=a;
    b.rows[1].statusKnown=true; assert(!SameSidePanelView(a,b)); b=a;
    std::strcpy(b.rows[0].name,"Litten");assert(!SameSidePanelView(a,b));
    const std::string dir = argc>1 ? argv[1] : "tests/previews";
    for(float k : {16.f/15.f,1.4f}) for(float scale : {1.f,2.5f})
    {
        const std::string path=dir+"/side-panels-"+(k<1.1f ? "16x9-" : "21x9-")+(scale==1.f ? "1x.ppm" : "2.5x.ppm");
        Preview(path.c_str(),k,scale);
    }
    std::puts("side panels: projection, HUD fit/walk, margins, settings and 4 previews passed");
}
