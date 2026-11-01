"""Artwork for the Kural setup wizard (Windows, Inno Setup).

usage: python3 packaging/windows/make_images.py
  needs Pillow with libraqm (correct Tamil shaping); fails loudly without it
writes next to this script: wizard.png (tall side image, 164 x 314 at 3x),
wizard-small.png (corner badge, 58 x 58 at 3x) and kural.ico (16 - 256 px)

Same palette as the plugin (Source/UI/Theme.cpp). The three lines in the tall image are artwork
(one voice becoming several), not a display of data.
"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont, features

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', '..', 'Resources', 'fonts')
TAMIL = 'குரல்'

ABYSS = (2, 8, 13); DEEP = (5, 21, 32); SURFACE = (18, 54, 70)
PEARL = (230, 236, 228); PEARL_DIM = (143, 163, 163); LABEL = (201, 214, 210)
VOICE = (191, 238, 233); GLOW = (63, 179, 184)

if not features.check('raqm'):
    raise SystemExit('Pillow was built without libraqm: Tamil would be shaped wrongly. Install libraqm and retry.')


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size, layout_engine=ImageFont.Layout.RAQM)


def water(w, h):
    """vertical depth gradient with faint caustic filaments near the top (same recipe as the plugin)"""
    y = np.linspace(0.0, 1.0, h)[:, None]
    t = np.clip(y * 1.6, 0, 1)
    top, mid, bot = np.array(SURFACE) * 0.8, np.array(DEEP), np.array(ABYSS)
    col = np.where(t[..., None] < 0.35, top + (mid - top) * (t[..., None] / 0.35),
                   mid + (bot - mid) * ((t[..., None] - 0.35) / 0.65))
    img = np.broadcast_to(col, (h, w, 3)).astype(np.float64).copy()
    yy, xx = np.mgrid[0:h, 0:w] / (w / 260.0)
    fx, fy = xx * 0.11, yy * 0.13
    a = np.sin(fx + 1.7 * np.sin(fy * 0.7)) + np.sin(fy * 1.1 + 1.3 * np.sin(fx * 0.6 + 0.8))
    b = np.sin(fx * 0.8 - fy * 0.5 + 2.0 * np.sin(fx * 0.31))
    v = np.clip(1.0 - np.abs(a + 0.6 * b) * 0.9, 0, 1) ** 3
    fade = np.clip(1.0 - (np.arange(h)[:, None] / h) * 1.25, 0, 1)
    img += (np.array(VOICE) - img) * (0.06 * v * fade)[..., None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGB')


def centred(d, y, text, f, fill, w):
    b = d.textbbox((0, 0), text, font=f)
    d.text(((w - (b[2] - b[0])) / 2 - b[0], y - b[1]), text, font=f, fill=fill)


def spaced(d, y, text, f, fill, w, gap):
    widths = [d.textbbox((0, 0), c, font=f)[2] for c in text]
    total = sum(widths) + gap * (len(text) - 1)
    x = (w - total) / 2
    for c, cw in zip(text, widths):
        d.text((x, y), c, font=f, fill=fill)
        x += cw + gap


def tall():
    W, H = 164 * 3, 314 * 3
    img = water(W, H).convert('RGBA')

    # three flowing lines: one voice (bright) and two harmonies following it (fainter)
    lines = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(lines)
    def path(offset, phase, amp):
        pts = []
        for i in range(0, W + 1, 3):
            x = i / W
            yv = 610 + offset - 50 * math.sin(x * 2.4 + 0.4) - amp * math.sin(x * 23.0 + phase) * min(1.0, x * 1.6)
            pts.append((i, yv))
        return pts
    for off, ph, amp, col, wd in ((-70, 0.9, 5, GLOW, 3), (70, 0.4, 5, GLOW, 3), (0, 0.0, 7, VOICE, 4)):
        d.line(path(off, ph, amp), fill=col + (110 if col == GLOW else 235,), width=wd, joint='curve')
    glow = lines.filter(ImageFilter.GaussianBlur(9))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, lines)

    d = ImageDraw.Draw(img)
    centred(d, 150, TAMIL, font('catamaran800.ttf', 116), PEARL, W)
    spaced(d, 318, 'KURAL', font('barlow600.ttf', 42), LABEL, W, 12)
    centred(d, 380, 'by R4N', font('catamaran600.ttf', 34), PEARL_DIM, W)
    # pearl hairline
    for x in range(W):
        a = max(0.0, 1.0 - abs(x - W / 2) / (W * 0.36))
        img.putpixel((x, 448), tuple(int(c * a + bg * (1 - a)) for c, bg in zip(PEARL, img.getpixel((x, 448))[:3])) + (255,))
    centred(d, 760, 'VOCAL HARMONIZER', font('barlow600.ttf', 26), PEARL_DIM, W)
    centred(d, 800, 'VST3  ·  Windows 64-bit', font('barlow500.ttf', 24), (95, 122, 130), W)
    img.convert('RGB').save(os.path.join(HERE, 'wizard.png'))


def badge(size, with_glow=True):
    S = size * 4
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    bg = water(S, S).convert('RGBA')
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=S * 0.22, fill=255)
    img.paste(bg, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((2, 2, S - 3, S - 3), radius=S * 0.22, outline=PEARL + (70,), width=max(2, S // 64))
    centred(d, S * 0.16, 'கு', font('catamaran800.ttf', int(S * 0.62)), PEARL, S)
    return img.resize((size, size), Image.LANCZOS)


def small():
    W = 58 * 3
    img = Image.new('RGB', (W, W), ABYSS)
    b = badge(W)
    img.paste(b, (0, 0), b)
    img.save(os.path.join(HERE, 'wizard-small.png'))


def icon():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = [badge(s) for s in sizes]
    imgs[-1].save(os.path.join(HERE, 'kural.ico'), sizes=[(s, s) for s in sizes], append_images=imgs[:-1])


if __name__ == '__main__':
    tall()
    small()
    icon()
    print('wrote wizard.png, wizard-small.png, kural.ico')
