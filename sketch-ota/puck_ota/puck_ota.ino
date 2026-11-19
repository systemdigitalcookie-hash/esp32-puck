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
#if __has_include("melody_img.h")
#include "melody_img.h"   // optional personal asset: extern const lv_img_dsc_t melody_img;
#define HAVE_MELODY 1
#endif

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
static lv_obj_t *lbl, *lbl_title, *lbl_time, *lbl_stat, *lbl_ver, *bar, *lbl_diag, *rec_btn, *rec_lbl, *melody;
static lv_obj_t *tv, *dots[4], *lbl_ver_small, *lbl_clock, *lbl_date, *lbl_tsrc, *lbl_info, *rec_list_obj, *lbl_listhdr;
static lv_obj_t *play_box = NULL;

static void btn_cb(lv_event_t *e) { want_check = true; }

static void net_task(void *) {
  WiFiManager wm;                       // saved creds reused; else AP "Puck-Setup" portal
  wm.setConfigPortalTimeout(180);
  snprintf(status_txt, sizeof status_txt, "WiFi: join AP Puck-Setup");
  if (!wm.autoConnect("Puck-Setup")) { snprintf(status_txt, sizeof status_txt, "WiFi failed"); vTaskDelete(NULL); }
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // cert validation needs correct time
  for (int i = 0; i < 40 && time(nullptr) < 1700000000; i++) delay(250);
  snprintf(status_txt, sizeof status_txt, "v%d  %s", FW_VERSION, WiFi.localIP().toString().c_str());
  for (;;) {
    if (want_check) {
      want_check = false;
      if (rec.state != REC_IDLE) { snprintf(status_txt, sizeof status_txt, "stop recording first"); continue; }
      WiFiClientSecure c; c.setCACert(GH_ROOT_CAS);   // TLS validated against pinned roots
      HTTPClient h; h.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); h.begin(c, String(OTA_BASE) + "/version.txt");
      int code = h.GET(); int remote = code == 200 ? h.getString().toInt() : -1; h.end();
      if (remote > FW_VERSION) {
        snprintf(status_txt, sizeof status_txt, "updating to v%d...", remote);
        httpUpdate.rebootOnUpdate(true);
        httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        httpUpdate.update(c, String(OTA_BASE) + "/firmware.bin");
        snprintf(status_txt, sizeof status_txt, "update failed");
      } else snprintf(status_txt, sizeof status_txt, "v%d up to date (http %d)", FW_VERSION, code);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

#define MELODY_ZOOM_IDLE 170   // ~66%: 58x80 px, static
#define MELODY_TOP_IDLE 78     // box top; drawn 58x80 spans y=98..178
#define MELODY_TOP_REC 100     // drawn 88x120 spans y=100..220
#define MELODY_ZOOM_REC 256    // 100%: 88x120 px, bobbing
static void rec_cb(lv_event_t *e) {
  if (rec.state == REC_IDLE) rec_start();
  else if (rec.state == REC_RECORDING) rec_stop();
}

static void bob_cb(void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }

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
    snprintf(sb, sizeof sb, "%s | Kuala Lumpur (UTC+8)", time_src);
  } else { strcpy(tb, "--:--"); strcpy(db, "waiting for time"); strcpy(sb, "needs Wi-Fi once"); }
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
  play_box = lv_msgbox_create(NULL, "Playing", sel_name, btns, false);
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
  if (b == 0) { rec_play(sel_name); if (rec_playing) show_play_box(); }
  else if (b == 1) {
    static const char *btns[] = {"Yes, delete", "No", ""};
    lv_obj_t *c = lv_msgbox_create(NULL, "Delete?", sel_name, btns, false);
    lv_obj_set_width(c, 210); lv_obj_center(c);
    lv_obj_add_event_cb(c, del_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
  }
}
static void row_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= nfiles) return;
  strncpy(sel_name, files[i].name, 23); sel_name[23] = 0;
  static const char *btns[] = {"Play", "Delete", "Close", ""};
  lv_obj_t *m = lv_msgbox_create(NULL, sel_name, rec.state == REC_IDLE ? "Choose action" : "Stop recording first", btns, false);
  lv_obj_set_width(m, 210); lv_obj_center(m);
  lv_obj_add_event_cb(m, act_box_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
static void refresh_list() {
  list_dirty = false;
  if (rec.state != REC_IDLE) return;
  nfiles = rec_list(files, 40);
  lv_obj_clean(rec_list_obj);
  if (nfiles == 0) { lv_list_add_text(rec_list_obj, "No recordings yet"); }
  for (int i = 0; i < nfiles; i++) {
    char row[64], d[16];
    uint32_t secs = files[i].bytes > 44 ? (files[i].bytes - 44) / 32000 : 0;
    fmt_dur(d, sizeof d, secs);
    snprintf(row, sizeof row, "%.8s  %s  %.1fMB", files[i].name, d, files[i].bytes / 1048576.0f);
    lv_obj_t *b = lv_list_add_btn(rec_list_obj, NULL, row);
    lv_obj_add_event_cb(b, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  list_seen_count = rec.count;
}

// ================= UI =================
static lv_obj_t *mk_label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int w) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

static void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  tv = lv_tileview_create(scr);
  lv_obj_set_size(tv, 360, 360); lv_obj_set_pos(tv, 0, 0);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
  lv_obj_t *t1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t *t2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t *t3 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);

  // ---------- page 0: recorder (unchanged from v4 except the Check-update button moved to Status) ----------
  lbl_title = lv_label_create(t0); lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
  lv_label_set_text(lbl_title, "Meeting Recorder"); lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 22);

  lbl_time = lv_label_create(t0); lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
  lv_label_set_text(lbl_time, "00:00"); lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 50);

  bar = lv_bar_create(t0); lv_obj_set_size(bar, 200, 10); lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 88);
  lv_bar_set_range(bar, 0, 100); lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x334455), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x30d070), LV_PART_INDICATOR);

  lbl_diag = lv_label_create(t0); lv_obj_set_style_text_font(lbl_diag, &lv_font_montserrat_12, 0);
  lv_label_set_long_mode(lbl_diag, LV_LABEL_LONG_WRAP); lv_obj_set_size(lbl_diag, 230, 46);
  lv_obj_set_style_text_align(lbl_diag, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl_diag, lv_color_hex(0xc8d4e0), 0);
  lv_label_set_text(lbl_diag, ""); lv_obj_align(lbl_diag, LV_ALIGN_TOP_MID, 0, 182);

#ifdef HAVE_MELODY
  melody = lv_img_create(t0); lv_img_set_src(melody, &melody_img);
  lv_obj_set_size(melody, 88, 120);
  lv_img_set_zoom(melody, MELODY_ZOOM_IDLE);
  lv_obj_align(melody, LV_ALIGN_TOP_MID, 0, MELODY_TOP_IDLE);
#endif

  rec_btn = lv_btn_create(t0); lv_obj_set_size(rec_btn, 58, 58); lv_obj_align(rec_btn, LV_ALIGN_TOP_MID, 0, 232);
  lv_obj_set_style_radius(rec_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(rec_btn, lv_color_hex(0xd02030), 0);
  lv_obj_add_event_cb(rec_btn, rec_cb, LV_EVENT_CLICKED, NULL);
  rec_lbl = lv_label_create(rec_btn); lv_obj_set_style_text_font(rec_lbl, &lv_font_montserrat_16, 0);
  lv_label_set_text(rec_lbl, "REC"); lv_obj_center(rec_lbl);

  lbl_stat = lv_label_create(t0); lv_obj_set_style_text_font(lbl_stat, &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(lbl_stat, LV_LABEL_LONG_CLIP); lv_obj_set_width(lbl_stat, 230);
  lv_obj_set_style_text_align(lbl_stat, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_stat, ""); lv_obj_align(lbl_stat, LV_ALIGN_TOP_MID, 0, 296);

  lbl_ver_small = lv_label_create(t0); lv_obj_set_style_text_font(lbl_ver_small, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_ver_small, lv_color_hex(0x7f90a0), 0);
  lv_label_set_text(lbl_ver_small, "v" FW_STR "  < swipe >"); lv_obj_align(lbl_ver_small, LV_ALIGN_TOP_MID, 0, 316);

  // ---------- page 1: clock ----------
  lbl_clock = mk_label(t1, &lv_font_montserrat_48, lv_color_white(), 260);
  lv_label_set_text(lbl_clock, "--:--"); lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 110);
  lbl_date = mk_label(t1, &lv_font_montserrat_20, lv_color_hex(0xc8d4e0), 260);
  lv_label_set_text(lbl_date, ""); lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 180);
  lbl_tsrc = mk_label(t1, &lv_font_montserrat_12, lv_color_hex(0x7f90a0), 240);
  lv_label_set_text(lbl_tsrc, ""); lv_obj_align(lbl_tsrc, LV_ALIGN_TOP_MID, 0, 225);

  // ---------- page 2: recordings ----------
  lbl_listhdr = lv_label_create(t2); lv_obj_set_style_text_font(lbl_listhdr, &lv_font_montserrat_20, 0);
  lv_label_set_text(lbl_listhdr, "Recordings"); lv_obj_align(lbl_listhdr, LV_ALIGN_TOP_MID, 0, 22);
  rec_list_obj = lv_list_create(t2); lv_obj_set_size(rec_list_obj, 240, 220); lv_obj_align(rec_list_obj, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_bg_color(rec_list_obj, lv_color_hex(0x1b2733), 0);
  lv_obj_set_style_text_font(rec_list_obj, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(rec_list_obj, lv_color_white(), 0);

  // ---------- page 3: status + Check update ----------
  lv_obj_t *st_t = lv_label_create(t3); lv_obj_set_style_text_font(st_t, &lv_font_montserrat_20, 0);
  lv_label_set_text(st_t, "Status"); lv_obj_align(st_t, LV_ALIGN_TOP_MID, 0, 22);
  lbl_info = mk_label(t3, &lv_font_montserrat_14, lv_color_hex(0xc8d4e0), 236);
  lv_obj_set_style_text_line_space(lbl_info, 4, 0);
  lv_label_set_text(lbl_info, ""); lv_obj_align(lbl_info, LV_ALIGN_TOP_MID, 0, 54);

  lbl = lv_label_create(t3); lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);   // net/OTA status
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP); lv_obj_set_width(lbl, 200);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x9fb0c0), 0);
  lv_label_set_text(lbl, ""); lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 262);

  lv_obj_t *b = lv_btn_create(t3); lv_obj_set_size(b, 140, 34); lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 282);
  lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
  lbl_ver = lv_label_create(b); lv_obj_set_style_text_font(lbl_ver, &lv_font_montserrat_14, 0);
  lv_label_set_text(lbl_ver, "v" FW_STR "  Check update"); lv_obj_center(lbl_ver);

  // ---------- page indicator dots (top layer, inside the round area) ----------
  for (int i = 0; i < 4; i++) {
    dots[i] = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(dots[i]);
    lv_obj_set_size(dots[i], 8, 8); lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
    lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(dots[i], LV_ALIGN_TOP_MID, (i - 1) * 16 - 8, 340);
  }
}

static void swatch_done(lv_timer_t *t) { lv_obj_del((lv_obj_t *)t->user_data); lv_timer_del(t); }
static void color_swatches() {   // 4 s boot strip: red, green, blue, My Melody pink, yellow - shows true panel colours
  const uint32_t cols[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xF090B0, 0xFFE000};
  lv_obj_t *c = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(c);
  lv_obj_set_size(c, 200, 36); lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 120);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < 5; i++) {
    lv_obj_t *q = lv_obj_create(c); lv_obj_remove_style_all(q); lv_obj_set_size(q, 36, 36); lv_obj_set_pos(q, i * 41, 0);
    lv_obj_set_style_bg_opa(q, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(q, lv_color_hex(cols[i]), 0);
  }
  lv_timer_create(swatch_done, 4000, c);
}

void setup() {
  I2C_Init(); Backlight_Init(); LCD_Init(); Lvgl_Init();
  pinMode(9, OUTPUT); digitalWrite(9, LOW);           // speaker amp off
  build_ui();
  color_swatches();
  ensure_tz();
  time_t e; if (rtc_read_epoch(&e)) { struct timeval tvv = { e, 0 }; settimeofday(&tvv, NULL); time_src = "from RTC (offline)"; }
  rec_init();
  xTaskCreate(net_task, "net", 12288, NULL, 1, NULL);
}

void loop() {
  static char shown_net[96], shown_stat[96], shown_diag[320], tbuf[16];
  static int last_state = -1, last_page = -1;
  static uint32_t last_ui = 0, last_slow = 0;
  uint32_t now = millis();
  if (now - last_ui >= 100) {
    last_ui = now;
    if (strcmp(shown_net, status_txt)) { strcpy(shown_net, status_txt); lv_label_set_text(lbl, shown_net); }
    uint32_t s = rec.secs;
    snprintf(tbuf, sizeof tbuf, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    lv_label_set_text(lbl_time, tbuf);
    lv_bar_set_value(bar, rec.state == REC_RECORDING ? rec.level : 0, LV_ANIM_OFF);
    char fr[16], st[96], dg[320];
    fmt_dur(fr, sizeof fr, rec.free_secs);
    snprintf(st, sizeof st, "%s | %s %s", rec.msg, rec.storage, fr);
    if (strcmp(shown_stat, st)) { strcpy(shown_stat, st); lv_label_set_text(lbl_stat, shown_stat); }
    snprintf(dg, sizeof dg, "%s\n%s | %s | %d saved%s\n%s", rec.mic, rec.storage, rec.sd, rec.count,
             rec.dropped ? " | drops!" : "", rec.i2c);
    if (strcmp(shown_diag, dg)) { strcpy(shown_diag, dg); lv_label_set_text(lbl_diag, shown_diag); }
    int stt = rec.state;
    if (stt != last_state) {
      last_state = stt;
      lv_label_set_text(rec_lbl, stt == REC_IDLE ? "REC" : (stt == REC_RECORDING ? "STOP" : "..."));
      lv_obj_set_style_bg_color(rec_btn, stt == REC_RECORDING ? lv_color_hex(0x701018) : lv_color_hex(0xd02030), 0);
      bool recording = (stt != REC_IDLE);
      // no page swiping while recording: stray swipes can't interfere
      if (recording) { lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF); }
      else lv_obj_add_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
      if (recording) lv_obj_add_flag(lbl_diag, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(lbl_diag, LV_OBJ_FLAG_HIDDEN);
#ifdef HAVE_MELODY
      lv_anim_del(melody, bob_cb); lv_obj_set_style_translate_y(melody, 0, 0);
      lv_img_set_zoom(melody, recording ? MELODY_ZOOM_REC : MELODY_ZOOM_IDLE);
      lv_obj_align(melody, LV_ALIGN_TOP_MID, 0, recording ? MELODY_TOP_REC : MELODY_TOP_IDLE);
      if (stt == REC_RECORDING) {
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, melody); lv_anim_set_exec_cb(&a, bob_cb);
        lv_anim_set_values(&a, 0, -8); lv_anim_set_time(&a, 600); lv_anim_set_playback_time(&a, 600);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
      }
#endif
      if (stt == REC_IDLE) list_dirty = true;
    }
    // page dots + per-page refresh
    int page = (lv_obj_get_scroll_x(tv) + 180) / 360; if (page < 0) page = 0; if (page > 3) page = 3;
    for (int i = 0; i < 4; i++) lv_obj_set_style_bg_color(dots[i], i == page ? lv_color_white() : lv_color_hex(0x4a5a6a), 0);
    if (page != last_page) { last_page = page; if (page == 2) list_dirty = true; last_slow = 0; }
    if (page == 2 && (list_dirty || rec.count != list_seen_count)) refresh_list();
    if (play_box && !rec_playing) { lv_msgbox_close_async(play_box); play_box = NULL; }
  }
  if (now - last_slow >= 1000) {   // 1 Hz: clock, status page
    last_slow = now;
    time_tick();
    if (last_page == 3) {
      char bt[80], inf[400], ssid[40];
      battery_text(bt, sizeof bt);
      if (WiFi.status() == WL_CONNECTED) {
        snprintf(ssid, sizeof ssid, "%s", WiFi.SSID().c_str());
        snprintf(inf, sizeof inf, "%s\nWi-Fi: %s\n%s  %d dBm\nFirmware: v%d\nStorage: %s", bt, ssid,
                 WiFi.localIP().toString().c_str(), WiFi.RSSI(), FW_VERSION, rec.storage);
      } else {
        snprintf(inf, sizeof inf, "%s\nWi-Fi: not connected\n(join AP Puck-Setup)\nFirmware: v%d\nStorage: %s", bt, FW_VERSION, rec.storage);
      }
      char fr2[16]; fmt_dur(fr2, sizeof fr2, rec.free_secs);
      size_t L = strlen(inf);
      snprintf(inf + L, sizeof inf - L, "\nRecording time left: %s", fr2);
      lv_label_set_text(lbl_info, inf);
    }
  }
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
