#!/usr/bin/env python3
"""Cut a piece out of a screenshot and magnify it if wanted.

`openbf2 --screenshot` writes BMP, and to look closely at a caption or a
button one had to catch the semantics of `sips -c` every time (it cuts from
the centre). Here the coordinates are the ordinary ones: top-left corner and size.

    tools/bmp_crop.py shot.bmp out.bmp --at 0 0 --size 700 40 --zoom 3

The output format is BMP too (24-bit), so anything can read it.
"""
import argparse
import struct
import sys


def read_bmp(path):
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise SystemExit("not BMP: %s" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (24, 32):
        raise SystemExit("only 24 and 32 bits are supported, and here it is %d" % bits)
    step = bits // 8
    stride = ((width * step + 3) // 4) * 4
    rows = []
    for y in range(abs(height)):
        # A positive height means the rows go bottom to top.
        source = (abs(height) - 1 - y) if height > 0 else y
        start = offset + source * stride
        line = data[start:start + width * step]
        rows.append([tuple(line[x * step:x * step + 3]) for x in range(width)])
    return width, abs(height), rows


def write_bmp(path, rows):
    height = len(rows)
    width = len(rows[0]) if height else 0
    stride = ((width * 3 + 3) // 4) * 4
    pad = b"\x00" * (stride - width * 3)
    body = b"".join(
        b"".join(bytes(pixel) for pixel in rows[y]) + pad for y in range(height - 1, -1, -1)
    )
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(body), 0, 0, 14 + 40)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(path, "wb").write(header + info + body)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("target")
    parser.add_argument("--at", nargs=2, type=int, default=[0, 0], metavar=("X", "Y"))
    parser.add_argument("--size", nargs=2, type=int, required=True, metavar=("W", "H"))
    parser.add_argument("--zoom", type=int, default=1)
    args = parser.parse_args()

    width, height, rows = read_bmp(args.source)
    x0, y0 = args.at
    w, h = args.size
    x1, y1 = min(x0 + w, width), min(y0 + h, height)
    if x0 >= x1 or y0 >= y1:
        raise SystemExit("the cut is empty")

    piece = [row[x0:x1] for row in rows[y0:y1]]
    if args.zoom > 1:
        piece = [
            [pixel for pixel in row for _ in range(args.zoom)]
            for row in piece
            for _ in range(args.zoom)
        ]
    write_bmp(args.target, piece)
    print("%s: %dx%d" % (args.target, len(piece[0]), len(piece)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
