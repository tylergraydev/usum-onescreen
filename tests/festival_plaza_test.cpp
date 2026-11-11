#include "Screens/FestivalPlaza.hpp"
#include "festival_plaza_samples.hpp"
#include "hires_screen_smoke.hpp"
#include <cstdio>
#include <cstring>
#include <iterator>
using namespace OneScreen;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
static void Feed(FestivalPlazaScreen &s, const Sample *p, std::vector<std::string> &log)
{ for (int i=0;i<3;++i) s.Sampled(p,log); }
static void Captures()
{
    using namespace FestivalCaptures;
    const SamplePoint points[]={{40,113},{280,113},{103,203},{145,203},{187,203},{40,124},{280,124},{5,8},{5,38}};
    CHECK(FestivalPlazaSamplePoints().size()==FestivalSampleCount);
    for (unsigned i=0;i<FestivalSampleCount;++i)
        CHECK(FestivalPlazaSamplePoints()[i].x==points[i].x && FestivalPlazaSamplePoints()[i].y==points[i].y);
    CHECK(DecodeFestivalPlaza(hub)==FestivalView::Hub);
    CHECK(DecodeFestivalPlaza(walk)==FestivalView::Hub);
    CHECK(DecodeFestivalPlaza(quit)==FestivalView::Quit);
    for (const Sample *s : {battle,trade,link,list,prof2}) CHECK(DecodeFestivalPlaza(s)==FestivalView::Other);
    Sample black[FestivalSampleCount]={}; Sample white[FestivalSampleCount];
    for (auto &s:white) s={255,255,255};
    CHECK(DecodeFestivalPlaza(black)==FestivalView::Other); CHECK(DecodeFestivalPlaza(white)==FestivalView::Other);
    for (const Sample *capture : {hub,walk,quit})
    {
        const auto expected=DecodeFestivalPlaza(capture);
        for (unsigned i=0;i<FestivalSampleCount;++i)
        {
            const bool required=expected==FestivalView::Hub ? i<5 : i==0 || i==1 || i>=5;
            std::vector<Sample> s(capture,capture+FestivalSampleCount);
            s[i]={0,0,0}; CHECK((DecodeFestivalPlaza(s.data())!=expected)==required);
            if (!required) continue;
            for (int delta : {-20,20})
            {
                s.assign(capture,capture+FestivalSampleCount);
                const int v=(int)s[i].r+delta; s[i].r=(u8)(v<0?0:v>255?255:v);
                CHECK(DecodeFestivalPlaza(s.data())==expected);
            }
            s.assign(capture,capture+FestivalSampleCount); s[i].r=(u8)(capture[i].r-21);
            CHECK(DecodeFestivalPlaza(s.data())!=expected);
        }
    }
    CHECK(IsFestivalPlaza("FieldRo",true)); CHECK(!IsFestivalPlaza("FieldRo",false));
    CHECK(!IsFestivalPlaza("JoinFestaProfile",true));
    FestivalPlazaProbe p;
    CHECK(p.Update(FestivalView::Quit)==0); CHECK(p.Update(FestivalView::Other)==0);
    CHECK(p.Update(FestivalView::Quit)==0); CHECK(p.Update(FestivalView::Other)==0);
    CHECK(p.Update(FestivalView::Quit)==WorldFestivalQuit);
    for(int i=0;i<6;++i) CHECK(p.Update(FestivalView::Other)==WorldFestivalQuit);
    CHECK(p.Update(FestivalView::Other)==0);
    for(int i=0;i<3;++i) p.Update(FestivalView::Quit);
    CHECK(p.Update(FestivalView::Hub)==0); // known hub invalidates held quit immediately
    CHECK(p.Update(FestivalView::Hub)==0); CHECK(p.Update(FestivalView::Hub)==WorldFestivalHub);
    CHECK(p.Update(FestivalView::Other)==0); // hub never holds stale keys
}
static void Keys()
{
    std::vector<std::string> log; FestivalPlazaScreen screen; screen.Begin(log);
    CHECK(!screen.Ready()); CHECK(screen.CaptureKeys()==0);
    screen.Sampled(FestivalCaptures::hub,log); screen.Sampled(FestivalCaptures::hub,log); CHECK(!screen.Ready());
    screen.Sampled(FestivalCaptures::hub,log); CHECK(screen.Ready());
    CHECK(screen.CaptureKeys()==(ScreenKey::L|ScreenKey::R|ScreenKey::Start|ScreenKey::B));
    for(u32 key:{ScreenKey::A,ScreenKey::DPad,ScreenKey::CPad,ScreenKey::L|ScreenKey::R}) CHECK(!screen.Key(key,log));
    const u32 keys[]={ScreenKey::L,ScreenKey::R,ScreenKey::Start,ScreenKey::B};
    const char *buttons[]={"festival_plaza.pokemon","festival_plaza.bag","festival_plaza.profile","festival_plaza.back"};
    const int xy[][2]={{187,213},{229,213},{145,213},{16,223}};
    for(int i=0;i<4;++i)
    {
        const auto *def=screen.Key(keys[i],log); CHECK(def); if(!def) continue;
        CHECK(CheckAction(*def).empty()); CHECK(!std::strcmp(def->steps[0].button,buttons[i]));
        const auto *b=FindLayoutButton(buttons[i]); CHECK(b && b->tapX==xy[i][0] && b->tapY==xy[i][1]);
        CHECK(!screen.Key(keys[i],log)); CHECK(screen.CaptureKeys()==0);
        screen.ActionDone(log); CHECK(!screen.Ready()); Feed(screen,FestivalCaptures::walk,log);
    }
    for(const Sample *s:{FestivalCaptures::quit,FestivalCaptures::battle,FestivalCaptures::trade,FestivalCaptures::link,FestivalCaptures::list,FestivalCaptures::prof2})
    {
        screen.Sampled(s,log); CHECK(!screen.Ready()); CHECK(screen.CaptureKeys()==0); CHECK(!screen.Key(ScreenKey::B,log));
        Feed(screen,FestivalCaptures::hub,log); CHECK(screen.Ready());
    }
}
enum class Scenario {Good,Flicker,NoPrompt,Changed,LostCro,LostScene,Stuck,StillPlaza,RouteMenu,GonePrompt,Cancel};
static void Sequence(Scenario scenario)
{
    ActionWorld w; w.scene="FieldRo"; w.flags=WorldFestivalActive|WorldFestivalHub;
    ActionRunner runner; std::vector<std::string> log; FestivalPlazaProbe probe;
    for(int i=0;i<3;++i) probe.Update(FestivalView::Hub);
    CHECK(CheckAction(FestivalPlazaLeaveAction()).empty()); CHECK(runner.Start(FestivalPlazaLeaveAction(),w,log));
    const Sample *bottom=FestivalCaptures::hub;
    bool touching=false, active=true; int back=0,quit=0; std::vector<int> edges;
    for(int frame=0;runner.Running() && frame<1600;++frame)
    {
        auto view=DecodeFestivalPlaza(bottom);
        if(scenario==Scenario::Flicker && bottom==FestivalCaptures::quit && frame%2) view=FestivalView::Other;
        w.flags=active ? WorldFestivalActive|probe.Update(view) : scenario==Scenario::RouteMenu ? 0 : WorldFestivalField;
        if(frame==18)
        {
            if(scenario==Scenario::Changed) { bottom=FestivalCaptures::hub; w.flags=WorldFestivalActive|probe.Update(FestivalView::Hub); }
            if(scenario==Scenario::LostCro) { active=false; w.flags=0; }
            if(scenario==Scenario::LostScene) w.scene="JoinFestaProfile";
            if(scenario==Scenario::GonePrompt) { bottom=FestivalCaptures::list; w.flags=WorldFestivalActive|probe.Update(FestivalView::Other); }
            if(scenario==Scenario::Cancel) runner.Cancel(log);
        }
        auto out=runner.Update(w,log); CHECK(out.keys==0);
        if(out.touching && !touching)
        {
            if(out.x==16 && out.y==223) {++back;edges.push_back(0);}
            else if(out.x==160 && out.y==118) {++quit;edges.push_back(1);}
            else CHECK(false);
        }
        if(!out.touching && touching)
        {
            if(!quit && scenario!=Scenario::NoPrompt) bottom=FestivalCaptures::quit;
            if(quit && scenario!=Scenario::Stuck && scenario!=Scenario::StillPlaza) active=false;
            if(quit && scenario==Scenario::StillPlaza) bottom=FestivalCaptures::hub;
        }
        touching=out.touching;
    }
    const bool good=scenario==Scenario::Good || scenario==Scenario::Flicker;
    CHECK(!runner.Running()); CHECK(runner.Succeeded()==good); CHECK(back==1); CHECK(quit<=1);
    if(good) {CHECK(quit==1); CHECK(edges==std::vector<int>({0,1})); CHECK(w.scene=="FieldRo");}
    if(scenario==Scenario::NoPrompt || scenario==Scenario::Changed || scenario==Scenario::LostCro || scenario==Scenario::LostScene || scenario==Scenario::Cancel || scenario==Scenario::GonePrompt) CHECK(quit==0);
}
static void Preview(float scale,const char *dir)
{
    FestivalPlazaScreen screen; const int width=(int)(400*scale),height=(int)(240*scale);
    std::vector<u8> pixels((size_t)width*height*3,130);
    UI::Surface surface{pixels.data(),3,width*3,UI::PixelFormat::BGR8,width,height};
    UI::Canvas canvas(surface,0,0,scale); screen.Draw(canvas);
    for(int i=0;i<width*(int)(216*scale)*3;++i) CHECK(pixels[i]==130);
    CHECK(HiResScreenSmoke::Run(FestivalPlazaArea,ScreenStyle::Radius,[&](UI::Canvas &c){screen.Draw(c);}));
    using namespace UI::Theme;
    CHECK(5+PromptWidth(Button::L,"Party")+3+PromptWidth(Button::R,"Bag")+3+PromptWidth(Button::Start,"Profile")+3+PromptWidth(Button::B,"Leave")<=FestivalPlazaArea.w);
    if(!dir) return;
    const auto path=std::string(dir)+(scale==1?"/festival-plaza-1x.ppm":"/festival-plaza-2.5x.ppm");
    FILE *f=std::fopen(path.c_str(),"wb"); CHECK(f); if(!f) return;
    std::fprintf(f,"P6\n%d %d\n255\n",width,height);
    for(size_t i=0;i<pixels.size();i+=3) {const u8 rgb[]={pixels[i+2],pixels[i+1],pixels[i]}; std::fwrite(rgb,1,3,f);}
    std::fclose(f);
}
int main(int argc,char **argv)
{
    Captures(); Keys();
    for(auto s:{Scenario::Good,Scenario::Flicker,Scenario::NoPrompt,Scenario::Changed,Scenario::LostCro,Scenario::LostScene,Scenario::Stuck,Scenario::StillPlaza,Scenario::RouteMenu,Scenario::GonePrompt,Scenario::Cancel}) Sequence(s);
    ActionRunner runner; ActionWorld world; std::vector<std::string> log; world.scene="FieldRo";
    for(u32 flags:{0u,WorldFestivalActive,WorldFestivalHub,WorldFestivalActive|WorldFestivalQuit})
    {world.flags=flags; CHECK(!runner.Start(FestivalPlazaLeaveAction(),world,log));}
    Preview(1,argc>1?argv[1]:nullptr); Preview(2.5f,argc>1?argv[1]:nullptr);
    std::printf("festival_plaza_test: %s (%d failures)\n",failures?"FAILED":"passed",failures); return failures?1:0;
}
