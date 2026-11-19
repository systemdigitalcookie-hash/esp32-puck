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
// Server hosts: <base>/version.txt (integer) and <base>/firmware.bin (app-only .ino.bin)
#define OTA_REPO "systemdigitalcookie-hash/esp32-puck"
#define OTA_BASE "https://github.com/" OTA_REPO "/releases/latest/download"

static char status_txt[96] = "v" FW_STR " booting";
static volatile bool want_check = false;
static lv_obj_t *lbl, *lbl_title, *lbl_time, *lbl_stat, *lbl_ver, *bar, *rec_btn, *rec_lbl, *melody;

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

static void rec_cb(lv_event_t *e) {
  if (rec.state == REC_IDLE) rec_start();
  else if (rec.state == REC_RECORDING) rec_stop();
}

static void bob_cb(void *o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }

static void fmt_dur(char *out, size_t n, uint32_t s) {
  if (s >= 3600) snprintf(out, n, "%luh%02lum", (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
  else snprintf(out, n, "%lum%02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lbl_title = lv_label_create(scr); lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
  lv_label_set_text(lbl_title, "Meeting Recorder"); lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 22);

  lbl_time = lv_label_create(scr); lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
  lv_label_set_text(lbl_time, "00:00"); lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 52);

  bar = lv_bar_create(scr); lv_obj_set_size(bar, 200, 10); lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 92);
  lv_bar_set_range(bar, 0, 100); lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x334455), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x30d070), LV_PART_INDICATOR);

#ifdef HAVE_MELODY
  melody = lv_img_create(scr); lv_img_set_src(melody, &melody_img);
  lv_obj_align(melody, LV_ALIGN_TOP_MID, 0, 106);
  lv_obj_add_flag(melody, LV_OBJ_FLAG_HIDDEN);
#endif

  rec_btn = lv_btn_create(scr); lv_obj_set_size(rec_btn, 76, 76); lv_obj_align(rec_btn, LV_ALIGN_CENTER, 0, 58);
  lv_obj_set_style_radius(rec_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(rec_btn, lv_color_hex(0xd02030), 0);
  lv_obj_add_event_cb(rec_btn, rec_cb, LV_EVENT_CLICKED, NULL);
  rec_lbl = lv_label_create(rec_btn); lv_obj_set_style_text_font(rec_lbl, &lv_font_montserrat_20, 0);
  lv_label_set_text(rec_lbl, "REC"); lv_obj_center(rec_lbl);

  lbl_stat = lv_label_create(scr); lv_obj_set_style_text_font(lbl_stat, &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(lbl_stat, LV_LABEL_LONG_CLIP); lv_obj_set_width(lbl_stat, 230);
  lv_obj_set_style_text_align(lbl_stat, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_stat, ""); lv_obj_align(lbl_stat, LV_ALIGN_BOTTOM_MID, 0, -72);

  lbl = lv_label_create(scr); lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);   // net/OTA status
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP); lv_obj_set_width(lbl, 200);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x9fb0c0), 0);
  lv_label_set_text(lbl, ""); lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -50);

  lv_obj_t *b = lv_btn_create(scr); lv_obj_set_size(b, 116, 26); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
  lbl_ver = lv_label_create(b); lv_obj_set_style_text_font(lbl_ver, &lv_font_montserrat_12, 0);
  lv_label_set_text(lbl_ver, "v" FW_STR "  Check update"); lv_obj_center(lbl_ver);
}

void setup() {
  I2C_Init(); Backlight_Init(); LCD_Init(); Lvgl_Init();
  build_ui();
  rec_init();
  xTaskCreate(net_task, "net", 12288, NULL, 1, NULL);
}

void loop() {
  static char shown_net[96], shown_stat[96], tbuf[16];
  static int last_state = -1;
  static uint32_t last_ui = 0;
  uint32_t now = millis();
  if (now - last_ui >= 100) {
    last_ui = now;
    if (strcmp(shown_net, status_txt)) { strcpy(shown_net, status_txt); lv_label_set_text(lbl, shown_net); }
    uint32_t s = rec.secs;
    snprintf(tbuf, sizeof tbuf, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    lv_label_set_text(lbl_time, tbuf);
    lv_bar_set_value(bar, rec.state == REC_RECORDING ? rec.level : 0, LV_ANIM_OFF);
    char fr[16], st[96];
    fmt_dur(fr, sizeof fr, rec.free_secs);
    snprintf(st, sizeof st, "%s | %s | %s left | %d saved%s", rec.msg, rec.storage, fr, rec.count, rec.dropped ? " | drops!" : "");
    if (strcmp(shown_stat, st)) { strcpy(shown_stat, st); lv_label_set_text(lbl_stat, shown_stat); }
    int stt = rec.state;
    if (stt != last_state) {
      last_state = stt;
      lv_label_set_text(rec_lbl, stt == REC_IDLE ? "REC" : (stt == REC_RECORDING ? "STOP" : "..."));
      lv_obj_set_style_bg_color(rec_btn, stt == REC_RECORDING ? lv_color_hex(0x701018) : lv_color_hex(0xd02030), 0);
#ifdef HAVE_MELODY
      if (stt == REC_RECORDING) {
        lv_obj_clear_flag(melody, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, melody); lv_anim_set_exec_cb(&a, bob_cb);
        lv_anim_set_values(&a, 0, -8); lv_anim_set_time(&a, 600); lv_anim_set_playback_time(&a, 600);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
      } else {
        lv_anim_del(melody, bob_cb); lv_obj_set_style_translate_y(melody, 0, 0);
        lv_obj_add_flag(melody, LV_OBJ_FLAG_HIDDEN);
      }
#endif
    }
  }
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
