#!/usr/bin/env python3
"""
Indemnity Run — master app-icon generator.

Composites two real game renders (so the icon IS the game art, palette keyed to
the ship's grey hull / purple cockpit / gold cannon tips):

  - background : ELITE_ICONBG  -> /tmp/elite_iconbg.ppm  (game world + distant
                 star on dark space, faint purple nebula accent)
  - foreground : ELITE_APPICON -> /tmp/elite_icon.ppm     (the VIPER hull on a
                 magenta key, keyed out here)

Emits the layers every platform needs:
  - lobby (device)   48x48 PNG
  - Android adaptive  foreground (transparent) + background + legacy/round
  - PC                256 master PNG (-> .ico in install_icons.py)
"""
import os, sys
from PIL import Image

SS = 4                      # supersample
R = 256                     # logical master size
W = R * SS

_ship_cache = [None]
def load_ship():
    """The real game hull, rendered by ELITE_APPICON on a magenta key bg."""
    if _ship_cache[0] is not None:
        return _ship_cache[0]
    path = os.environ.get("ICON_SHIP", "/tmp/elite_icon.ppm")
    s = Image.open(path).convert("RGBA")
    px = s.load()
    for y in range(s.size[1]):
        for x in range(s.size[0]):
            r, g, b, _ = px[x, y]
            if r > 170 and b > 170 and g < 95:        # magenta key
                px[x, y] = (0, 0, 0, 0)
    s = s.crop(s.getbbox())                            # trim to the hull
    _ship_cache[0] = s
    return s

def add_ship(img, cxf, cyf, widthf):
    ship = load_ship()
    w = int(W * widthf)
    h = int(w * ship.size[1] / ship.size[0])
    ship = ship.resize((w, h), Image.LANCZOS)
    img.alpha_composite(ship, (int(W * cxf - w / 2), int(W * cyf - h / 2)))

_bg_cache = [None]
def load_bg():
    """The real game backdrop (world + distant star on dark space + faint
    nebula), from ELITE_ICONBG."""
    if _bg_cache[0] is not None:
        return _bg_cache[0]
    path = os.environ.get("ICON_BG", "/tmp/elite_iconbg.ppm")
    bg = Image.open(path).convert("RGBA").resize((W, W), Image.LANCZOS)
    _bg_cache[0] = bg
    return bg

def render(mode):
    """'full' square icon | 'abg' adaptive background | 'afg' adaptive fg."""
    if mode in ("full", "abg"):
        img = load_bg().copy()
    else:
        img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    if mode == "full":
        add_ship(img, 0.48, 0.42, 0.82)
    elif mode == "afg":
        # ship centred + smaller so the launcher mask never crops it
        add_ship(img, 0.50, 0.50, 0.66)

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
