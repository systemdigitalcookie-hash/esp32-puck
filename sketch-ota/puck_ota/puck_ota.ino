// Demo + Wi-Fi captive portal (WiFiManager) + HTTPS OTA pull. Prototype.
// Version shown on screen. Updates: GitHub releases/latest/download/{version.txt,firmware.bin}.
#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include "recorder.h"
#if __has_include("headset_img.h")
#include "headset_img.h"   // personal asset (gitignored): animated headset character, LVGL indexed 2-bit frames
#define HAVE_HEADSET 1
#endif
#include "ui_style.h"

#include "version.h"
#include "certs.h"
#define _S(x) #x
#define S(x) _S(x)
#define FW_STR S(FW_VERSION)
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
// Server hosts: <base>/version.txt (integer) and <base>/firmware.bin (app-only .ino.bin)
#define OTA_REPO "systemdigitalcookie-hash/esp32-puck"
#define OTA_BASE "https://github.com/" OTA_REPO "/releases/latest/download"

static char status_txt[96] = "v" FW_STR " booting";
static volatile bool want_check = false;
static lv_obj_t *lbl_time, *lbl_stat, *bar, *rec_btn, *rec_icon, *hs_img = NULL;
static int hs_frame = 0;
static lv_obj_t *tv, *dots[5], *lbl_ver_small, *lbl_clock, *lbl_date, *lbl_tsrc, *lbl_info, *rec_list_obj, *lbl_listhdr;
static lv_obj_t *play_box = NULL;
#define NPAGES 5   // 0 Recorder, 1 Tauke, 2 Clock, 3 Recordings, 4 Settings
#define PG_LIST 3
#define PG_STATUS 4
#include "tauke_ui.h"

static void btn_cb(lv_event_t *e) { want_check = true; }

static void net_task(void *) {
  WiFiManager wm;                       // saved creds reused; else AP "Puck-Setup" portal
  wm.setConfigPortalTimeout(180);
  snprintf(status_txt, sizeof status_txt, "WiFi: join AP Puck-Setup");
  if (!wm.autoConnect("Puck-Setup")) { snprintf(status_txt, sizeof status_txt, "WiFi failed"); vTaskDelete(NULL); }
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // cert validation needs correct time
  for (int i = 0; i < 40 && time(nullptr) < 1700000000; i++) delay(250);
  snprintf(status_txt, sizeof status_txt, "%s", WiFi.localIP().toString().c_str());
  for (;;) {
    if (want_check) {
      want_check = false;
      if (rec.state != REC_IDLE || tk_state != TK_IDLE) { snprintf(status_txt, sizeof status_txt, "stop recording / Tauke first"); continue; }
      WiFiClientSecure c; c.setCACert(GH_ROOT_CAS);   // TLS validated against pinned roots
      HTTPClient h; h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); h.begin(c, String(OTA_BASE) + "/version.txt");
      int code = h.GET(); int remote = code == 200 ? h.getString().toInt() : -1; h.end();
      if (remote > FW_VERSION) {
        snprintf(status_txt, sizeof status_txt, "updating to v%d...", remote);
        httpUpdate.rebootOnUpdate(true);
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        httpUpdate.update(c, String(OTA_BASE) + "/firmware.bin");
        snprintf(status_txt, sizeof status_txt, "update failed");
      } else if (code == 200) snprintf(status_txt, sizeof status_txt, "Up to date");
      else snprintf(status_txt, sizeof status_txt, "Update check failed (%d)", code);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

static void rec_cb(lv_event_t *e) {
  if (tk_state != TK_IDLE && tk_state != TK_ERROR) return;   // Tauke is using the mic/speaker
  if (rec.state == REC_IDLE) rec_start();
  else if (rec.state == REC_RECORDING) rec_stop();
}


static void fmt_dur(char *out, size_t n, uint32_t s) {
  if (s >= 3600) snprintf(out, n, "%luh%02lum", (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
  else snprintf(out, n, "%lum%02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// ================= RTC (PCF85063 @0x51), local time MYT-8 =================
#define TZ_MYT "MYT-8"
#define RTC_ADDR 0x51
#define EPOCH_2026 1767225600UL
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
static void ensure_tz() {
  const char *tz = getenv("TZ");
  if (!tz || strcmp(tz, TZ_MYT)) { setenv("TZ", TZ_MYT, 1); tzset(); }   // net_task's configTime(0,0) resets TZ; re-apply
}
static bool rtc_read_epoch(time_t *out) {
  Wire.beginTransmission(RTC_ADDR); Wire.write(0x04);
  if (Wire.endTransmission(true)) return false;
  if (Wire.requestFrom((uint8_t)RTC_ADDR, (uint8_t)7) != 7) return false;
  uint8_t r[7]; for (int i = 0; i < 7; i++) r[i] = Wire.read();
  if (r[0] & 0x80) return false;                    // oscillator-stop flag: time not trustworthy
  struct tm t = {};
  t.tm_sec = unbcd(r[0] & 0x7F); t.tm_min = unbcd(r[1] & 0x7F); t.tm_hour = unbcd(r[2] & 0x3F);
  t.tm_mday = unbcd(r[3] & 0x3F); t.tm_mon = unbcd(r[5] & 0x1F) - 1; t.tm_year = 100 + unbcd(r[6]);
  ensure_tz(); t.tm_isdst = 0;
  time_t e = mktime(&t);
  if (e < (time_t)EPOCH_2026) return false;
  *out = e; return true;
}
static void rtc_write_epoch(time_t e) {
  ensure_tz();
  struct tm t; localtime_r(&e, &t);
  Wire.beginTransmission(RTC_ADDR); Wire.write(0x04);
  Wire.write(bcd(t.tm_sec)); Wire.write(bcd(t.tm_min)); Wire.write(bcd(t.tm_hour)); Wire.write(bcd(t.tm_mday));
  Wire.write(t.tm_wday & 7); Wire.write(bcd(t.tm_mon + 1)); Wire.write(bcd(t.tm_year % 100));
  Wire.endTransmission(true);
}
static const char *time_src = "none";

static void time_tick() {
  ensure_tz();
  time_t now = time(nullptr);
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && now > (time_t)EPOCH_2026) {
    rtc_write_epoch(now); time_src = "NTP synced";
  }
  char tb[16], db[40], sb[48];
  if (now > (time_t)EPOCH_2026) {
    struct tm t; localtime_r(&now, &t);
    strftime(tb, sizeof tb, "%H:%M", &t); strftime(db, sizeof db, "%a, %d %b %Y", &t);
    snprintf(sb, sizeof sb, "Kuala Lumpur");
  } else { strcpy(tb, "--:--"); strcpy(db, ""); strcpy(sb, "Needs Wi-Fi once"); }
  lv_label_set_text(lbl_clock, tb); lv_label_set_text(lbl_date, db); lv_label_set_text(lbl_tsrc, sb);
}

// ================= BQ27220 battery gauge (@0x55) =================
static bool bq_word(uint8_t cmd, uint16_t *v) {
  Wire.beginTransmission(0x55); Wire.write(cmd);
  if (Wire.endTransmission(true)) return false;
  if (Wire.requestFrom((uint8_t)0x55, (uint8_t)2) != 2) return false;
  uint8_t lo = Wire.read(), hi = Wire.read(); *v = lo | (hi << 8); return true;
}
static void battery_text(char *out, size_t n) {
  uint16_t st, mv, soc, cur_raw;
  if (!bq_word(0x0A, &st) || !bq_word(0x08, &mv)) { snprintf(out, n, "Battery: gauge not responding"); return; }
  if (!(st & (1u << 3)) || mv < 2500) { snprintf(out, n, "Battery: none (USB powered)"); return; }
  bq_word(0x2C, &soc); bq_word(0x0C, &cur_raw);
  int cur = cur_raw < 0x8000 ? cur_raw : (int)cur_raw - 65536;
  const char *m = cur > 20 ? "charging" : (cur < -20 ? "discharging" : "idle/full");
  snprintf(out, n, "Battery: %u%%  %.2f V\n%s %+d mA", soc, mv / 1000.0f, m, cur);
}

// ================= Recordings page =================
static RecFile files[40];
static int nfiles = 0;
static bool list_dirty = true;
static char sel_name[24];
static int list_seen_count = -1;

static void close_box_async(lv_obj_t *m) { lv_msgbox_close_async(m); }
static void play_box_cb(lv_event_t *e) { rec_play_stop(); close_box_async(lv_event_get_current_target(e)); play_box = NULL; }
static void show_play_box() {
  static const char *btns[] = {"Stop", ""};
  play_box = lv_msgbox_create(NULL, "Playing", sel_name, btns, false); ui_style_msgbox(play_box);
  lv_obj_set_width(play_box, 200); lv_obj_center(play_box);
  lv_obj_add_event_cb(play_box, play_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
static void del_box_cb(lv_event_t *e) {
  lv_obj_t *m = lv_event_get_current_target(e);
  if (lv_msgbox_get_active_btn(m) == 0 && rec_delete(sel_name)) list_dirty = true;
  close_box_async(m);
}
static void act_box_cb(lv_event_t *e) {
  lv_obj_t *m = lv_event_get_current_target(e);
  uint16_t b = lv_msgbox_get_active_btn(m);
  close_box_async(m);
  if (b == 0) { if (tk_state == TK_IDLE || tk_state == TK_ERROR) rec_play(sel_name); if (rec_playing) show_play_box(); }
  else if (b == 1) {
    static const char *btns[] = {"Yes, delete", "No", ""};
    lv_obj_t *c = lv_msgbox_create(NULL, "Delete?", sel_name, btns, false); ui_style_msgbox(c);
    lv_obj_set_width(c, 210); lv_obj_center(c);
    lv_obj_add_event_cb(c, del_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
  }
}
static void row_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= nfiles) return;
  strncpy(sel_name, files[i].name, 23); sel_name[23] = 0;
  static const char *btns[] = {"Play", "Delete", "Close", ""};
  lv_obj_t *m = lv_msgbox_create(NULL, sel_name, rec.state == REC_IDLE ? "" : "Stop recording first", btns, false); ui_style_msgbox(m);
  lv_obj_set_width(m, 210); lv_obj_center(m);
  lv_obj_add_event_cb(m, act_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
static void refresh_list() {
  list_dirty = false;
  if (rec.state != REC_IDLE) return;
  nfiles = rec_list(files, 40);
  lv_obj_clean(rec_list_obj);
  if (nfiles == 0) {
    lv_obj_t *e = lv_list_add_text(rec_list_obj, "No recordings yet");
    lv_obj_set_style_bg_opa(e, LV_OPA_TRANSP, 0); lv_obj_set_style_text_color(e, lv_color_hex(C_GREY), 0);
    lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0);
  }
  for (int i = 0; i < nfiles; i++) {
    char row[64], d[16];
    uint32_t secs = files[i].bytes > 44 ? (files[i].bytes - 44) / 32000 : 0;
    fmt_dur(d, sizeof d, secs);
    int num = 0; sscanf(files[i].name, "rec_%d", &num);
    snprintf(row, sizeof row, "Recording %d    %s", num, d);
    lv_obj_t *b = lv_list_add_btn(rec_list_obj, NULL, row);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_color(b, lv_color_hex(C_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_color(b, lv_color_hex(C_DIV), 0);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_add_event_cb(b, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  list_seen_count = rec.count;
}

// ================= UI (v7: Notion-inspired, minimal, light) =================
static void rec_show_state(int stt) {
  bool recording = stt == REC_RECORDING;
  // idle: red dot; recording: red stop square
  lv_obj_set_size(rec_icon, recording ? 18 : 22, recording ? 18 : 22);
  lv_obj_set_style_radius(rec_icon, recording ? 3 : LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(rec_icon, lv_color_hex(stt == REC_FINALIZING ? C_LINE : C_RED), 0);
  lv_obj_center(rec_icon);
  if (recording) lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(lbl_time, lv_color_hex(recording ? C_TEXT : C_GREY), 0);
}

#include "settings_ui.h"

static void build_ui() {
  ui_styles_init();
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  tv = lv_tileview_create(scr);
  lv_obj_set_size(tv, 360, 360); lv_obj_set_pos(tv, 0, 0);
  lv_obj_set_style_bg_color(tv, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
  lv_obj_t *tk = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t *t1 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t *t2 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t *t3 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_LEFT);

  // ---------- page 0: recorder ----------
  ui_title(t0, "Recorder");
#ifdef HAVE_HEADSET
  hs_img = lv_img_create(t0); lv_img_set_src(hs_img, &headset_frames[0]);
  lv_obj_align(hs_img, LV_ALIGN_TOP_MID, 0, 60);
#endif
  lbl_time = ui_label(t0, &lv_font_montserrat_28, C_GREY, 0, "00:00");
  lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 212);
  bar = lv_bar_create(t0); lv_obj_set_size(bar, 110, 3); lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 250);
  lv_bar_set_range(bar, 0, 100); lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(C_DIV), LV_PART_MAIN); lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(C_TEXT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 2, LV_PART_MAIN); lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
  rec_btn = ui_btn(t0, 54, 54, "", NULL);
  lv_obj_set_style_radius(rec_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_align(rec_btn, LV_ALIGN_TOP_MID, 0, 260);
  lv_obj_add_event_cb(rec_btn, rec_cb, LV_EVENT_CLICKED, NULL);
  rec_icon = lv_obj_create(rec_btn); lv_obj_remove_style_all(rec_icon);
  lv_obj_set_style_bg_opa(rec_icon, LV_OPA_COVER, 0); lv_obj_clear_flag(rec_icon, LV_OBJ_FLAG_CLICKABLE);
  lbl_stat = ui_label(t0, &lv_font_montserrat_12, C_GREY, 200, "");
  lv_label_set_long_mode(lbl_stat, LV_LABEL_LONG_DOT);
  lv_obj_align(lbl_stat, LV_ALIGN_TOP_MID, 0, 320);
  rec_show_state(REC_IDLE);

  // ---------- page 1: Tauke voice ----------
  tk_build(tk);

  // ---------- page 2: clock ----------
  lbl_clock = ui_label(t1, &lv_font_montserrat_48, C_TEXT, 260, "--:--");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 122);
  lbl_date = ui_label(t1, &lv_font_montserrat_16, C_TEXT, 260, "");
  lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 186);
  lbl_tsrc = ui_label(t1, &lv_font_montserrat_12, C_GREY, 240, "");
  lv_obj_align(lbl_tsrc, LV_ALIGN_TOP_MID, 0, 212);

  // ---------- page 3: recordings ----------
  lbl_listhdr = ui_title(t2, "Recordings");
  ui_divider(t2, 220, 62);
  rec_list_obj = lv_list_create(t2); lv_obj_set_size(rec_list_obj, 236, 236); lv_obj_align(rec_list_obj, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_set_style_bg_color(rec_list_obj, lv_color_hex(C_BG), 0);
  lv_obj_set_style_border_width(rec_list_obj, 0, 0); lv_obj_set_style_radius(rec_list_obj, 0, 0);
  lv_obj_set_style_pad_all(rec_list_obj, 0, 0);
  lv_obj_set_style_text_font(rec_list_obj, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(rec_list_obj, lv_color_hex(C_TEXT), 0);
  lv_obj_set_scrollbar_mode(rec_list_obj, LV_SCROLLBAR_MODE_OFF);

  // ---------- page 4: settings (Android-style list) ----------
  settings_build(t3);

  // ---------- page dots ----------
  for (int i = 0; i < NPAGES; i++) {
    dots[i] = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(dots[i]);
    lv_obj_set_size(dots[i], 6, 6); lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(dots[i], LV_ALIGN_TOP_MID, (i - 2) * 14, 342);
  }
}

void setup() {
  I2C_Init(); Backlight_Init(); LCD_Init(); Lvgl_Init();
  pinMode(9, OUTPUT); digitalWrite(9, LOW);           // speaker amp off
  build_ui();
  settings_load();   // volume, mic gain, brightness from NVS (before rec_init so the gain applies)
  ensure_tz();
  time_t e; if (rtc_read_epoch(&e)) { struct timeval tvv = { e, 0 }; settimeofday(&tvv, NULL); time_src = "from RTC (offline)"; }
  rec_init();
  xTaskCreate(net_task, "net", 12288, NULL, 1, NULL);
}

void loop() {
  static char shown_net[96], shown_stat[96], tbuf[16];
  static int last_state = -1, last_page = -1;
  static uint32_t last_ui = 0, last_slow = 0, last_tk = 0, last_hs = 0;
  uint32_t now = millis();
  if (now - last_tk >= 33) {   // Tauke face + state machine
    last_tk = now;
    tk_tick(last_page == 1 || lv_obj_get_scroll_x(tv) / 360 == 1 || (lv_obj_get_scroll_x(tv) + 359) / 360 == 1);
    sub_tick();
  }
#ifdef HAVE_HEADSET
  if (rec.state == REC_RECORDING && now - last_hs >= HEADSET_MS) {   // animate character while recording
    last_hs = now; hs_frame = (hs_frame + 1) % HEADSET_FRAMES;
    lv_img_set_src(hs_img, &headset_frames[hs_frame]);
  } else if (rec.state != REC_RECORDING && hs_frame != 0) { hs_frame = 0; lv_img_set_src(hs_img, &headset_frames[0]); }
#endif
  if (now - last_ui >= 100) {
    last_ui = now;
    if (!ota_lbl) shown_net[0] = 0;
    else if (strcmp(shown_net, status_txt)) { strcpy(shown_net, status_txt); lv_label_set_text(ota_lbl, shown_net); }
    uint32_t s = rec.secs;
    snprintf(tbuf, sizeof tbuf, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    lv_label_set_text(lbl_time, tbuf);
    lv_bar_set_value(bar, rec.state == REC_RECORDING ? rec.level : 0, LV_ANIM_OFF);
    char fr[16], st[96];
    fmt_dur(fr, sizeof fr, rec.free_secs);
    // minimal: only show problems, else free time
    bool problem = strcmp(rec.msg, "ready") && strcmp(rec.msg, "recording") && strcmp(rec.msg, "saved") &&
                   strcmp(rec.msg, "playing") && strcmp(rec.msg, "deleted") && rec.msg[0];
    if (problem) snprintf(st, sizeof st, "%s", rec.msg); else snprintf(st, sizeof st, "%s left", fr);
    if (strcmp(shown_stat, st)) {
      strcpy(shown_stat, st); lv_label_set_text(lbl_stat, shown_stat);
      lv_obj_set_style_text_color(lbl_stat, lv_color_hex(problem ? C_RED : C_GREY), 0);
    }
    int stt = rec.state;
    if (stt != last_state) {
      last_state = stt;
      rec_show_state(stt);
      bool recording = (stt != REC_IDLE);
      // no page swiping while recording: stray swipes can't interfere
      if (recording) { lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF); }
      else if (tk_state != TK_LISTEN) lv_obj_add_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
      if (stt == REC_IDLE) list_dirty = true;
    }
    // page dots + per-page refresh
    int page = (lv_obj_get_scroll_x(tv) + 180) / 360; if (page < 0) page = 0; if (page > NPAGES - 1) page = NPAGES - 1;
    for (int i = 0; i < NPAGES; i++) lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == page ? C_TEXT : C_LINE), 0);
    if (page != last_page) { last_page = page; if (page == PG_LIST) list_dirty = true; last_slow = 0; }
    if (page == PG_LIST && (list_dirty || rec.count != list_seen_count)) refresh_list();
    if (play_box && !rec_playing) { lv_msgbox_close_async(play_box); play_box = NULL; }
  }
  if (now - last_slow >= 1000) {   // 1 Hz: clock, status page
    last_slow = now;
    time_tick();
    if (last_page == PG_STATUS) settings_summaries();
    sub_refresh();
  }
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
