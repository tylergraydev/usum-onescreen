"""Write an Azahar input movie (.ctm), for input without the plugin.

Usage: py -3 -I tools/movie.py OUT.ctm SECONDS:KEYS [SECONDS:KEYS ...]

Each step holds KEYS (`-` for none, else names joined by `+`: a b select
start right left up down r l x y) for SECONDS of emulated time. Play it with
`tools/instance.py start NAME --movie OUT.ctm` (Azahar's `-p`). Example, a soft
reset on the title screen: `movie.py reset.ctm 25:- 5:l+r+start 300:-`.

Format (azahar-emu/azahar 2126.2, src/core/movie.cpp): a 256-byte CTMHeader,
then 7-byte ControllerState records. Azahar reads one PadAndCircle and one
Touch record per pad update (234 per second, hle/service/hid/hid.cpp), so each
update here is that pair. Accelerometer/gyro records are only read while a
game turns those sensors on; Ultra Sun reading them would show as "Expected to
read type" errors in Azahar's log. Revision is left zero, so Azahar warns that
the movie is from another version; clock and tick base are its own defaults.
"""
import struct
import sys

TITLE = 0x00040000001B5000
UPDATES_PER_SECOND = 234
KEYS = {"a": 0, "b": 1, "select": 2, "start": 3, "right": 4, "left": 5, "up": 6, "down": 7,
        "r": 8, "l": 9, "x": 10, "y": 11}


def keys_mask(text):
    if text == "-":
        return 0
    mask = 0
    for name in text.split("+"):
        if name not in KEYS:
            sys.exit(f"unknown key '{name}' (one of {' '.join(KEYS)})")
        mask |= 1 << KEYS[name]
    return mask


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    records = bytearray()
    updates = 0
    for step in sys.argv[2:]:
        seconds, _, keys = step.partition(":")
        mask = keys_mask(keys)
        pad = struct.pack("<BHhh", 0, mask, 0, 0)
        touch = struct.pack("<BHHBx", 1, 0, 0, 0)
        count = round(float(seconds) * UPDATES_PER_SECOND)
        records += (pad + touch) * count
        updates += count

    # filetype, program_id, revision, clock_init_time (0: settings),
    # id, author, rerecord_count, input_count, timing_base_ticks (-1: generated)
    header = struct.pack("<4sQ20sQQ32sIQq", b"CTM\x1b", TITLE, bytes(20), 0, 0,
                         b"usum-onescreen tools/movie.py", 0, updates, -1)
    header += bytes(256 - len(header))
    with open(sys.argv[1], "wb") as f:
        f.write(header + records)
    print(f"{sys.argv[1]}: {updates} pad updates, {updates / UPDATES_PER_SECOND:.1f} s")


if __name__ == "__main__":
    main()
