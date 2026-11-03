#pragma once

// Native Trainer Passport (scene TrainerPass, X menu → Trainer Passport).
// Read-only. The game shows the trainer card on the top screen and a book of
// 15 stamp pages on the bottom screen (◀ ▶ or L / R turn the page; A there
// is Portrait Photo, which the native screen keeps from the game). The
// native screen shows the card's main values, read from memory, and the
// stamp page the game is on (it remembers the page between visits; read
// from memory every frame): earned or not from the page's pixels, the
// stamp's name and text from the passport's own message file. See
// docs/NATIVE-SCREENS.md.

#include "Screens/NativeScreen.hpp"

namespace OneScreen
{
    const int PassportPages = 15;

    // What the glue read from memory when the scene opened (docs/OFFSETS.md).
    struct PassportData
    {
        bool found = false;             // the passport's work object was found
        std::string name;               // MyStatus
        bool idValid = false;
        u32 id = 0;                     // MyStatus +8 (TID | SID << 16)
        bool moneyValid = false;
        u32 money = 0;                  // Misc +8
        bool timeValid = false;
        u32 hours = 0, minutes = 0;     // the save's play time
        // Labels from the passport's message file (lines 0, 2, 7, 13);
        // empty: English defaults.
        std::string labelName, labelId, labelMoney, labelTime;
        // Stamp n's name and text (lines 22 + 2n, 23 + 2n); "\n" kept.
        std::string stampName[PassportPages], stampText[PassportPages];
    };

    // The six-digit ID No. the card shows: (TID | SID << 16) mod 1000000.
    std::string FormatTrainerId(u32 raw);
    std::string FormatMoney(u32 money);         // "$27,068"
    std::string FormatPlayTime(u32 hours, u32 minutes);    // "14:44"

    // What the bottom screen shows.
    struct PassportShown
    {
        bool valid = false;     // a passport page (its paper) is on screen
        bool earned = false;    // the page has a stamp (dark emblem strokes)
    };

    const std::vector<SamplePoint> &PassportSamplePoints(void);
    PassportShown DecodePassport(const Sample *samples);

    // All of the top screen but its edges: the game's card, down to its
    // "A Portrait Photo" hint (y 222-238), is covered.
    static constexpr UI::Rect PassportArea = { 12, 4, 376, 232 };

    class PassportScreen : public NativeScreen
    {
    public:
        const char *Name(void) const override { return "passport"; }
        const char *Scene(void) const override { return "TrainerPass"; }
        UI::Rect Area(void) const override { return PassportArea; }
        u32 CaptureKeys(void) const override;
        const std::vector<SamplePoint> &Samples(void) const override { return PassportSamplePoints(); }

        // The glue reads the data until it is found: for the first frames
        // of the scene the passport's objects aren't filled in yet.
        void SetData(const PassportData &data, std::vector<std::string> &log);
        bool HasData(void) const { return m_data.found; }
        // The game's page, 0-based (-1: not readable), every frame.
        void SetPage(int page) { m_page = page; }

        void Begin(std::vector<std::string> &log) override;
        void Sampled(const Sample *samples, std::vector<std::string> &log) override;
        bool Ready(void) const override { return m_shown.valid; }
        const ActionDef *Key(u32 key, std::vector<std::string> &log) override;
        void ActionDone(std::vector<std::string> &log) override;

        u32 DrawKey(void) const override;
        void Draw(UI::Canvas &canvas) const override;
        std::string Status(void) const override;

        int Page(void) const { return m_page; }     // 0-based
        const PassportShown &Shown(void) const { return m_shown; }

        static const u32 StableSamples = 3;

    private:
        PassportData m_data;
        PassportShown m_shown, m_candidate;
        u32 m_candidateCount = 0;
        int m_page = -1;
        int m_loggedPage = -2;
        bool m_loggedEarned = false;
        bool m_turning = false;
    };
}
