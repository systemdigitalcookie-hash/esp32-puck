# Approximate render of the v10 layout (same coordinates as build_ui / tk_build). Not pixel-exact LVGL.
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


SPR = Image.open("/workspace/sprites_preview.png")   # from tools_sprites.py (3x preview of 3x sprites)
def spr(k):  # crop sprite k from the preview (scale back to on-device size)
    im = SPR.resize((SPR.width // 3, SPR.height // 3), Image.NEAREST).crop((k * 40 + 5, 6, k * 40 + 35, 6 + (48 if k < 3 else 24)))
    return im
GRASS = (90, 160, 80)
def paste_spr(im, k, x, y):
    s = spr(k).convert("RGB"); m = Image.new("L", s.size, 0); px = s.load(); mp = m.load()
    for yy in range(s.height):
        for xx in range(s.width):
            if px[xx, yy] != GRASS: mp[xx, yy] = 255
    im.paste(s, (x, y), m)
def rec_ready():
    im, d = page(); ctext(d, 24, "Recorder", 20)
    d.ellipse((148, 58, 212, 122), fill=BG, outline=LINE); d.ellipse((167, 77, 193, 103), fill=RED)
    ctext(d, 128, "Tap to record", 12, GREY); div(d, 150, 236)
    for i, (n, t, dt) in enumerate([(3, "04:12", "19 Nov, 20:31"), (2, "00:48", "19 Nov, 18:02"), (1, "12:05", "18 Nov, 09:15")]):
        y = 151 + i * 46
        d.text((72, y + 6), f"Recording {n}", font=font(14), fill=TEXT)
        w = d.textlength(t, font=font(12)); d.text((288 - w, y + 8), t, font=font(12), fill=GREY)
        d.text((72, y + 26), dt, font=font(12), fill=GREY); d.ellipse((280, y + 30, 288, y + 38), outline=LINE)
        d.line((62, y + 45, 298, y + 45), fill=DIV)
    dots(d, 0); return im
def rec_running(paused):
    im, d = page()
    lbl = "Paused" if paused else "Recording"; f = font(16); w = d.textlength(lbl, font=f); x0 = 180 - (w + 18) / 2
    d.ellipse((x0, 34, x0 + 10, 44), fill=GREY if paused else RED); d.text((x0 + 18, 28), lbl, font=f, fill=TEXT)
    im.paste(hs if paused else hs_rec, (105, 52))
    ctext(d, 198, "00:42", 48, "#9B9A97" if paused else TEXT)
    if not paused: d.rounded_rectangle((110, 256, 250, 260), 2, fill=DIV); d.rounded_rectangle((110, 256, 200, 260), 2, fill=TEXT)
    btn(d, 122, 270, 104, 42, ("\u25b6  Resume" if paused else "II  Pause"), 21)
    d.rounded_rectangle((186, 270, 290, 312), 21, fill=BG, outline=LINE)
    d.rectangle((206, 285, 218, 297), fill=RED); d.text((226, 281), "Stop", font=font(14), fill=TEXT)
    dots(d, 0); return im
def rec_saving():
    im, d = page(); d.arc((152, 118, 208, 174), 0, 360, fill=DIV, width=4); d.arc((152, 118, 208, 174), 300, 60, fill=TEXT, width=4)
    ctext(d, 194, "Saving...", 16); dots(d, 0); return im
def rec_saved():
    im, d = page(); d.ellipse((148, 114, 212, 178), fill=BG, outline=LINE)
    d.line((166, 147, 177, 158), fill=TEXT, width=3); d.line((177, 158, 196, 136), fill=TEXT, width=3)
    ctext(d, 194, "Saved 0:42 to SD", 16); dots(d, 0); return im
def rec_error():
    im, d = page(); ctext(d, 98, "\u26a0", 28, RED); ctext(d, 146, "SD card full", 14, RED)
    btn(d, 126, 226, 96, 38, "Retry"); btn(d, 234, 226, 96, 38, "OK"); dots(d, 0); return im
def rec_detail():
    im, d = page(); d.ellipse((70, 22, 106, 58), fill=BG); ctext(d, 30, "<", 18, TEXT, 88)
    ctext(d, 28, "Recording 3", 20); ctext(d, 56, "19 Nov, 20:31", 12, GREY)
    d.ellipse((148, 84, 212, 148), fill=BG, outline=LINE); d.rectangle((169, 104, 175, 128), fill=TEXT); d.rectangle((185, 104, 191, 128), fill=TEXT)
    d.rounded_rectangle((80, 164, 280, 168), 2, fill=DIV); d.rounded_rectangle((80, 164, 120, 168), 2, fill=TEXT)
    ctext(d, 174, "00:51 / 04:12", 12, GREY)
    f = font(14)
    for k, (a, b) in enumerate([("Stored", "SD card"), ("Size", "7.7 MB")]):
        y = 200 + k * 30; aw = d.textlength(a, font=f); d.text((120 - aw / 2, y), a, font=f, fill=GREY)
        bw = d.textlength(b, font=f); d.text((286 - bw, y), b, font=f, fill=TEXT); div(d, y + 24)
    btn(d, 180, 270, 120, 34, "Delete", 8, BG, RED); return im
def game_field(score, cars):
    im = Image.new("RGB", (360, 360), GRASS); d = ImageDraw.Draw(im)
    for x, y in [(40, 120), (62, 200), (30, 250), (80, 90), (290, 110), (310, 190), (280, 260), (320, 230), (70, 290), (300, 300)]:
        d.rectangle((x, y, x + 5, y + 2), fill=(76, 142, 62))
    d.rectangle((105, 0, 254, 359), fill=(74, 74, 80))
    for y in range(-10, 360, 48):
        for x in (99, 255): d.rectangle((x, y, x + 5, y + 23), fill=(216, 74, 58)); d.rectangle((x, y + 24, x + 5, y + 47), fill=(244, 244, 244))
        for x in (153, 203): d.rectangle((x, y, x + 3, y + 23), fill=(242, 242, 242))
    for k, x, y in cars: paste_spr(im, k, x, y)
    paste_spr(im, 0, 165 + 20, 270)
    d.rounded_rectangle((148, 26, 212, 56), 6, fill=(30, 30, 34)); ctext(d, 29, str(score), 20, "#FFFFFF")
    return im, d
def game_idle():
    im, d = page(); ctext(d, 28, "Drive", 20)
    paste_spr(im, 0, 131, 74); paste_spr(im, 1, 171, 62); paste_spr(im, 3, 209, 110)
    d.rounded_rectangle((105, 150, 255, 204), 27, fill=TEXT); ctext(d, 165, "\u25b6  Start", 20, "#FFFFFF")
    ctext(d, 218, "Best  37", 16); ctext(d, 246, "Hold level, tap Start, tilt to steer", 12, GREY)
    ctext(d, 285, "Tilt: X", 12, GREY); dots(d, 3); return im
def game_play():
    im, d = game_field(14, [(1, 117, 60), (2, 215, 150), (3, 165, 230)]); dots(d, 3); return im
def game_over():
    im, d = game_field(23, [(1, 167, 252)])
    d.rounded_rectangle((70, 75, 290, 285), 14, fill=BG, outline=LINE)
    ctext(d, 89, "Game over", 16, GREY); ctext(d, 113, "23", 48); ctext(d, 171, "Best  37", 14)
    ctext(d, 217, "Tap to play again", 12, GREY); btn(d, 180, 241, 90, 30, "Menu"); dots(d, 3); return im

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
    rows = [("Sound", "Volume 90"), ("Microphone", "Gain 42 dB"), ("Display", "Brightness 70%"), ("Storage", "SD 29.7 GB free"),
            ("Wi-Fi", "HomeNet"), ("Battery", "85%"), ("About", "Firmware v11")]
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
    f = font(14); d.text((90, 92), "Volume", font=f, fill=GREY); d.text((252, 92), "90", font=f, fill=TEXT)
    d.rounded_rectangle((75, 128, 285, 134), 3, fill=DIV); d.rounded_rectangle((75, 128, 243, 134), 3, fill=TEXT)
    d.ellipse((233, 121, 253, 141), fill=BG, outline=LINE)
    btn(d, 180, 176, 150, 36, "Test speaker"); return im
def mic():
    im, d = page(); subhdr(d, "Microphone")
    f = font(14); d.text((88, 92), "Gain", font=f, fill=GREY); d.text((236, 92), "42 dB", font=f, fill=TEXT)
    d.rounded_rectangle((75, 128, 285, 134), 3, fill=DIV); d.rounded_rectangle((75, 128, 225, 134), 3, fill=TEXT)
    d.ellipse((215, 121, 235, 141), fill=BG, outline=LINE)
    ctext(d, 158, "Level", 12, GREY); d.rounded_rectangle((75, 178, 285, 184), 3, fill=DIV); d.rounded_rectangle((75, 178, 160, 184), 3, fill=TEXT)
    btn(d, 180, 200, 150, 36, "Mic test"); ctext(d, 248, "Records 3 s, then plays it back", 12, GREY); return im
def about():
    im, d = page(); subhdr(d, "About")
    f = font(14)
    for i, (k, v) in enumerate([("Firmware", "v11"), ("Built", "Nov 19 2026"), ("Device", "F412FA3C5B10")]):
        y = 76 + i * 34; kw = d.textlength(k, font=f); d.text((120 - kw / 2, y), k, font=f, fill=GREY)
        vw = d.textlength(v, font=f); d.text((298 - vw, y), v, font=f, fill=TEXT); div(d, y + 26, 236)
    btn(d, 180, 186, 150, 36, "Check update"); ctext(d, 232, "Up to date", 12, GREY); return im

def bars(d, x, y, lvl):
    for i in range(4):
        h = 4 + i * 3; d.rectangle((x + i * 5, y + 13 - h, x + i * 5 + 2, y + 13), fill=TEXT if i < lvl else DIV)
def lock(d, x, y):
    d.arc((x + 1, y, x + 9, y + 9), 180, 360, fill=GREY, width=2); d.rounded_rectangle((x, y + 5, x + 10, y + 13), 2, fill=GREY)
def wifi_list():
    im, d = page(); subhdr(d, "Wi-Fi")
    ctext(d, 70, "HomeNet", 16); ctext(d, 92, "192.168.1.23  |  -54 dBm", 12, GREY)
    btn(d, 180, 112, 76, 26, "\u21bb Scan", 8, BG, TEXT, 12)
    d.text((67, 150), "Networks", font=font(12), fill=GREY)
    for i, (n, l, sec, tag) in enumerate([("HomeNet", 4, True, "Connected"), ("HomeNet_5G", 3, True, "Saved"), ("Cafe Free WiFi", 2, False, ""), ("TM-Unifi-4410", 1, True, "")]):
        y = 168 + i * 40
        bars(d, 67, y + 13, l); d.text((95, y + (5 if tag else 12)), n, font=font(14), fill=TEXT)
        if tag: d.text((95, y + 23), tag, font=font(12), fill=GREY)
        if sec: lock(d, 279, y + 13)
        d.line((55, y + 39, 305, y + 39), fill=DIV)
    return im
def wifi_kb():
    im, d = page(); ctext(d, 24, "TM-Unifi-4410", 16)
    d.rounded_rectangle((63, 54, 253, 94), 8, outline=LINE); d.text((75, 64), "\u2022\u2022\u2022\u2022\u2022\u2022", font=font(16), fill=TEXT)
    btn(d, 280, 54, 40, 40, "o")
    d.rounded_rectangle((38, 102, 322, 252), 10, fill="#F7F6F3")
    rows = ["1# qwertyuiop <", "ABC asdfghjkl \u21b5", "_ - zxcvbnm . , :", "x < space > \u2713"]
    for r, row in enumerate(rows):
        keys = row.split(" ") if r == 3 else list(row.replace(" ", ""))
        if r == 0: keys = ["1#"] + list("qwertyuiop") + ["<"]
        if r == 1: keys = ["ABC"] + list("asdfghjkl") + ["\u21b5"]
        if r == 2: keys = ["_", "-"] + list("zxcvbnm") + [".", ",", ":"]
        kw = (284 - 6 - 3 * (len(keys) - 1)) / len(keys); y = 105 + r * 36.5
        for k, key in enumerate(keys):
            x = 41 + k * (kw + 3); d.rounded_rectangle((x, y, x + kw, y + 33), 5, fill=BG, outline=DIV)
            f = font(12); w = d.textlength(key, font=f); d.text((x + kw / 2 - w / 2, y + 10), key, font=f, fill=TEXT)
    btn(d, 126, 262, 96, 36, "Cancel"); d.rounded_rectangle((186, 262, 282, 298), 8, fill=TEXT); ctext(d, 271, "Connect", 14, BG, 234)
    return im
def wifi_conn(ok):
    im, d = page()
    if ok is None:
        d.arc((155, 104, 205, 154), 0, 360, fill=DIV, width=4); d.arc((155, 104, 205, 154), 300, 60, fill=TEXT, width=4)
        ctext(d, 172, "Connecting to\nTM-Unifi-4410...", 14)
    else:
        ctext(d, 112, "\u2713" if ok else "\u26a0", 28, TEXT if ok else RED)
        ctext(d, 172, "Connected to TM-Unifi-4410" if ok else "Wrong password.\nBack on HomeNet", 14, TEXT if ok else RED)
        btn(d, 180, 240, 96, 36, "OK")
    return im
panels = [("1 Recorder: ready + recent list", rec_ready()), ("1 Recorder: recording", rec_running(False)),
          ("1 Recorder: paused (timer blinks)", rec_running(True)), ("1 Recorder: saving", rec_saving()),
          ("1 Recorder: saved (2 s)", rec_saved()), ("1 Recorder: error", rec_error()), ("1 Recording detail", rec_detail()),
          ("2 Tauke (idle)", tauke(True, "idle")), ("2 Tauke - waiting", tauke(True, "think")), ("3 Clock", clock()),
          ("4 Drive: start", game_idle()), ("4 Drive: playing", game_play()), ("4 Drive: game over", game_over()),
          ("5 Settings", settings()), ("5 Settings > Wi-Fi", wifi_list()), ("Wi-Fi: password", wifi_kb()), ("Wi-Fi: connecting", wifi_conn(None)), ("Wi-Fi: failed -> fallback", wifi_conn(False)), ("5 Settings > About", about())]
cols = 4; W = 400; H = 430
sheet = Image.new("RGB", (cols * W, ((len(panels) + cols - 1) // cols) * H), "#2b2b2b")
mask = Image.new("L", (360, 360), 0); ImageDraw.Draw(mask).ellipse((0, 0, 359, 359), fill=255)
sd = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(panels):
    x, y = (i % cols) * W + 20, (i // cols) * H + 20
    sheet.paste(im, (x, y), mask)
    sd.text((x, y + 372), name, font=font(16), fill="#e0e0e0")
sheet.save("layout_mock_v11.png"); print("ok", sheet.size)
