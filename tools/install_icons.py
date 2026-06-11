#!/usr/bin/env python3
"""Take the rendered master layers (/tmp/icon) and install every platform icon:
lobby PNG, Android adaptive (fg/bg + legacy + round), and the PC .ico/.png."""
import os
from PIL import Image

SRC = "/tmp/icon"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # ThumbyElite/
full = Image.open(f"{SRC}/full.png").convert("RGB")
abg  = Image.open(f"{SRC}/abg.png").convert("RGB")
afg  = Image.open(f"{SRC}/afg.png").convert("RGBA")

def save(img, path, size):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.resize((size, size), Image.LANCZOS).save(path)

# --- lobby (device): 48x48 ----------------------------------------------
lobby = os.path.normpath(f"{ROOT}/../ThumbyOne/lobby/icons/elite.png")
save(full, lobby, 48)
print("lobby ->", lobby)

# --- Android ------------------------------------------------------------
ARES = f"{ROOT}/android/app/src/main/res"
DENS = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
for d, m in DENS.items():
    leg = int(48 * m); adp = int(108 * m)
    save(full.convert("RGBA"), f"{ARES}/mipmap-{d}/ic_launcher.png", leg)
    save(full.convert("RGBA"), f"{ARES}/mipmap-{d}/ic_launcher_round.png", leg)
    save(afg, f"{ARES}/mipmap-{d}/ic_launcher_foreground.png", adp)
    save(abg.convert("RGBA"), f"{ARES}/mipmap-{d}/ic_launcher_background.png", adp)

ADP_XML = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
           '    <background android:drawable="@mipmap/ic_launcher_background" />\n'
           '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
           '</adaptive-icon>\n')
os.makedirs(f"{ARES}/mipmap-anydpi-v26", exist_ok=True)
for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
    with open(f"{ARES}/mipmap-anydpi-v26/{name}", "w") as f:
        f.write(ADP_XML)
print("android -> adaptive + legacy + round")

# --- PC: multi-size .ico + window png -----------------------------------
WIN = f"{ROOT}/host/win"
full.resize((256, 256), Image.LANCZOS).save(f"{WIN}/indemnityrun.png")
full.save(f"{WIN}/indemnityrun.ico",
          sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("pc ->", f"{WIN}/indemnityrun.ico")
