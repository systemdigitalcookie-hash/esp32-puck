// ================= Tauke voice page (included once from puck_ota.ino) =================
// Hold-to-talk -> 16 kHz PCM POST to the Tauke server (secrets.h: TAUKE_URL, TAUKE_TOKEN; gitignored)
// -> reply PCM played through ES8311 + speaker amp. Face drawn with LVGL shapes.
#pragma once
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#include <HTTPClient.h>
#include "esp_heap_caps.h"

#ifdef TAUKE_TOKEN
// Token is stored XOR-scrambled so it is not a plain string in the published firmware.bin.
// (Light obfuscation only; the server also rate-limits and can allow-list device ids.)
template <size_t N> struct TkX { char d[N]; constexpr TkX(const char (&s)[N]) : d{} { for (size_t i = 0; i < N; i++) d[i] = s[i] ^ (char)(0x5A + i * 7); } };
static constexpr TkX<sizeof(TAUKE_TOKEN)> tk_tok_x(TAUKE_TOKEN);
static String tk_token() { String o; for (size_t i = 0; i + 1 < sizeof(TAUKE_TOKEN); i++) o += (char)(tk_tok_x.d[i] ^ (char)(0x5A + i * 7)); return o; }
#endif

enum { TK_IDLE = 0, TK_LISTEN, TK_THINK, TK_SPEAK, TK_ERROR };
#define TK_MIC_MAX (16000 * 20)   // 20 s max question
#define TK_OUT_MAX (16000 * 45)   // 45 s max answer
static volatile int tk_state = TK_IDLE;
static int16_t *tk_mic = NULL, *tk_out = NULL;
static volatile uint32_t tk_mic_n = 0, tk_out_n = 0;
static volatile bool tk_net_done = false;
static char tk_err[72], tk_reply[640], tk_heard[240];
static lv_obj_t *tk_face, *tk_eye[2], *tk_btn, *tk_btn_lbl, *tk_cap_box, *tk_cap, *tk_title;
static uint32_t tk_t0 = 0, tk_err_until = 0, tk_next_blink = 3000;

static void tk_urldecode(const String &in, char *out, size_t n) {
  size_t o = 0;
  for (size_t i = 0; i < in.length() && o + 1 < n; i++) {
    char c = in[i];
    if (c == '%' && i + 2 < in.length()) { char hx[3] = {in[i + 1], in[i + 2], 0}; c = (char)strtol(hx, NULL, 16); i += 2; }
    else if (c == '+') c = ' ';
    // the built-in fonts are ASCII only: map common UTF-8 punctuation, drop the rest
    if ((uint8_t)c >= 0x80) {
      if ((uint8_t)c == 0xE2 && i + 6 <= in.length()) {   // %E2%80%99 etc.
        String rest = in.substring(i + 1, i + 7);
        if (rest == "%80%99" || rest == "%80%98") c = '\''; else if (rest == "%80%9C" || rest == "%80%9D") c = '"';
        else if (rest == "%80%94" || rest == "%80%93") c = '-'; else if (rest == "%80%A6") c = '.'; else c = 0;
        i += 6;
      } else c = 0;
      if (!c) continue;
    }
    out[o++] = c;
  }
  out[o] = 0;
}

static void tk_net_task(void *) {
  tk_err[0] = 0; tk_out_n = 0; tk_reply[0] = 0; tk_heard[0] = 0;
#ifndef TAUKE_URL
  snprintf(tk_err, sizeof tk_err, "Not configured (no secrets.h)");
#else
  if (WiFi.status() != WL_CONNECTED) snprintf(tk_err, sizeof tk_err, "No Wi-Fi. Join AP Puck-Setup");
  else if (time(nullptr) < 1700000000) snprintf(tk_err, sizeof tk_err, "Clock not synced yet, try again");
  else {
    WiFiClientSecure c; c.setCACert(GH_ROOT_CAS);   // Railway uses Let's Encrypt (ISRG roots are pinned)
    HTTPClient h; h.setTimeout(45000); h.setConnectTimeout(12000);
    if (!h.begin(c, TAUKE_URL)) snprintf(tk_err, sizeof tk_err, "Bad server URL");
    else {
      h.addHeader("Authorization", String("Bearer ") + tk_token());
      h.addHeader("Content-Type", "audio/pcm");
      h.addHeader("X-Device-Id", WiFi.macAddress());
      const char *keys[] = {"X-Transcript", "X-Reply", "X-Error"};
      h.collectHeaders(keys, 3);
      int code = h.sendRequest("POST", (uint8_t *)tk_mic, tk_mic_n * 2);
      if (code == 200) {
        tk_urldecode(h.header("X-Transcript"), tk_heard, sizeof tk_heard);
        tk_urldecode(h.header("X-Reply"), tk_reply, sizeof tk_reply);
        int len = h.getSize();
        WiFiClient *s = h.getStreamPtr();
        uint8_t *dst = (uint8_t *)tk_out; uint32_t maxb = TK_OUT_MAX * 2, got = 0, t = millis();
        while ((h.connected() || s->available()) && (len < 0 || got < (uint32_t)len) && got < maxb && millis() - t < 20000) {
          int av = s->available();
          if (av <= 0) { delay(5); continue; }
          int r = s->read(dst + got, min<uint32_t>(av, maxb - got));
          if (r > 0) { got += r; t = millis(); }
        }
        tk_out_n = got / 2;
        if (!tk_out_n) snprintf(tk_err, sizeof tk_err, "Reply had no audio");
      } else if (code < 0) {
        snprintf(tk_err, sizeof tk_err, "Server unreachable: %s", h.errorToString(code).c_str());
      } else {
        String e = h.header("X-Error");
        snprintf(tk_err, sizeof tk_err, "Server error %d%s%s", code, e.length() ? ": " : "", e.c_str());
      }
      h.end();
    }
  }
#endif
  tk_net_done = true;
  vTaskDelete(NULL);
}

static void tk_set_caption(const char *txt, uint32_t color) {
  lv_label_set_text(tk_cap, txt);
  lv_obj_set_style_text_color(tk_cap, lv_color_hex(color), 0);
  lv_obj_scroll_to_y(tk_cap_box, 0, LV_ANIM_OFF);
}

static void tk_go(int st) {
  tk_state = st; tk_t0 = millis();
  static const char *titles[] = {"Tauke", "Listening...", "Thinking...", "Tauke", "Tauke"};
  lv_label_set_text(tk_title, titles[st]);
  lv_label_set_text(tk_btn_lbl, st == TK_LISTEN ? "Release to send" : (st == TK_SPEAK ? "Tap to stop" : (st == TK_THINK ? "..." : "Hold to talk")));
  lv_obj_set_style_bg_color(tk_btn, lv_color_hex(st == TK_LISTEN ? 0x30a060 : (st == TK_THINK ? 0x3a4654 : 0x2a6fdb)), 0);
  if (st == TK_LISTEN) { lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLLABLE); }   // no page swipes while talking
  else if (rec.state == REC_IDLE) lv_obj_add_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
}

static void tk_error(const char *m) {
  strncpy(tk_err, m, sizeof tk_err - 1); tk_err[sizeof tk_err - 1] = 0;
  tk_set_caption(tk_err, 0xff7070);
  tk_err_until = millis() + 4000;
  tk_go(TK_ERROR);
}

static void tk_send() {
  uint32_t n = talk_cap_stop();
  if (n < 16000 * 4 / 10) { tk_error("Hold the button while you talk"); return; }
  tk_mic_n = n; tk_net_done = false;
  tk_set_caption("...", 0x9fb0c0);
  tk_go(TK_THINK);
  if (xTaskCreate(tk_net_task, "tk_net", 12288, NULL, 2, NULL) != pdPASS) tk_error("Out of memory");
}

static void tk_btn_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (tk_state == TK_SPEAK) { talk_play_stop(); return; }
    if (tk_state == TK_THINK || tk_state == TK_LISTEN) return;
#ifndef TAUKE_URL
    tk_error("Not configured (no secrets.h)"); return;
#endif
    if (!tk_mic || !tk_out) { tk_error("No PSRAM buffer"); return; }
    if (rec.state != REC_IDLE) { tk_error("Stop the recorder first"); return; }
    if (rec_playing) { tk_error("Stop playback first"); return; }
    if (WiFi.status() != WL_CONNECTED) { tk_error("No Wi-Fi. Join AP Puck-Setup"); return; }
    if (!talk_cap_start(tk_mic, TK_MIC_MAX)) { tk_error("Mic start failed"); return; }
    tk_set_caption("I'm listening, boss", 0x9fb0c0);
    tk_go(TK_LISTEN);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (tk_state == TK_LISTEN) tk_send();
  }
}

static void tk_build(lv_obj_t *t) {
  tk_mic = (int16_t *)heap_caps_malloc(TK_MIC_MAX * 2, MALLOC_CAP_SPIRAM);
  tk_out = (int16_t *)heap_caps_malloc(TK_OUT_MAX * 2, MALLOC_CAP_SPIRAM);
  lv_obj_set_style_bg_color(t, lv_color_hex(0x0b0f14), 0); lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

  tk_title = lv_label_create(t); lv_obj_set_style_text_font(tk_title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(tk_title, lv_color_hex(0xc8d4e0), 0);
  lv_label_set_text(tk_title, "Tauke"); lv_obj_align(tk_title, LV_ALIGN_TOP_MID, 0, 22);

  tk_face = lv_obj_create(t); lv_obj_remove_style_all(tk_face);
  lv_obj_set_size(tk_face, 200, 164); lv_obj_align(tk_face, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_radius(tk_face, 78, 0);
  lv_obj_set_style_bg_opa(tk_face, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(tk_face, lv_color_hex(0xf4f4f2), 0);
  lv_obj_clear_flag(tk_face, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 2; i++) {
    tk_eye[i] = lv_obj_create(tk_face); lv_obj_remove_style_all(tk_eye[i]);
    lv_obj_set_style_radius(tk_eye[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(tk_eye[i], LV_OPA_COVER, 0); lv_obj_set_style_bg_color(tk_eye[i], lv_color_hex(0x111111), 0);
    lv_obj_clear_flag(tk_eye[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(tk_eye[i], 28, 58); lv_obj_align(tk_eye[i], LV_ALIGN_CENTER, i ? 36 : -36, -4);
  }

  tk_cap_box = lv_obj_create(t); lv_obj_remove_style_all(tk_cap_box);
  lv_obj_set_size(tk_cap_box, 264, 56); lv_obj_align(tk_cap_box, LV_ALIGN_TOP_MID, 0, 220);
  lv_obj_set_scrollbar_mode(tk_cap_box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(tk_cap_box, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  tk_cap = lv_label_create(tk_cap_box); lv_obj_set_width(tk_cap, 264);
  lv_label_set_long_mode(tk_cap, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(tk_cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(tk_cap, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(tk_cap, lv_color_hex(0x9fb0c0), 0);
#ifdef TAUKE_URL
  lv_label_set_text(tk_cap, "Hold the button and talk to Tauke");
#else
  lv_label_set_text(tk_cap, "Tauke not configured in this build");
#endif

  tk_btn = lv_btn_create(t); lv_obj_set_size(tk_btn, 150, 42); lv_obj_align(tk_btn, LV_ALIGN_TOP_MID, 0, 282);
  lv_obj_set_style_radius(tk_btn, 21, 0);
  lv_obj_set_style_bg_color(tk_btn, lv_color_hex(0x2a6fdb), 0);
  lv_obj_clear_flag(tk_btn, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_add_event_cb(tk_btn, tk_btn_cb, LV_EVENT_ALL, NULL);
  tk_btn_lbl = lv_label_create(tk_btn); lv_obj_set_style_text_font(tk_btn_lbl, &lv_font_montserrat_16, 0);
  lv_label_set_text(tk_btn_lbl, "Hold to talk"); lv_obj_center(tk_btn_lbl);
}

// eyes: w/h size, dx/dy offset from rest position
static void tk_eyes(int w, int h, int dx, int dy) {
  for (int i = 0; i < 2; i++) {
    lv_obj_set_size(tk_eye[i], w, h);
    lv_obj_align(tk_eye[i], LV_ALIGN_CENTER, (i ? 36 : -36) + dx, -4 + dy);
  }
}

// ~30 Hz: state machine + face animation (UI task)
static void tk_tick(bool visible) {
  uint32_t now = millis(), t = now - tk_t0;
  int st = tk_state;
  if (st == TK_LISTEN && talk_cap_n >= TK_MIC_MAX) tk_send();   // buffer full: send what we have
  if (st == TK_THINK && tk_net_done) {
    if (tk_err[0]) { char m[72]; strcpy(m, tk_err); tk_error(m); }
    else {
      tk_set_caption(tk_reply[0] ? tk_reply : "(no text)", 0xe8eef4);
      if (talk_play_start(tk_out, tk_out_n)) tk_go(TK_SPEAK);
      else { tk_err_until = now + 6000; tk_go(TK_ERROR); lv_label_set_text(tk_title, "Speaker failed"); }
    }
  }
  if (st == TK_SPEAK && !talk_playing) tk_go(TK_IDLE);
  if (st == TK_ERROR && now > tk_err_until) tk_go(TK_IDLE);
  if (!visible) return;
  st = tk_state; t = now - tk_t0;
  int lvl = talk_level;
  lv_obj_set_style_translate_y(tk_face, 0, 0);
  switch (st) {
    case TK_IDLE: {
      int h = 58;
      if (now > tk_next_blink) { if (now > tk_next_blink + 140) tk_next_blink = now + 2500 + (esp_random() % 3000); else h = 6; }
      int dx = (int)(3 * sinf(now / 1700.0f));
      tk_eyes(28, h, dx, 0);
      break;
    }
    case TK_LISTEN:
      tk_eyes(32, 70 + lvl / 8, 0, -14);
      break;
    case TK_THINK:
      tk_eyes(26, 52, (int)(18 * sinf(t / 260.0f)), -6);
      break;
    case TK_SPEAK: {
      int h = 58 - lvl * 26 / 100;
      tk_eyes(30 + lvl / 12, h, 0, 2);
      lv_obj_set_style_translate_y(tk_face, (int)(4 * sinf(t / 110.0f)), 0);
      if (tk_out_n) {   // scroll long captions along with playback
        lv_coord_t over = lv_obj_get_height(tk_cap) - lv_obj_get_height(tk_cap_box);
        if (over > 0) lv_obj_scroll_to_y(tk_cap_box, (lv_coord_t)((int64_t)over * talk_play_pos / tk_out_n), LV_ANIM_OFF);
      }
      break;
    }
    default:   // error: squint
      tk_eyes(30, 14, 0, 6);
      break;
  }
}
