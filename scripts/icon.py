"""
icon
Generates the home screen icons of the web UI, for iOS and for Android.

The mark is the plotter itself: two anchors at the top, two belts running
down to the bot, and the line the bot has drawn underneath it. Drawn in the accent blue the whole UI uses (--accent in app.css) so the
icon on a home screen and the page it opens are recognisably the same thing.

Deliberately dependency-free. Pillow would draw this in ten lines, but it would
also be a package somebody has to install before they can regenerate a file
that changes once a year - so the shapes are signed distance fields, the
antialiasing is analytic, and the PNG encoder is zlib plus a header. The
results are committed, so neither the web build nor CI ever runs this:

    python scripts/icon.py

@author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
@version  1.0
@created  31.8.2026
@updated  15.11.2026
"""

import math
import os
import struct
import zlib

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUBLIC_DIR = os.path.join(PROJECT_DIR, "web", "public")

# --accent and --accent-text from web/src/app.css, light theme.
ACCENT = (0x3B, 0x6E, 0xA5)
INK = (0xFF, 0xFF, 0xFF)


def rounded_rect(px, py, cx, cy, half_w, half_h, radius):
    """Signed distance to a rounded rectangle: negative inside, in pixels."""
    dx = abs(px - cx) - (half_w - radius)
    dy = abs(py - cy) - (half_h - radius)
    outside = math.hypot(max(dx, 0.0), max(dy, 0.0))
    inside = min(max(dx, dy), 0.0)
    return outside + inside - radius


def circle(px, py, cx, cy, radius):
    return math.hypot(px - cx, py - cy) - radius


def segment(px, py, ax, ay, bx, by):
    """Unsigned distance to the line segment a-b."""
    vx, vy = bx - ax, by - ay
    t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy)
    t = min(1.0, max(0.0, t))
    return math.hypot(px - (ax + t * vx), py - (ay + t * vy))


def squiggle(cx, cy, mark, n=24):
    """The drawn line under the bot: one and a half periods of a sine, as a
    polyline - fine enough that the facets do not show at 512 px."""
    half = mark * 0.42
    amp = mark * 0.06
    base = cy + mark * 0.42
    return [(cx - half + 2 * half * i / n,
             base + amp * math.sin(3 * math.pi * i / n))
            for i in range(n + 1)]


def geometry(cx, cy, mark):
    """Anchors, bot and bot radius, shared by the PNGs and the favicon so the
    two cannot drift apart."""
    left = (cx - mark * 0.5, cy - mark * 0.42)
    right = (cx + mark * 0.5, cy - mark * 0.42)
    bot = (cx, cy + mark * 0.16)
    return left, right, bot, mark * 0.12


def coverage(distance):
    """Analytic antialiasing: one pixel of falloff across the edge."""
    return min(1.0, max(0.0, 0.5 - distance))


def over(base, colour, alpha):
    return tuple(round(b + (c - b) * alpha) for b, c in zip(base, colour))


def render(size, maskable):
    """One icon as a flat list of RGB rows.

    `maskable` draws the background full-bleed and shrinks the mark, because a
    launcher may crop a maskable icon to any shape inside the middle 80 %.
    """
    # Everything below is in fractions of the icon, so the same numbers hold at
    # 180 px and at 512.
    plate_radius = 0.0 if maskable else size * 0.22
    mark = size * (0.44 if maskable else 0.54)  # height of the drawing

    cx = size / 2.0
    cy = size / 2.0

    left, right, bot, bot_r = geometry(cx, cy, mark)
    anchor_r = max(1.0, mark * 0.05)
    belt = max(0.75, size * 0.03) / 2.0       # thinner than the drawn line:
    line = max(1.0, size * 0.055) / 2.0       # the drawing is the point
    wave = squiggle(cx, cy, mark)
    waves = list(zip(wave, wave[1:]))

    rows = []
    for y in range(size):
        row = bytearray()
        py = y + 0.5
        for x in range(size):
            px = x + 0.5

            if maskable:
                pixel = ACCENT
            else:
                plate = coverage(rounded_rect(px, py, cx, cy, size / 2.0, size / 2.0, plate_radius))
                # Outside the plate is transparent-looking white; the icons are
                # opaque RGB, and every launcher paints them on something.
                pixel = over((0xFF, 0xFF, 0xFF), ACCENT, plate)

            # Belts, anchors, the bot, and what it drew.
            d = min(segment(px, py, *left, *bot), segment(px, py, *right, *bot)) - belt
            d = min(d, circle(px, py, *left, anchor_r), circle(px, py, *right, anchor_r))
            d = min(d, circle(px, py, *bot, bot_r))
            d = min(d, min(segment(px, py, *a, *b) for a, b in waves) - line)
            pixel = over(pixel, INK, coverage(d))

            row += bytes(pixel)
        rows.append(bytes(row))
    return rows


def write_png(path, size, rows):
    """Minimal RGB8 PNG: signature, IHDR, one IDAT, IEND."""

    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\x00" + row for row in rows)  # filter type 0 per scanline
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as out:
        out.write(png)
    print("wrote %s (%d x %d)" % (os.path.relpath(path, PROJECT_DIR), size, size))


def write_favicon(path):
    """The browser tab icon, as SVG - one file that is sharp at every size."""
    accent = "#%02x%02x%02x" % ACCENT
    mark = 64 * 0.54
    (lx, ly), (rx, ry), (bx, by), br = geometry(32.0, 32.0, mark)
    wave = " ".join("%.1f,%.1f" % p for p in squiggle(32.0, 32.0, mark))
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">
  <rect width="64" height="64" rx="14" fill="{accent}"/>
  <g stroke="#fff" fill="none" stroke-linecap="round" stroke-linejoin="round">
    <path d="M{lx:.1f} {ly:.1f} L{bx:.1f} {by:.1f} L{rx:.1f} {ry:.1f}" stroke-width="1.9"/>
    <polyline points="{wave}" stroke-width="3.5"/>
  </g>
  <g fill="#fff">
    <circle cx="{lx:.1f}" cy="{ly:.1f}" r="{mark * 0.05:.1f}"/>
    <circle cx="{rx:.1f}" cy="{ry:.1f}" r="{mark * 0.05:.1f}"/>
    <circle cx="{bx:.1f}" cy="{by:.1f}" r="{br:.1f}"/>
  </g>
</svg>
"""
    with open(path, "w", encoding="utf-8", newline="\n") as out:
        out.write(svg)
    print("wrote %s" % os.path.relpath(path, PROJECT_DIR))


def main():
    os.makedirs(PUBLIC_DIR, exist_ok=True)

    write_favicon(os.path.join(PUBLIC_DIR, "favicon.svg"))

    # iOS reads apple-touch-icon and ignores the manifest; Android does the
    # opposite. The maskable one is declared separately in the manifest because
    # it is drawn with a wider margin - see sendManifest() in WebRoutes.cpp.
    for name, size, maskable in (
        ("apple-touch-icon.png", 180, True),
        ("icon-192.png", 192, False),
        ("icon-512.png", 512, False),
        ("icon-512-maskable.png", 512, True),
    ):
        write_png(os.path.join(PUBLIC_DIR, name), size, render(size, maskable))


if __name__ == "__main__":
    main()
