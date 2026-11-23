#!/usr/bin/env python3
"""Draws the installer artwork and the app icon in PromptMIDI's own look (Source/Plugin/Theme.h).

    python3 Tools/make_installer_art.py

Writes:
    Packaging/Art/wizard-<scale>.png        Windows installer: the tall image on the first and last pages
    Packaging/Art/wizard-small-<scale>.png  Windows installer: the mark in the top corner of the other pages
    Packaging/Art/PromptMIDI.ico            Windows installer, uninstaller and app icon
    Packaging/Art/macos-background.png, macos-background-dark.png   macOS installer backgrounds
    Packaging/Art/promptmidi-256.png        Linux: the app's menu icon
    Resources/Icon/PromptMIDI.png           the app icon (1024 x 1024) the build uses
Needs Pillow. The results are committed, so building an installer doesn't need Python.
"""

import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "Packaging", "Art")
FONTS = os.path.join(ROOT, "Resources", "Fonts")

BG = (13, 15, 19)
TEXT = (231, 233, 238)
TEXT_DIM = (115, 124, 138)
ACCENT = (125, 111, 247)
ACCENT_HOT = (155, 144, 255)
TRACKS = [(125, 111, 247), (77, 171, 247), (64, 201, 139), (244, 181, 75), (239, 99, 113), (176, 132, 255), (90, 200, 220)]

# Inno Setup 6.6's image areas at 100, 150, 200 and 250 % (it picks the closest).
WIZARD_SIZES = {100: (202, 386), 150: (336, 643), 200: (430, 824), 250: (551, 1055)}
SMALL_SIZES = {100: 58, 150: 97, 200: 124, 250: 159}


def font(size, weight="SemiBold"):
    return ImageFont.truetype(os.path.join(FONTS, f"Inter-{weight}.ttf"), size)


def mark(size):
    """The PromptMIDI mark: a rounded square in the accent gradient with a white note."""
    s = size * 4
    grad = Image.new("RGBA", (s, s))
    dark = tuple(int(c * 0.65) for c in ACCENT)
    gd = ImageDraw.Draw(grad)
    for i in range(2 * s):
        t = i / (2 * s)
        col = tuple(int(ACCENT_HOT[c] + (dark[c] - ACCENT_HOT[c]) * t) for c in range(3)) + (255,)
        gd.line((i, 0, 0, i), fill=col, width=2)
    m = Image.new("L", (s, s), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, s - 1, s - 1), radius=int(s * 0.25), fill=255)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    img.paste(grad, (0, 0), m)
    d = ImageDraw.Draw(img)
    u = s / 28.0
    d.ellipse((7.5 * u, 15.5 * u, 16.0 * u, 22.0 * u), fill="white")
    d.rectangle((14.6 * u, 6.0 * u, 16.4 * u, 19.5 * u), fill="white")
    d.rectangle((14.6 * u, 6.0 * u, 21.1 * u, 7.8 * u), fill="white")
    return img.resize((size, size), Image.LANCZOS)


def roll(size, box, seed, alpha):
    """A layer of piano-roll bars, like the arrangement view."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x0, y0, x1, y1 = box
    rows = 14
    row_h = (y1 - y0) / rows
    rnd = seed
    for r in range(rows):
        x = x0 + rnd % 23
        col = TRACKS[r % len(TRACKS)]
        while x < x1:
            rnd = (rnd * 1103515245 + 12345) & 0x7FFFFFFF
            w = 10 + rnd % 46
            if rnd % 5:
                a = int(alpha * (0.45 + (rnd % 100) / 180))
                d.rounded_rectangle((x, y0 + r * row_h + 1, min(x1, x + w), y0 + (r + 1) * row_h - 2), radius=2, fill=col + (a,))
            x += w + 4 + rnd % 18
    return layer


def faded(layer, mask):
    """layer, with its opacity multiplied by mask (L)."""
    out = layer.copy()
    out.putalpha(ImageChops.multiply(layer.getchannel("A"), mask))
    return out


def ramp(size, start, end, horizontal=False, peak=255):
    """A mask that rises from 0 at start to peak at end (pixels, along y or x)."""
    w, h = size
    mask = Image.new("L", size, 0)
    d = ImageDraw.Draw(mask)
    for i in range(int(start), w if horizontal else h):
        v = int(peak * min(1.0, max(0.0, (i - start) / max(1, end - start))))
        d.line((i, 0, i, h) if horizontal else (0, i, w, i), fill=v)
    return mask


def glow(img, centre, radius, colour, strength):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse((centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius), fill=colour + (strength,))
    img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(radius * 0.6)))


def wizard(w, h):
    """The tall image: a violet glow, the mark and name, a piano roll rising out of the dark below."""
    img = Image.new("RGBA", (w, h), BG + (255,))
    k = w / 202
    glow(img, (w // 2, int(h * 0.3)), int(100 * k), ACCENT, 90)
    bars = roll((w, h), (0, int(h * 0.6), w, int(h * 0.99)), 7, 160)
    img.alpha_composite(faded(bars, ramp((w, h), h * 0.6, h * 0.9, peak=210)))
    m = mark(int(60 * k))
    top = int(h * 0.2)
    img.alpha_composite(m, ((w - m.width) // 2, top))
    d = ImageDraw.Draw(img)
    f = font(int(22 * k))
    d.text(((w - d.textlength("PromptMIDI", font=f)) / 2, top + m.height + 16 * k), "PromptMIDI", font=f, fill=TEXT)
    f2 = font(int(12 * k), "Regular")
    tag = "Describe it. Get MIDI."
    d.text(((w - d.textlength(tag, font=f2)) / 2, top + m.height + 47 * k), tag, font=f2, fill=TEXT_DIM)
    return img.convert("RGB")


def wizard_small(n):
    """The corner image: just the mark, on transparent."""
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    m = mark(int(n * 0.86))
    img.alpha_composite(m, ((n - m.width) // 2, (n - m.height) // 2))
    return img


def mac_background(dark):
    """macOS Installer draws this behind its step list; the mark and a roll sit bottom left, below the steps."""
    w, h = 1240, 836          # 620 x 418 points at 2x
    base = BG if dark else (246, 246, 249)
    img = Image.new("RGBA", (w, h), base + (255,))
    glow(img, (130, 600), 120, ACCENT, 70 if dark else 40)
    bars = roll((w, h), (0, int(h * 0.74), int(w * 0.32), h - 24), 11, 90 if dark else 70)
    fade = ImageChops.multiply(ramp((w, h), h * 0.74, h * 0.92), ImageChops.invert(ramp((w, h), w * 0.12, w * 0.3, horizontal=True)))
    img.alpha_composite(faded(bars, fade))
    m = mark(112)
    img.alpha_composite(m, (64, 520))
    return img.convert("RGB")


def app_icon():
    """1024 x 1024: the mark with the margin macOS and Windows expect around an app icon."""
    img = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    img.alpha_composite(mark(824), (100, 100))
    return img


def main():
    os.makedirs(ART, exist_ok=True)
    for scale, (w, h) in WIZARD_SIZES.items():
        wizard(w, h).save(os.path.join(ART, f"wizard-{scale}.png"), optimize=True)
    for scale, n in SMALL_SIZES.items():
        wizard_small(n).save(os.path.join(ART, f"wizard-small-{scale}.png"), optimize=True)
    icon = app_icon()
    icon.save(os.path.join(ART, "PromptMIDI.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    for dark, name in ((False, "macos-background.png"), (True, "macos-background-dark.png")):
        mac_background(dark).save(os.path.join(ART, name), dpi=(144, 144), optimize=True)
    icon.resize((256, 256), Image.LANCZOS).save(os.path.join(ART, "promptmidi-256.png"), optimize=True)
    os.makedirs(os.path.join(ROOT, "Resources", "Icon"), exist_ok=True)
    icon.save(os.path.join(ROOT, "Resources", "Icon", "PromptMIDI.png"), optimize=True)
    print("wrote", ART)


if __name__ == "__main__":
    main()
