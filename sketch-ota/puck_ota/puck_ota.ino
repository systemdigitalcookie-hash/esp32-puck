// Demo + Wi-Fi captive portal (WiFiManager) + HTTPS OTA pull. Prototype.
// Version shown on screen. Updates: GitHub releases/latest/download/{version.txt,firmware.bin}.
#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include "wifi_net.h"
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
static lv_obj_t *tv, *dots[5], *lbl_clock, *lbl_date, *lbl_tsrc;
#define NPAGES 5   // 0 Recorder (+ recordings), 1 Tauke, 2 Clock, 3 Drive game, 4 Settings
#define PG_TAUKE 1
#define PG_GAME 3
#define PG_STATUS 4
// one place decides whether pages can be swiped (recording, talking to Tauke, playing the game lock it)
static bool lock_rec = false, lock_tk = false, lock_game = false;
static void nav_apply() {
  if (lock_rec || lock_tk || lock_game) lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
  else lv_obj_add_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
}
#include "tauke_ui.h"
#include "recorder_ui.h"

static void btn_cb(lv_event_t *e) { want_check = true; }

static void net_task(void *) {
  // v11: saved networks (strongest known first) -> WiFiManager portal "Puck-Setup" only as the fallback
  bool online = wn_boot("Puck-Setup", status_txt, sizeof status_txt);
  if (online) { wn_time_once(); snprintf(status_txt, sizeof status_txt, "%s", WiFi.localIP().toString().c_str()); }
  for (;;) {
    if (wn_service()) snprintf(status_txt, sizeof status_txt, "%s", WiFi.localIP().toString().c_str());
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

#include "settings_ui.h"
#include "game_ui.h"

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

  // ---------- page 0: recorder + recent recordings ----------
  ru_build(t0);

  // ---------- page 1: Tauke voice ----------
  tk_build(tk);

  // ---------- page 2: clock ----------
  lbl_clock = ui_label(t1, &lv_font_montserrat_48, C_TEXT, 260, "--:--");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 122);
  lbl_date = ui_label(t1, &lv_font_montserrat_16, C_TEXT, 260, "");
  lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 186);
  lbl_tsrc = ui_label(t1, &lv_font_montserrat_12, C_GREY, 240, "");
  lv_obj_align(lbl_tsrc, LV_ALIGN_TOP_MID, 0, 212);

  // ---------- page 3: Drive game ----------
  gm_build(t2);

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
  static char shown_net[96];
  static int last_page = -1;
  static uint32_t last_ui = 0, last_slow = 0, last_tk = 0;
  uint32_t now = millis();
  int sx = lv_obj_get_scroll_x(tv);
  bool settled = sx % 360 == 0;
  if (now - last_tk >= 33) {   // ~30 Hz: Tauke face, recorder state machine, settings sub-screens
    last_tk = now;
    tk_tick(last_page == PG_TAUKE || sx / 360 == PG_TAUKE || (sx + 359) / 360 == PG_TAUKE);
    ru_tick(now);
    sub_tick();
  }
  gm_tick(now, settled ? sx / 360 == PG_GAME : (sx / 360 == PG_GAME || (sx + 359) / 360 == PG_GAME));
  if (now - last_ui >= 100) {
    last_ui = now;
    if (!ota_lbl) shown_net[0] = 0;
    else if (strcmp(shown_net, status_txt)) { strcpy(shown_net, status_txt); lv_label_set_text(ota_lbl, shown_net); }
    int page = (sx + 180) / 360; if (page < 0) page = 0; if (page > NPAGES - 1) page = NPAGES - 1;
    for (int i = 0; i < NPAGES; i++) lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == page ? C_TEXT : C_LINE), 0);
    if (page != last_page) {
      last_page = page; last_slow = 0;
      if (page == 0) list_dirty = true;
      if (page != 0 && rd) rd_close();
    }
  }
  if (now - last_slow >= 1000) {   // 1 Hz: clock, status page
    last_slow = now;
    time_tick();
    if (last_page == PG_STATUS) settings_summaries();
    sub_refresh();
  }
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(gm == GM_PLAY ? 1 : 5));
}
