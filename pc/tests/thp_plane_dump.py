#!/usr/bin/env python3
"""Untile a decoded THP luma/chroma plane and report what is in it.

The THP decoder writes its output straight into GX_TF_I8 tile order (8x4
tiles), because that is what lbMthp_8001F67C hands to GXInitTexObj. This
reads such a plane back into raster order so it can be looked at.

    python3 pc/tests/thp_plane_dump.py y.bin 640 480 [--ppm out.ppm u.bin v.bin]

Diagnostic only. It reads a plane dumped from a running process; it does not
write anything into the repository, and no decoded frame belongs in git.
"""
import sys


def untile(data, w, h):
    """GX I8: 8x4 tiles, left to right, in four-row bands top to bottom."""
    out = bytearray(w * h)
    for y in range(h):
        band = (y >> 2) * (w << 2)
        row = (y & 3) * 8
        for x in range(w):
            out[y * w + x] = data[band + (x >> 3) * 32 + row + (x & 7)]
    return out


def describe(name, plane, w, h):
    hist = [0] * 256
    for b in plane:
        hist[b] += 1
    lo = next(i for i, c in enumerate(hist) if c)
    hi = next(i for i in range(255, -1, -1) if hist[i])
    mean = sum(i * c for i, c in enumerate(hist)) / len(plane)
    distinct = sum(1 for c in hist if c)
    top = sorted(range(256), key=lambda i: -hist[i])[:4]
    print(f"{name}: {w}x{h} min={lo} max={hi} mean={mean:.1f} "
          f"distinct={distinct}")
    print("   most common: " +
          ", ".join(f"{v}({hist[v] * 100.0 / len(plane):.1f}%)" for v in top))

    # Vertical structure: a real frame varies down the picture, a broken
    # decode usually does not.
    rows = [sum(plane[y * w:(y + 1) * w]) / w for y in range(h)]
    print("   row means: " + " ".join(f"{rows[y]:.0f}"
                                      for y in range(0, h, max(1, h // 12))))


def main():
    path, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    y = untile(open(path, "rb").read(), w, h)
    describe("Y", y, w, h)

    if "--ppm" in sys.argv:
        i = sys.argv.index("--ppm")
        out, up, vp = sys.argv[i + 1], sys.argv[i + 2], sys.argv[i + 3]
        cw, ch = w // 2, h // 2
        u = untile(open(up, "rb").read(), cw, ch)
        v = untile(open(vp, "rb").read(), cw, ch)
        describe("U", u, cw, ch)
        describe("V", v, cw, ch)
        px = bytearray(w * h * 3)
        for j in range(h):
            for i2 in range(w):
                yy = y[j * w + i2] - 16
                uu = u[(j // 2) * cw + i2 // 2] - 128
                vv = v[(j // 2) * cw + i2 // 2] - 128
                r = (298 * yy + 409 * vv + 128) >> 8
                g = (298 * yy - 100 * uu - 208 * vv + 128) >> 8
                b = (298 * yy + 516 * uu + 128) >> 8
                o = (j * w + i2) * 3
                px[o] = max(0, min(255, r))
                px[o + 1] = max(0, min(255, g))
                px[o + 2] = max(0, min(255, b))
        with open(out, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(px)
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
