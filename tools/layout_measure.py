"""Measure button bounds on a bottom-screen screenshot (from tools/shot.py).

Usage: py -3 -I tools/layout_measure.py PNG X,Y [X,Y ...] [--tol N]

For each seed point, flood-fills the 4-connected region whose colour is within
--tol (default 40, sum of |dR|+|dG|+|dB|) of the seed pixel's colour and prints
its bounding box as x0,y0,x1,y1 (inclusive) plus size and centre. Seed on the
button's flat background colour, not on its text. Text and icons inside the
button leave holes but do not change the box as long as the background
surrounds them. Needs Pillow.
"""
import sys
from collections import deque

from PIL import Image


def region(img, seed, tol):
    w, h = img.size
    px = img.load()
    base = px[seed]
    seen = bytearray(w * h)
    q = deque([seed])
    seen[seed[1] * w + seed[0]] = 1
    x0 = x1 = seed[0]
    y0 = y1 = seed[1]
    n = 0
    while q:
        x, y = q.popleft()
        n += 1
        x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx]:
                c = px[nx, ny]
                if sum(abs(c[i] - base[i]) for i in range(3)) <= tol:
                    seen[ny * w + nx] = 1
                    q.append((nx, ny))
    return base, (x0, y0, x1, y1), n


def main(argv):
    tol = 40
    if "--tol" in argv:
        i = argv.index("--tol")
        tol = int(argv[i + 1])
        del argv[i:i + 2]
    if len(argv) < 2:
        sys.exit(__doc__)
    img = Image.open(argv[0]).convert("RGB")
    for s in argv[1:]:
        x, y = map(int, s.split(","))
        base, (x0, y0, x1, y1), n = region(img, (x, y), tol)
        print(f"seed {x},{y} rgb{base}: rect {x0},{y0},{x1},{y1} "
              f"size {x1 - x0 + 1}x{y1 - y0 + 1} centre {(x0 + x1) // 2},{(y0 + y1) // 2} px {n}")


if __name__ == "__main__":
    main(sys.argv[1:])
