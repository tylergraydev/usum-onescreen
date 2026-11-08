#include "Shot.hpp"

#include "Framebuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static const char *RequestPath = "/usum-shot.req";
    static const char *TopPath     = "/usum-shot-top.ppm";
    static const char *BottomPath  = "/usum-shot-bottom.ppm";

    static u8 s_top[TopWidth * ScreenHeight * 3];
    static u8 s_bottom[BottomWidth * ScreenHeight * 3];

    // Set by ShotPoll when a request arrives; cleared by the OSD callback once
    // the matching screen has been copied.
    static volatile bool s_wantTop = false;
    static volatile bool s_wantBottom = false;
    static bool s_pending = false;

    bool ShotOnScreen(const Screen &screen)
    {
        if (screen.IsTop && s_wantTop)
        {
            ReadScreen(screen, TopWidth, s_top);
            s_wantTop = false;
            return true;
        }
        else if (!screen.IsTop && s_wantBottom)
        {
            ReadScreen(screen, BottomWidth, s_bottom);
            s_wantBottom = false;
            return true;
        }
        return false;
    }

    // Binary PPM (P6): a short text header, then RGB888 rows.
    static void WritePpm(const char *path, const u8 *pixels, u32 width)
    {
        File file;
        if (File::Open(file, path, File::WRITE | File::CREATE | File::TRUNCATE) != File::SUCCESS)
            return;
        const std::string header = Utils::Format("P6\n%lu %lu\n255\n", width, ScreenHeight);
        file.Write(header.c_str(), header.size());
        file.Write(pixels, width * ScreenHeight * 3);
        file.Close();
    }

    void ShotPoll(void)
    {
        static u32 frames = 0;

        if (s_pending)
        {
            if (s_wantTop || s_wantBottom)
                return;
            WritePpm(TopPath, s_top, TopWidth);
            WritePpm(BottomPath, s_bottom, BottomWidth);
            File::Remove(RequestPath);
            s_pending = false;
            return;
        }

        // Checking the SD card costs a file-system call, so only look twice a second.
        if (++frames % 30 != 0)
            return;
        if (File::Exists(RequestPath) == 1)
        {
            s_pending = true;
            s_wantBottom = true;
            s_wantTop = true;
        }
    }
}
