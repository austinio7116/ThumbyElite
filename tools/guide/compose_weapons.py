"""Armoury showcase compositor.

Input (produced by ELITE_WPNMOVIE=1 ./build_host/thumbyelite_host 42
                   > /tmp/wpn_log.txt):
  /tmp/movie/f_*.ppm       game frames, 30fps
  /tmp/guide_audio.raw     s16 mono 22050Hz, 735 samples per frame
  /tmp/wpn_log.txt         [card] N NAME|sub   and   [cap] N text

Output: /tmp/wpn_cut/c_*.png + /tmp/wpn_audio.wav (silence inserted
wherever card frames pause the gameplay, so audio stays frame-synced).
"""
from PIL import Image, ImageDraw, ImageFont
import glob, os, re, textwrap, wave

SC = 4; S = 128 * SC
def F(sz, b=False):
    p = ('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf' if b
         else '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf')
    try: return ImageFont.truetype(p, sz)
    except Exception: return ImageFont.load_default()
fcap = F(24, True); fbig = F(56, True); fsub = F(26); ftag = F(18, True)

# weapon HUD colors from k_weapons, for the card titles
WCOL = {
    'PULSE-S': (255, 80, 60),   'PULSE-M': (255, 120, 50),
    'PULSE-L': (255, 160, 40),  'BEAM':    (255, 220, 120),
    'PHOTON':  (120, 220, 255), 'GAUSS':   (200, 230, 255),
    'AUTOCAN': (255, 210, 140), 'MISSILE': (255, 170, 90),
    'HOMING':  (255, 120, 200), 'FLAK':    (255, 190, 110),
    'RAILGUN': (170, 240, 255), 'ION':     (110, 160, 255),
    'MINE':    (255, 140, 70),  'TRACTOR': (255, 215, 120),
    'MINING':  (255, 200, 90),  'PLASMA':  (120, 235, 255),
    'P.LANCE': (190, 140, 255), 'BLASTER': (150, 200, 255),
}

cards, caps = [], []
for line in open('/tmp/wpn_log.txt'):
    m = re.match(r'\[card\] (\d+) (.+)\|(.+)', line.rstrip())
    if m: cards.append((int(m.group(1)), m.group(2), m.group(3)))
    m = re.match(r'\[cap\] (\d+) (.+)', line.rstrip())
    if m: caps.append((int(m.group(1)), m.group(2)))

def cap_for(f):
    c = ""
    for fr, t in caps:
        if fr <= f: c = t
        else: break
    return c

out = '/tmp/wpn_cut'; os.makedirs(out, exist_ok=True)
for x in glob.glob(out + '/*.png'): os.remove(x)
frames = sorted(glob.glob('/tmp/movie/f_*.ppm'))
raw = open('/tmp/guide_audio.raw', 'rb').read()
SPF = 735 * 2                       # bytes of audio per frame
SIL = b'\x00\x00' * 735
seq = 0
audio = bytearray()
INTRO, CARD, OUTRO = 78, 40, 95

def emit(im):
    global seq
    im.convert('RGB').save(f'{out}/c_{seq:06d}.png'); seq += 1

def title_card(big, big_col, sub, tag, n):
    for _ in range(n):
        im = Image.new('RGB', (S, S), (6, 8, 14))
        d = ImageDraw.Draw(im)
        if tag:
            d.text((S // 2, 96), tag, anchor="mm", font=ftag,
                   fill=(110, 125, 150))
        d.text((S // 2, S // 2 - 18), big, anchor="mm", font=fbig,
               fill=big_col)
        d.text((S // 2, S // 2 + 44), sub, anchor="mm", font=fsub,
               fill=(180, 190, 205))
        emit(im)
        audio.extend(SIL)

# intro
for _ in range(INTRO):
    im = Image.new('RGB', (S, S), (6, 8, 14))
    d = ImageDraw.Draw(im)
    d.text((S // 2, S // 2 - 50), "THUMBY ELITE", anchor="mm",
           font=fbig, fill=(120, 200, 255))
    d.text((S // 2, S // 2 + 18), "THE ARMOURY", anchor="mm",
           font=F(40, True), fill=(255, 190, 110))
    d.text((S // 2, S // 2 + 70), "every weapon, field-tested",
           anchor="mm", font=fsub, fill=(180, 190, 205))
    emit(im)
    audio.extend(SIL)

ci = 0
for i, fp in enumerate(frames):
    while ci < len(cards) and cards[ci][0] <= i:
        _, name, sub = cards[ci]; ci += 1
        title_card(name, WCOL.get(name, (120, 200, 255)), sub,
                   "THE ARMOURY  %d / %d" % (ci, len(cards)), CARD)
    im = Image.open(fp).resize((S, S), Image.NEAREST).convert('RGBA')
    txt = cap_for(i)
    wr = textwrap.wrap(txt, width=34)[:2]
    if wr:
        lh = 30; h = len(wr) * lh + 20
        y0 = S - 6 - h
        strip = Image.new('RGBA', (S, h), (8, 12, 20, 205))
        im.alpha_composite(strip, (0, y0))
        d = ImageDraw.Draw(im); ty = y0 + 10
        for L in wr:
            d.text((16, ty), L, font=fcap, fill=(232, 238, 245)); ty += lh
    emit(im)
    audio.extend(raw[i * SPF:(i + 1) * SPF] or SIL)

# outro
for _ in range(OUTRO):
    im = Image.new('RGB', (S, S), (6, 8, 14))
    d = ImageDraw.Draw(im)
    d.text((S // 2, S // 2 - 30), "THUMBY ELITE", anchor="mm",
           font=fbig, fill=(120, 200, 255))
    d.text((S // 2, S // 2 + 34), "color.thumby.us", anchor="mm",
           font=fsub, fill=(180, 190, 205))
    d.text((S // 2, S // 2 + 70), "the infinite galaxy awaits",
           anchor="mm", font=fsub, fill=(180, 190, 205))
    emit(im)
    audio.extend(SIL)

wf = wave.open('/tmp/wpn_audio.wav', 'wb')
wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(22050)
wf.writeframes(bytes(audio)); wf.close()
print("frames", seq, "cards", len(cards), "audio samples",
      len(audio) // 2, "(%.1fs)" % (seq / 30.0))
