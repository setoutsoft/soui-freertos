#!/usr/bin/env python3
"""mkicon.py -- generate res/app.ico for the FreeRTOS demos.

Why a generator instead of a checked-in drawing: the icon has to demonstrate the
two things the port's icon path gets wrong most easily -- per-pixel alpha and a
size list -- and both are easier to keep honest with 20 lines of SDF maths than
with a binary blob nobody can regenerate.

  python tools/mkicon.py res/app.ico

Output: one .ico holding 32x32 and 16x16 entries, 32 bits per pixel, supersampled
4x so the alpha ramp is clean at both sizes.  The mark is a "window": a rounded
frame with a title band and two text lines -- it still reads at 16 px, which a
letter would not.
"""
import struct
import sys

SS = 4  # supersampling factor


def lerp(a, b, t):
    return a + (b - a) * t


def rrect(x, y, x0, y0, x1, y1, r):
    """Signed distance to a rounded rectangle (negative inside)."""
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    hx, hy = (x1 - x0) / 2.0, (y1 - y0) / 2.0
    qx = abs(x - cx) - (hx - r)
    qy = abs(y - cy) - (hy - r)
    return (max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2) ** 0.5 + min(max(qx, qy), 0.0) - r


def cov(d):
    """1 inside, 0 outside, one-pixel ramp across the edge."""
    return max(0.0, min(1.0, 0.5 - d))


def render(size):
    W = size * SS
    f = float(W)
    # window frame: outer box 22%..78%, 2.6% stroke -> ~2.5px at 32px
    box = (0.20 * f, 0.24 * f, 0.80 * f, 0.78 * f)
    stroke = 0.055 * f
    band = (0.20 * f, 0.24 * f, 0.80 * f, 0.42 * f)          # title band
    lines = [(0.30 * f, 0.53 * f, 0.70 * f, 0.60 * f),
             (0.30 * f, 0.67 * f, 0.70 * f, 0.74 * f)]

    rows = []
    for y in range(W):
        row = []
        for x in range(W):
            px, py = x + 0.5, y + 0.5

            bg_d = rrect(px, py, 0.0, 0.0, f, f, 0.22 * f)
            bg_a = cov(bg_d)

            # white marks
            frame = abs(rrect(px, py, *box, 0.06 * f)) - stroke / 2.0
            fg = cov(frame)
            fg = max(fg, cov(rrect(px, py, *band, 0.03 * f)))
            for ln in lines:
                fg = max(fg, cov(rrect(px, py, *ln, 0.02 * f)))

            # vertical gradient: orange -> pink (the demo's accent pair)
            t = py / f
            br = lerp(0xFF, 0xE9, t)
            bg = lerp(0x8C, 0x1E, t)
            bb = lerp(0x10, 0x63, t)

            row.append((lerp(br, 255.0, fg), lerp(bg, 255.0, fg),
                        lerp(bb, 255.0, fg), bg_a))
        rows.append(row)

    # box-filter down to `size`
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            ar = ag = ab = aa = 0.0
            for yy in range(SS):
                for xx in range(SS):
                    r_, g_, b_, a_ = rows[y * SS + yy][x * SS + xx]
                    ar += r_ * a_
                    ag += g_ * a_
                    ab += b_ * a_
                    aa += a_
            n = SS * SS
            if aa <= 0.0:
                row.append((0, 0, 0, 0))
            else:
                # premultiplied average -> straight alpha
                row.append((int(ar / aa), int(ag / aa), int(ab / aa),
                            int(round(aa / n * 255))))
        out.append(row)
    return out


def dib(rows, size):
    """BITMAPINFOHEADER + bottom-up BGRA, i.e. what an RT_ICON holds."""
    hdr = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0,
                      size * size * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(size - 1, -1, -1):  # bottom-up
        for r, g, b, a in rows[y]:
            body += bytes((b, g, r, a))
    # AND mask: all zero (alpha decides); 1bpp rows padded to 4 bytes
    maskRow = ((size + 31) // 32) * 4
    body += bytes(maskRow * size)
    return hdr + bytes(body)


def main(path):
    sizes = [32, 16]
    images = [dib(render(s), s) for s in sizes]
    out = bytearray(struct.pack('<HHH', 0, 1, len(sizes)))
    offset = 6 + 16 * len(sizes)
    for s, img in zip(sizes, images):
        out += struct.pack('<BBBBHHII', s, s, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    for img in images:
        out += img
    with open(path, 'wb') as f:
        f.write(out)
    print('wrote %s (%d bytes, sizes %s)' % (path, len(out), sizes))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'res/app.ico')
