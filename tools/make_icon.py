#!/usr/bin/env python3
"""
Indemnity Run — master app-icon generator.

Draws a single high-res scene (a fighter banking over a lit world, deep-space
backdrop with a blue/red nebula wash, in the wordmark's cyan->gold palette) and
emits every platform target:

  - lobby (device)   48x48 PNG
  - Android adaptive  foreground (transparent) + background layers + legacy png
  - PC                256 master PNG (-> .ico built by ImageMagick)

Layers can be rendered separately (full / fg / bg) for the adaptive icon.
"""
import math, os, sys, random
from PIL import Image, ImageDraw, ImageFilter

SS = 4                      # supersample
R = 256                     # logical master size
W = R * SS

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def radial_bg(size, inner, outer, cx, cy, rad):
    img = Image.new("RGB", (size, size), outer)
    px = img.load()
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy) / rad
            if d > 1: d = 1
            px[x, y] = lerp(inner, outer, d * d)
    return img

def soft_blob(layer, cx, cy, rad, color, alpha):
    """Add a soft radial glow (additive-ish) onto an RGBA layer."""
    blob = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(blob)
    steps = 24
    for i in range(steps, 0, -1):
        t = i / steps
        a = int(alpha * (1 - t) * (1 - t))
        r = rad * t
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (a,))
    layer.alpha_composite(blob)

def draw_planet(layer, cx, cy, rad, light):
    """A lit sphere with a terminator + bright limb + thin atmosphere ring."""
    sph = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    px = sph.load()
    lit  = (70, 150, 175)      # teal day side
    dark = (10, 22, 40)        # night side
    rim  = (150, 235, 255)     # limb glow
    for y in range(int(cy - rad - 4), int(cy + rad + 4)):
        if y < 0 or y >= layer.size[1]: continue
        for x in range(int(cx - rad - 4), int(cx + rad + 4)):
            if x < 0 or x >= layer.size[0]: continue
            dx, dy = (x - cx) / rad, (y - cy) / rad
            r2 = dx * dx + dy * dy
            if r2 > 1.02: continue
            nz = math.sqrt(max(0.0, 1 - r2))
            shade = max(0.0, dx * light[0] + dy * light[1] + nz * light[2])
            shade = 0.06 + 0.94 * shade
            base = lerp(dark, lit, shade)
            if r2 > 0.86:                       # limb / atmosphere
                t = (r2 - 0.86) / 0.16
                base = lerp(base, rim, min(1.0, t) * (0.5 + 0.5 * shade))
            a = 255 if r2 <= 0.98 else int(255 * (1.02 - r2) / 0.04)
            px[x, y] = base + (max(0, min(255, a)),)
    sph = sph.filter(ImageFilter.GaussianBlur(SS * 0.4))
    layer.alpha_composite(sph)

def draw_ship(size, scale):
    """A sleek wedge fighter (nose up), cyan body + warm engine glow, on its
    own transparent layer for rotation."""
    s = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(s)
    cx, cy = size // 2, size // 2
    def P(x, y): return (cx + x * scale, cy + y * scale)
    # engine glow first (behind the hull): a couple of tight warm blobs
    soft_blob(s, *P(0, 1.45), rad=size * 0.085, color=(255, 120, 40), alpha=170)
    soft_blob(s, *P(0, 1.08), rad=size * 0.13, color=(255, 190, 100), alpha=230)
    # hull (wedge)
    hull = [P(0, -1.55), P(-1.15, 0.55), P(-0.42, 0.30), P(0, 0.95),
            P(0.42, 0.30), P(1.15, 0.55)]
    d.polygon(hull, fill=(36, 70, 110))
    # lit top half (lighter, left-lit)
    top = [P(0, -1.55), P(-1.15, 0.55), P(-0.42, 0.30), P(0, 0.30),
           P(0, -1.55)]
    d.polygon(top, fill=(150, 215, 245))
    right = [P(0, -1.55), P(0, 0.30), P(0.42, 0.30), P(1.15, 0.55)]
    d.polygon(right, fill=(70, 140, 190))
    # crisp nose-to-tail spine + outline
    d.line([P(0, -1.55), P(0, 0.95)], fill=(210, 245, 255), width=max(1, scale // 18))
    d.line(hull + [hull[0]], fill=(12, 24, 44), width=max(1, scale // 16), joint="curve")
    # cockpit
    d.ellipse([P(-0.16, -0.75)[0], P(-0.16, -0.75)[1], P(0.16, -0.30)[0], P(0.16, -0.30)[1]],
              fill=(20, 40, 70))
    d.ellipse([P(-0.10, -0.68)[0], P(-0.10, -0.68)[1], P(0.10, -0.42)[0], P(0.10, -0.42)[1]],
              fill=(140, 230, 255))
    return s

def stars(layer, n, seed):
    rnd = random.Random(seed)
    d = ImageDraw.Draw(layer)
    for _ in range(n):
        x, y = rnd.uniform(0, layer.size[0]), rnd.uniform(0, layer.size[1])
        b = rnd.uniform(0.3, 1.0)
        r = SS * (1.4 if b > 0.85 else 0.8)
        c = (int(220 * b), int(230 * b), int(255 * b))
        d.ellipse([x - r, y - r, x + r, y + r], fill=c)

def space_bg():
    base = radial_bg(W, (26, 40, 74), (6, 9, 20), W * 0.40, W * 0.34, W * 0.95)
    img = base.convert("RGBA")
    soft_blob(img, W * 0.74, W * 0.30, W * 0.42, (40, 70, 150), 90)   # blue nebula
    soft_blob(img, W * 0.22, W * 0.66, W * 0.34, (150, 40, 60), 60)   # red nebula
    stars(img, 90, 7)
    return img

def add_ship(img, cxf, cyf, scalef):
    ship = draw_ship(int(W * 0.7), int(W * scalef / 3.2))
    ship = ship.rotate(-32, resample=Image.BICUBIC, expand=False)
    img.alpha_composite(ship, (int(W * cxf - ship.size[0] / 2),
                               int(W * cyf - ship.size[1] / 2)))

def render(mode):
    """'full' square icon | 'abg' adaptive background | 'afg' adaptive fg."""
    if mode in ("full", "abg"):
        img = space_bg()
    else:
        img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    if mode in ("full", "abg"):
        draw_planet(img, W * 0.78, W * 1.04, W * 0.55, light=(-0.55, -0.45, 0.70))

    if mode == "full":
        add_ship(img, 0.46, 0.40, 0.70)
    elif mode == "afg":
        # ship centred + smaller so the launcher mask never crops it
        add_ship(img, 0.50, 0.50, 0.52)

    return img.resize((R, R), Image.LANCZOS)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/icon"
    os.makedirs(out, exist_ok=True)
    render("full").convert("RGB").save(f"{out}/full.png")
    render("abg").convert("RGB").save(f"{out}/abg.png")
    render("afg").save(f"{out}/afg.png")
    comp = render("abg").convert("RGBA"); comp.alpha_composite(render("afg"))
    comp.convert("RGB").save(f"{out}/preview.png")
    print("wrote", out)
