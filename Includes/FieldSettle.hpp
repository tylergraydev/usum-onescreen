#pragma once

// After entering the field, the field state (Scene.hpp FieldState()) only
// counts once it has held one value for a while: coming back from an app
// Rotom opened (Town Map, Pokédex) it still says 4 (menu) for 30 frames or
// more before it drops to 0. Used by AutoPanel; no CTRPF dependency, so
// tests/ can build it on the PC.

namespace OneScreen
{
    struct FieldSettle
    {
        // The Town Map's 4 was measured at 30 frames from the first FieldRo
        // read (docs/OFFSETS.md "Field state"); this leaves room for slower runs.
        static const int Frames = 60;

        int left = 0;   // reads still to go before the state counts
        int last = -1;  // last state read since entering the field

        // The field was entered. The value read before (in the field the
        // player left) is forgotten, so the first read starts the count.
        void Enter(void)
        {
            left = Frames;
            last = -1;
        }

        // A state was read (>= 0). Returns true while it hasn't settled yet;
        // any change starts the count again.
        bool Read(int state)
        {
            if (left > 0)
                left = state == last ? left - 1 : Frames;
            last = state;
            return left > 0;
        }
    };
}
