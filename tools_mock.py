# Approximate render of the v7 layout (same coordinates as build_ui / tk_build). Not pixel-exact LVGL.
from PIL import Image, ImageDraw, ImageFont
F = "/usr/share/fonts/truetype/sand-box/google/Montserrat/Montserrat-VariableFont_wght.ttf"
def font(sz):
    f = ImageFont.truetype(F, sz)
    try: f.set_variation_by_axes([500])
    except Exception: pass
    return f
BG, TEXT, GREY, DIV, LINE, RED, PRESS = "#FFFFFF", "#37352F", "#787774", "#E9E9E7", "#DCDBD8", "#E03E3E", "#EFEFED"
hs = Image.open("headset_preview.png").crop((0, 0, 150, 150))
hs_rec = Image.open("headset_preview.png").crop((150 * 3, 150, 150 * 4, 300))
def page():
    im = Image.new("RGB", (360, 360), BG); return im, ImageDraw.Draw(im)
def ctext(d, y, s, sz, col=TEXT, cx=180):
    f = font(sz)
    for i, line in enumerate(s.split("\n")):
        w = d.textlength(line, font=f); d.text((cx - w / 2, y + i * (sz + 4)), line, font=f, fill=col)
def btn(d, cx, y, w, h, label, r=8, fill=BG, tcol=TEXT, sz=14):
    d.rounded_rectangle((cx - w / 2, y, cx + w / 2, y + h), r, fill=fill, outline=LINE, width=1)
    if label: ctext(d, y + (h - sz) / 2 - 2, label, sz, tcol, cx)
def dots(d, active):
    for i in range(5):
        x = 180 + (i - 2) * 14; d.ellipse((x - 3, 342, x + 3, 348), fill=TEXT if i == active else LINE)
def div(d, y, w=220): d.line((180 - w / 2, y, 180 + w / 2, y), fill=DIV, width=1)

def recorder(recording):
    im, d = page(); ctext(d, 28, "Recorder", 20)
    im.paste(hs_rec if recording else hs, (105, 60))
    ctext(d, 212, "12:34" if recording else "00:00", 28, TEXT if recording else GREY)
    if recording:
        d.rounded_rectangle((125, 250, 235, 253), 2, fill=DIV); d.rounded_rectangle((125, 250, 190, 253), 2, fill=TEXT)
    d.ellipse((153, 260, 207, 314), fill=BG, outline=LINE)
    if recording: d.rounded_rectangle((171, 278, 189, 296), 3, fill=RED)
    else: d.ellipse((169, 276, 191, 298), fill=RED)
    ctext(d, 320, "2h41m left", 12, GREY); dots(d, 0); return im

def tauke(real, state):
    im, d = page()
    ctext(d, 28, "Tauke", 20)
    fx, fy, fw, fh = 180, 66, 184, 150
    d.rounded_rectangle((fx - fw / 2, fy, fx + fw / 2, fy + fh), 72, fill="#FFFFFF", outline=LINE, width=2)
    cy = fy + fh / 2 - 2; dx = 14 if state == "think" else 0; dy = -6 if state == "think" else 0; h = 50 if state == "think" else 56; w = 26 if state == "think" else 28
    for s in (-1, 1):
        ex = fx + s * 34 + dx; ey = cy + dy
        d.ellipse((ex - w / 2, ey - h / 2, ex + w / 2, ey + h / 2), fill="#111111")
        if real: d.rounded_rectangle((ex + s * 3 - 14, ey - h / 2 - 15, ex + s * 3 + 14, ey - h / 2 - 10), 3, fill="#111111")
    if state == "think": ctext(d, 226, "Asking Tauke... 12s\nwhat's on my Notion today?", 14, GREY); btn(d, 180, 280, 150, 40, "Tap to cancel", 20)
    elif state == "speak": ctext(d, 226, "Boss, two client calls today and\nthe proposal is due.", 14, TEXT); btn(d, 180, 280, 150, 40, "Tap to stop", 20)
    else: ctext(d, 226, "", 14); btn(d, 180, 280, 150, 40, "Hold to talk", 20)
    dots(d, 1); return im

def clock():
    im, d = page(); ctext(d, 122, "20:41", 48); ctext(d, 186, "Thu, 19 Nov 2026", 16); ctext(d, 212, "Kuala Lumpur", 12, GREY); dots(d, 2); return im

def recordings():
    im, d = page(); ctext(d, 28, "Recordings", 20); div(d, 62)
    for i, (n, t) in enumerate([(3, "4m12s"), (2, "0m48s"), (1, "12m05s")]):
        y = 66 + i * 44; f = font(14)
        d.text((72, y + 13), f"Recording {n}    {t}", font=f, fill=TEXT); d.line((62, y + 43, 298, y + 43), fill=DIV)
    dots(d, 3); return im

def status():
    im, d = page(); ctext(d, 28, "Status", 20); div(d, 62)
    rows = [("Battery", "USB power"), ("Wi-Fi", "Yang_Home"), ("Storage", "Flash, 2h41m"), ("Mic", "OK"), ("Firmware", "v7")]
    f = font(14)
    for i, (k, v) in enumerate(rows):
        y = 70 + i * 34
        kw = d.textlength(k, font=f); d.text((124 - kw / 2, y), k, font=f, fill=GREY)
        vw = d.textlength(v, font=f); d.text((291 - vw, y), v, font=f, fill=TEXT)
        if i < 4: div(d, y + 26, 230)
    btn(d, 180, 250, 150, 34, "Check update"); ctext(d, 292, "Up to date", 12, GREY); dots(d, 4); return im


ICON = {"Sound": "\u266a", "Microphone": "\u25cf", "Display": "\u25d0", "Storage": "\u25a4", "Wi-Fi": "\u2248", "Battery": "\u25ae", "About": "\u2699"}
SYM = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15)
def settings():
    im, d = page(); ctext(d, 28, "Settings", 20); div(d, 62)
    rows = [("Sound", "Volume 80"), ("Microphone", "Gain 30 dB"), ("Display", "Brightness 70%"), ("Storage", "SD 29.7 GB free"),
            ("Wi-Fi", "HomeNet"), ("Battery", "85%"), ("About", "Firmware v8")]
    for i, (t, sm) in enumerate(rows[:5]):
        y = 64 + i * 52
        d.text((55 + 14, y + 17), ICON[t], font=SYM, fill=GREY)
        d.text((55 + 48, y + 8), t, font=font(14), fill=TEXT); d.text((55 + 48, y + 28), sm, font=font(12), fill=GREY)
        d.text((55 + 250 - 18, y + 18), ">", font=font(12), fill=LINE)
        d.line((55, y + 51, 305, y + 51), fill=DIV)
    dots(d, 4); return im
def subhdr(d, title):
    d.ellipse((70, 22, 106, 58), fill=BG); ctext(d, 30, "<", 18, TEXT, 88); ctext(d, 28, title, 20); div(d, 62)
def sound():
    im, d = page(); subhdr(d, "Sound")
    f = font(14); d.text((90, 92), "Volume", font=f, fill=GREY); d.text((252, 92), "80", font=f, fill=TEXT)
    d.rounded_rectangle((75, 128, 285, 134), 3, fill=DIV); d.rounded_rectangle((75, 128, 243, 134), 3, fill=TEXT)
    d.ellipse((233, 121, 253, 141), fill=BG, outline=LINE)
    btn(d, 180, 176, 150, 36, "Test speaker"); return im
def mic():
    im, d = page(); subhdr(d, "Microphone")
    f = font(14); d.text((88, 92), "Gain", font=f, fill=GREY); d.text((236, 92), "30 dB", font=f, fill=TEXT)
    d.rounded_rectangle((75, 128, 285, 134), 3, fill=DIV); d.rounded_rectangle((75, 128, 225, 134), 3, fill=TEXT)
    d.ellipse((215, 121, 235, 141), fill=BG, outline=LINE)
    ctext(d, 158, "Level", 12, GREY); d.rounded_rectangle((75, 178, 285, 184), 3, fill=DIV); d.rounded_rectangle((75, 178, 160, 184), 3, fill=TEXT)
    btn(d, 180, 200, 150, 36, "Mic test"); ctext(d, 248, "Records 3 s, then plays it back", 12, GREY); return im
def about():
    im, d = page(); subhdr(d, "About")
    f = font(14)
    for i, (k, v) in enumerate([("Firmware", "v8"), ("Built", "Nov 19 2026"), ("Device", "F412FA3C5B10")]):
        y = 76 + i * 34; kw = d.textlength(k, font=f); d.text((120 - kw / 2, y), k, font=f, fill=GREY)
        vw = d.textlength(v, font=f); d.text((298 - vw, y), v, font=f, fill=TEXT); div(d, y + 26, 236)
    btn(d, 180, 186, 150, 36, "Check update"); ctext(d, 232, "Up to date", 12, GREY); return im
panels = [("1 Recorder (idle)", recorder(False)), ("1 Recorder (recording)", recorder(True)), ("2 Tauke (idle)", tauke(True, "idle")),
          ("2 Tauke - waiting", tauke(True, "think")), ("2 Tauke - speaking", tauke(True, "speak")), ("3 Clock", clock()),
          ("4 Recordings", recordings()), ("5 Settings", settings()), ("5 Settings > Sound", sound()), ("5 Settings > Microphone", mic()),
          ("5 Settings > About", about())]
cols = 4; W = 400; H = 430
sheet = Image.new("RGB", (cols * W, ((len(panels) + cols - 1) // cols) * H), "#2b2b2b")
mask = Image.new("L", (360, 360), 0); ImageDraw.Draw(mask).ellipse((0, 0, 359, 359), fill=255)
sd = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(panels):
    x, y = (i % cols) * W + 20, (i // cols) * H + 20
    sheet.paste(im, (x, y), mask)
    sd.text((x, y + 372), name, font=font(16), fill="#e0e0e0")
sheet.save("layout_mock_v8.png"); print("ok", sheet.size)
