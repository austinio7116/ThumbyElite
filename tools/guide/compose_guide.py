from PIL import Image, ImageDraw, ImageFont
import glob, os, re, textwrap, wave
SC=4; S=128*SC
def F(sz,b=False):
    p='/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf' if b else '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'
    try: return ImageFont.truetype(p,sz)
    except: return ImageFont.load_default()
fcap=F(24,True); fbig=F(56,True); fsub=F(24); ftag=F(16,True)
caps=[]
for line in open('/tmp/guide_caps.txt'):
    m=re.match(r'\[cap\] (\d+) (.+)', line.rstrip())
    if m: caps.append((int(m.group(1)),m.group(2)))
def cap_for(f):
    c=""
    for fr,t in caps:
        if fr<=f: c=t
        else: break
    return c
def pos_for(t):
    # galaxy/system maps have their legend KEYS along the bottom edge,
    # so those captions must sit at the TOP, never covering the keys.
    for k in ("MARKET","SHIPYARD","OUTFITTING","MISSIONS","STATUS","GALAXY",
              "SYSTEM","PANEL","panel","REFUEL","CHART","layer","LAYER",
              "JUMP RANGE","range","RANGE","SURVEY","Pan the chart","star",
              "FUEL","fuel","destination"):
        if k in t: return "top"
    return "bottom"
out='/tmp/guide_cut'; os.makedirs(out,exist_ok=True)
for x in glob.glob(out+'/*.png'): os.remove(x)
frames=sorted(glob.glob('/tmp/movie/f_*.ppm')); seq=0
INTRO,OUTRO=78,95
def card(big,sub,n):
    global seq
    for _ in range(n):
        im=Image.new('RGB',(S,S),(6,8,14)); d=ImageDraw.Draw(im)
        y=S//2-60
        for L in big: d.text((S//2,y),L,anchor="mm",font=fbig,fill=(120,200,255)); y+=64
        y+=14
        for L in sub: d.text((S//2,y),L,anchor="mm",font=fsub,fill=(180,190,205)); y+=34
        im.save(f'{out}/c_{seq:06d}.png'); seq+=1
card(["THUMBY ELITE"],["a complete field guide","trade   fight   explore"],INTRO)
for i,fp in enumerate(frames):
    im=Image.open(fp).resize((S,S),Image.NEAREST).convert('RGBA')
    txt=cap_for(i); wr=textwrap.wrap(txt,width=34)[:2]
    if wr:
        lh=30; h=len(wr)*lh+20
        y0=6 if pos_for(txt)=="top" else S-6-h
        strip=Image.new('RGBA',(S,h),(8,12,20,205)); im.alpha_composite(strip,(0,y0))
        d=ImageDraw.Draw(im); ty=y0+10
        for L in wr: d.text((16,ty),L,font=fcap,fill=(232,238,245)); ty+=lh
    im.convert('RGB').save(f'{out}/c_{seq:06d}.png'); seq+=1
card(["THUMBY ELITE"],["color.thumby.us","the infinite galaxy awaits"],OUTRO)
raw=open('/tmp/guide_audio.raw','rb').read(); sil=b'\x00\x00'*735
full=sil*INTRO+raw+sil*OUTRO
wf=wave.open('/tmp/guide_audio.wav','wb'); wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(22050); wf.writeframes(full); wf.close()
print("frames",seq,"audio",len(full)//2)
