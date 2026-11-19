// Demo + Wi-Fi captive portal (WiFiManager) + HTTPS OTA pull. Prototype.
// Version shown on screen. Updates: GitHub releases/latest/download/{version.txt,firmware.bin}.
#include "Display_ST77916.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

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
static lv_obj_t *lbl;

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

void setup() {
  I2C_Init(); Backlight_Init(); LCD_Init(); Lvgl_Init();
  lbl = lv_label_create(lv_scr_act()); lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t *b = lv_btn_create(lv_scr_act()); lv_obj_align(b, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, NULL);
  lv_label_set_text(lv_label_create(b), "Check update");
  xTaskCreate(net_task, "net", 12288, NULL, 1, NULL);
}

void loop() {
  static char shown[96];
  if (strcmp(shown, status_txt)) { strcpy(shown, status_txt); lv_label_set_text(lbl, shown); }
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
