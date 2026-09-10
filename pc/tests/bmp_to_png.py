#!/usr/bin/env python3
"""Convert a captured framebuffer BMP to PNG so it can be viewed inline.

pc_vulkan.c writes captures as 32-bit BGRA BMPs with a negative height (top
down), because Win32 I/O is the only file API safe to use alongside the game's
own MSL stdio. Nothing in the repository reads PNG, so this exists purely to
look at a capture during development.

    python3 pc/tests/bmp_to_png.py build/menu-smoke.bmp build/menu-smoke.png

Captures are game frames and stay under build/, which is gitignored. Do not
commit either the BMP or the PNG.
"""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()

    if data[:2] != b"BM":
        raise SystemExit("not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits != 32:
        raise SystemExit(f"expected 32bpp, got {bits}")

    top_down = height < 0
    height = abs(height)
    stride = width * 4

    rows = []
    for y in range(height):
        src_y = y if top_down else height - 1 - y
        start = offset + src_y * stride
        row = data[start:start + stride]
        # BGRA -> RGB, dropping alpha: the capture's alpha is the framebuffer's
        # own and is not meaningful as transparency here.
        out = bytearray(b"\x00")
        for x in range(0, stride, 4):
            out += bytes((row[x + 2], row[x + 1], row[x]))
        rows.append(bytes(out))

    raw = b"".join(rows)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    open(dst, "wb").write(png)

    # A quick summary, so a capture can be judged without opening it.
    lit = sum(1 for i in range(0, len(raw), 1) if raw[i])
    print(f"{width}x{height} -> {dst}")


if __name__ == "__main__":
    main()
