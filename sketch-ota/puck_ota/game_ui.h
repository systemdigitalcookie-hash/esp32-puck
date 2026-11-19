// ================= Drive: pixel-art tilt racing game (v10) =================
// Idle (title, Start, best) -> Playing (tilt to steer, dodge cars/cones, speed ramps) -> Game over (score, best, tap to retry).
// Tilt = QMI8658 accelerometer @0x6B on the shared I2C bus (Wire already started by I2C_Init; all I2C runs on the loop task).
// Included from puck_ota.ino after nav_apply()/lock_game. Needs <Preferences.h> (from settings_ui.h).
#pragma once
#include "game_sprites.h"

#define QMI_ADDR 0x6B
enum { GM_IDLE = 0, GM_PLAY, GM_PAUSE, GM_OVER };
static int gm = GM_IDLE;
static bool imu_ok = false, imu_tried = false;
static float imu_a0 = 0, steer_f = 0, car_x = 180, gm_speed = 0, gm_dist = 0, gm_next = 0;
static int gm_score = 0, gm_best = 0, gm_axis = 0;   // gm_axis: 0 X, 1 X reversed, 2 Y, 3 Y reversed (NVS "g_axis")
static uint32_t gm_frame = 0, gm_over_t = 0, gm_log_t = 0;
static int16_t imu_raw[3];
static lv_obj_t *gp_idle, *gp_play, *gp_over, *g_best_lbl, *g_axis_lbl, *g_imu_lbl, *g_player, *g_score_lbl, *g_pause_lbl;
static lv_obj_t *g_dash[2], *g_kerb[2], *g_over_score, *g_over_best, *g_over_new;
static const char *AXIS_TXT[4] = {"Tilt: X", "Tilt: X reversed", "Tilt: Y", "Tilt: Y reversed"};
#define ROAD_L 105
#define ROAD_R 255
#define NOBS 6
struct Obs { lv_obj_t *img; bool on; bool passed; float y; int x, w, h; };
static Obs obs[NOBS];

static bool qmi_w(uint8_t r, uint8_t v) { Wire.beginTransmission(QMI_ADDR); Wire.write(r); Wire.write(v); return Wire.endTransmission(true) == 0; }
static bool qmi_r(uint8_t r, uint8_t *b, int n) {
  Wire.beginTransmission(QMI_ADDR); Wire.write(r);
  if (Wire.endTransmission(false)) return false;
  if (Wire.requestFrom((uint8_t)QMI_ADDR, (uint8_t)n) != n) return false;
  for (int i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}
static void imu_init() {
  imu_tried = true;
  uint8_t id = 0;
  if (!qmi_r(0x00, &id, 1) || id != 0x05) { Serial.printf("[imu] QMI8658 not found (id=0x%02X)\n", id); imu_ok = false; return; }
  imu_ok = qmi_w(0x02, 0x40)      // CTRL1: register auto-increment
        && qmi_w(0x03, 0x15)      // CTRL2: accel +-4 g, ODR ~ 250 Hz
        && qmi_w(0x08, 0x01);     // CTRL7: accel on (gyro off)
  delay(20);
  Serial.printf("[imu] QMI8658 id=0x05 init %s\n", imu_ok ? "ok" : "failed");
}
static bool imu_read() {
  uint8_t b[6];
  if (!imu_ok || !qmi_r(0x35, b, 6)) return false;
  for (int i = 0; i < 3; i++) imu_raw[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
  return true;
}
static float imu_axis_g() {   // selected steering axis in g (8192 LSB/g at +-4 g)
  float v = imu_raw[gm_axis < 2 ? 0 : 1] / 8192.0f;
  return (gm_axis & 1) ? -v : v;
}

static void gm_prefs_load() {
  Preferences p;
  if (p.begin("puck", true)) { gm_best = p.getInt("g_best", 0); gm_axis = p.getInt("g_axis", 0) & 3; p.end(); }
}
static void gm_save_int(const char *k, int v) { Preferences p; if (p.begin("puck", false)) { p.putInt(k, v); p.end(); } }

static void gm_show(lv_obj_t *show) {
  lv_obj_t *all[] = {gp_idle, gp_play, gp_over};
  for (lv_obj_t *p : all) if (p != show && p != gp_play) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  if (show == gp_idle) lv_obj_add_flag(gp_play, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(gp_play, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(show, LV_OBJ_FLAG_HIDDEN);
}
static void gm_set(int s) {
  gm = s;
  lock_game = (s == GM_PLAY || s == GM_PAUSE);
  nav_apply();
  if (s == GM_PAUSE) lv_obj_clear_flag(g_pause_lbl, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_pause_lbl, LV_OBJ_FLAG_HIDDEN);
  char b[32];
  if (s == GM_IDLE) {
    snprintf(b, sizeof b, "Best  %d", gm_best); lv_label_set_text(g_best_lbl, b);
    lv_label_set_text(g_axis_lbl, AXIS_TXT[gm_axis]);
    gm_show(gp_idle);
  } else if (s == GM_OVER) {
    snprintf(b, sizeof b, "%d", gm_score); lv_label_set_text(g_over_score, b);
    snprintf(b, sizeof b, "Best  %d", gm_best); lv_label_set_text(g_over_best, b);
    gm_show(gp_over);
  } else gm_show(gp_play);
}

static void gm_start() {
  if (!imu_tried) imu_init();
  // calibrate neutral = how you're holding it right now
  float acc = 0; int n = 0;
  for (int i = 0; i < 8; i++) { if (imu_read()) { acc += imu_axis_g(); n++; } delay(4); }
  imu_a0 = n ? acc / n : 0; steer_f = 0;
  Serial.printf("[game] start: imu=%s axis=%s neutral=%.3f g\n", imu_ok ? "ok" : "none", AXIS_TXT[gm_axis], imu_a0);
  car_x = 180; gm_speed = 3.0f; gm_dist = 0; gm_next = 60; gm_score = 0; gm_frame = 0;
  for (auto &o : obs) { o.on = false; lv_obj_add_flag(o.img, LV_OBJ_FLAG_HIDDEN); }
  lv_label_set_text(g_score_lbl, "0");
  lv_obj_set_x(g_player, (int)car_x - 15);
  gm_set(GM_PLAY);
}
static void gm_start_cb(lv_event_t *e) { gm_start(); }
static void gm_axis_cb(lv_event_t *e) {
  gm_axis = (gm_axis + 1) & 3; gm_save_int("g_axis", gm_axis);
  lv_label_set_text(g_axis_lbl, AXIS_TXT[gm_axis]);
}
static void gm_play_tap_cb(lv_event_t *e) {
  if (gm == GM_PAUSE) gm_set(GM_PLAY);
  else if (gm == GM_PLAY && !imu_ok) {   // no tilt sensor: tap left/right half to steer
    lv_point_t p; lv_indev_get_point(lv_indev_get_act(), &p);
    car_x += p.x < 180 ? -50 : 50;
  }
}
static void gm_over_cb(lv_event_t *e) { if (millis() - gm_over_t > 600) gm_start(); }   // ignore the crash-moment tap
static void gm_menu_cb(lv_event_t *e) { gm_set(GM_IDLE); }

static void gm_spawn() {
  static const lv_img_dsc_t *SP[3] = {&spr_car_red, &spr_car_yel, &spr_cone};
  const int lanes[3] = {130, 180, 230};
  int free_lane = random(3);
  int count = (gm_score > 8 && random(100) < 35) ? 2 : 1;   // never block all three lanes
  for (int k = 0, lane = random(3); k < count; k++, lane = (lane + 1) % 3) {
    if (lane == free_lane && count == 2) lane = (lane + 1) % 3;
    for (auto &o : obs) if (!o.on) {
      const lv_img_dsc_t *s = SP[random(3)];
      lv_img_set_src(o.img, s);
      o.w = s->header.w; o.h = s->header.h; o.x = lanes[lane] - o.w / 2 + (int)random(-6, 7);
      o.y = -o.h - (float)random(0, 20); o.on = true; o.passed = false;
      lv_obj_clear_flag(o.img, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(o.img, o.x, (int)o.y);
      break;
    }
  }
}

static void gm_frame_tick(uint32_t now) {
  gm_frame++;
  // --- steering ---
  if (imu_read()) {
    float t = imu_axis_g() - imu_a0;
    if (fabsf(t) < 0.04f) t = 0; else t -= t > 0 ? 0.04f : -0.04f;   // deadzone
    steer_f += (t - steer_f) * 0.35f;                                   // smoothing
    float tgt = 180 + constrain(steer_f / 0.28f, -1.0f, 1.0f) * 60;     // ~16 deg tilt = full side
    car_x += (tgt - car_x) * 0.30f;
    if (now - gm_log_t >= 200) { gm_log_t = now; Serial.printf("[imu] ax=%d ay=%d az=%d steer=%.2f x=%d\n", imu_raw[0], imu_raw[1], imu_raw[2], steer_f, (int)car_x); }
  }
  car_x = constrain(car_x, ROAD_L + 16.0f, ROAD_R - 16.0f);
  lv_obj_set_x(g_player, (int)car_x - 15);
  // --- world scroll ---
  gm_speed = min(11.0f, gm_speed + 0.0025f);
  gm_dist += gm_speed;
  int off = (int)gm_dist % 48;
  for (int i = 0; i < 2; i++) { lv_obj_set_style_translate_y(g_dash[i], off - 48, 0); lv_obj_set_style_translate_y(g_kerb[i], off - 48, 0); }
  if (gm_dist >= gm_next) { gm_spawn(); gm_next = gm_dist + max(95.0f, 190.0f - gm_score * 3.0f) + random(0, 40); }
  // --- obstacles + collision ---
  int px0 = (int)car_x - 12, px1 = (int)car_x + 12, py0 = 272, py1 = 316;   // player hitbox (inset)
  for (auto &o : obs) {
    if (!o.on) continue;
    o.y += gm_speed;
    if (o.y > 360) { o.on = false; lv_obj_add_flag(o.img, LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_set_y(o.img, (int)o.y);
    int ox0 = o.x + 4, ox1 = o.x + o.w - 4, oy0 = (int)o.y + 4, oy1 = (int)o.y + o.h - 4;
    if (ox0 < px1 && ox1 > px0 && oy0 < py1 && oy1 > py0) {
      Serial.printf("[game] crash score=%d best=%d\n", gm_score, gm_best);
      bool rec_new = gm_score > gm_best;
      if (rec_new) { gm_best = gm_score; gm_save_int("g_best", gm_best); }
      if (rec_new) lv_obj_clear_flag(g_over_new, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_over_new, LV_OBJ_FLAG_HIDDEN);
      gm_over_t = now; gm_set(GM_OVER); return;
    }
    if (!o.passed && o.y > py1) { o.passed = true; gm_score++; char b[12]; snprintf(b, sizeof b, "%d", gm_score); lv_label_set_text(g_score_lbl, b); }
  }
}

static lv_obj_t *gm_rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t col) {
  lv_obj_t *r = lv_obj_create(p); lv_obj_remove_style_all(r);
  lv_obj_set_pos(r, x, y); lv_obj_set_size(r, w, h);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, lv_color_hex(col), 0);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return r;
}
static lv_obj_t *gm_vline(lv_obj_t *p, int x, int w, uint32_t col, int dash) {
  static lv_point_t pts[2] = {{0, 0}, {0, 408}};
  lv_obj_t *l = lv_line_create(p); lv_line_set_points(l, pts, 2);
  lv_obj_set_pos(l, x, 0);
  lv_obj_set_style_line_width(l, w, 0); lv_obj_set_style_line_color(l, lv_color_hex(col), 0);
  lv_obj_set_style_line_dash_width(l, dash, 0); lv_obj_set_style_line_dash_gap(l, dash, 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

static void gm_build(lv_obj_t *t) {
  lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  gm_prefs_load();
  // ---- play field (also the backdrop of game over) ----
  gp_play = lv_obj_create(t); lv_obj_remove_style_all(gp_play);
  lv_obj_set_size(gp_play, 360, 360); lv_obj_set_style_bg_opa(gp_play, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(gp_play, lv_color_hex(0x5BA34A), 0);
  lv_obj_clear_flag(gp_play, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(gp_play, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gp_play, gm_play_tap_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_clip_corner(gp_play, false, 0);
  // grass tufts (static pixel detail)
  const int tufts[][2] = {{40, 120}, {62, 200}, {30, 250}, {80, 90}, {290, 110}, {310, 190}, {280, 260}, {320, 230}, {70, 290}, {300, 300}};
  for (auto &tf : tufts) gm_rect(gp_play, tf[0], tf[1], 6, 3, 0x4C8E3E);
  gm_rect(gp_play, ROAD_L, 0, ROAD_R - ROAD_L, 360, 0x4A4A50);
  gm_rect(gp_play, ROAD_L - 6, 0, 6, 360, 0xF4F4F4);
  gm_rect(gp_play, ROAD_R, 0, 6, 360, 0xF4F4F4);
  g_kerb[0] = gm_vline(gp_play, ROAD_L - 6, 6, 0xD84A3A, 24);
  g_kerb[1] = gm_vline(gp_play, ROAD_R, 6, 0xD84A3A, 24);
  g_dash[0] = gm_vline(gp_play, 153, 4, 0xF2F2F2, 24);
  g_dash[1] = gm_vline(gp_play, 203, 4, 0xF2F2F2, 24);
  for (auto &o : obs) { o.img = lv_img_create(gp_play); lv_img_set_src(o.img, &spr_car_red); lv_obj_add_flag(o.img, LV_OBJ_FLAG_HIDDEN); o.on = false; }
  g_player = lv_img_create(gp_play); lv_img_set_src(g_player, &spr_player); lv_obj_set_pos(g_player, 165, 270);
  lv_obj_t *sc = lv_obj_create(gp_play); lv_obj_remove_style_all(sc);
  lv_obj_set_size(sc, 64, 30); lv_obj_align(sc, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_bg_opa(sc, LV_OPA_80, 0); lv_obj_set_style_bg_color(sc, lv_color_hex(0x1E1E22), 0);
  lv_obj_set_style_radius(sc, 6, 0); lv_obj_clear_flag(sc, LV_OBJ_FLAG_CLICKABLE);
  g_score_lbl = lv_label_create(sc); lv_obj_set_style_text_font(g_score_lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(g_score_lbl, lv_color_hex(0xFFFFFF), 0); lv_label_set_text(g_score_lbl, "0"); lv_obj_center(g_score_lbl);
  g_pause_lbl = ui_label(gp_play, &lv_font_montserrat_16, 0xFFFFFF, 200, "Paused\ntap to resume");
  lv_obj_set_style_bg_opa(g_pause_lbl, LV_OPA_80, 0); lv_obj_set_style_bg_color(g_pause_lbl, lv_color_hex(0x1E1E22), 0);
  lv_obj_set_style_pad_all(g_pause_lbl, 10, 0); lv_obj_set_style_radius(g_pause_lbl, 8, 0);
  lv_obj_align(g_pause_lbl, LV_ALIGN_CENTER, 0, 0); lv_obj_add_flag(g_pause_lbl, LV_OBJ_FLAG_HIDDEN);

  // ---- idle / start screen ----
  gp_idle = lv_obj_create(t); lv_obj_remove_style_all(gp_idle);
  lv_obj_set_size(gp_idle, 360, 360); lv_obj_clear_flag(gp_idle, LV_OBJ_FLAG_SCROLLABLE);
  ui_title(gp_idle, "Drive");
  lv_obj_t *pc = lv_img_create(gp_idle); lv_img_set_src(pc, &spr_player); lv_obj_align(pc, LV_ALIGN_TOP_MID, -34, 74);
  lv_obj_t *ec = lv_img_create(gp_idle); lv_img_set_src(ec, &spr_car_red); lv_obj_align(ec, LV_ALIGN_TOP_MID, 6, 62);
  lv_obj_t *cn = lv_img_create(gp_idle); lv_img_set_src(cn, &spr_cone); lv_obj_align(cn, LV_ALIGN_TOP_MID, 44, 98);
  lv_obj_t *sb = ui_btn(gp_idle, 150, 54, LV_SYMBOL_PLAY "  Start", NULL);
  lv_obj_set_style_radius(sb, 27, 0); lv_obj_set_style_bg_color(sb, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_text_color(sb, lv_color_hex(0xFFFFFF), 0); lv_obj_set_style_text_font(sb, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(sb, lv_color_hex(0x55534E), LV_STATE_PRESSED);
  lv_obj_align(sb, LV_ALIGN_TOP_MID, 0, 150); lv_obj_add_event_cb(sb, gm_start_cb, LV_EVENT_CLICKED, NULL);
  g_best_lbl = ui_label(gp_idle, &lv_font_montserrat_16, C_TEXT, 0, "Best  0"); lv_obj_align(g_best_lbl, LV_ALIGN_TOP_MID, 0, 218);
  g_imu_lbl = ui_label(gp_idle, &lv_font_montserrat_12, C_GREY, 220, "Hold level, tap Start, tilt to steer");
  lv_obj_align(g_imu_lbl, LV_ALIGN_TOP_MID, 0, 246);
  lv_obj_t *ab = ui_btn(gp_idle, 150, 30, "", &g_axis_lbl);
  lv_obj_set_style_border_width(ab, 0, 0); lv_obj_set_style_text_font(g_axis_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_axis_lbl, lv_color_hex(C_GREY), 0);
  lv_obj_align(ab, LV_ALIGN_TOP_MID, 0, 278); lv_obj_add_event_cb(ab, gm_axis_cb, LV_EVENT_CLICKED, NULL);

  // ---- game over card ----
  gp_over = lv_obj_create(t); lv_obj_remove_style_all(gp_over);
  lv_obj_set_size(gp_over, 220, 210); lv_obj_align(gp_over, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(gp_over, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(gp_over, lv_color_hex(C_BG), 0);
  lv_obj_set_style_radius(gp_over, 14, 0); lv_obj_set_style_border_width(gp_over, 1, 0);
  lv_obj_set_style_border_color(gp_over, lv_color_hex(C_LINE), 0);
  lv_obj_clear_flag(gp_over, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(gp_over, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gp_over, gm_over_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *go = ui_label(gp_over, &lv_font_montserrat_16, C_GREY, 0, "Game over"); lv_obj_align(go, LV_ALIGN_TOP_MID, 0, 14);
  g_over_score = ui_label(gp_over, &lv_font_montserrat_48, C_TEXT, 0, "0"); lv_obj_align(g_over_score, LV_ALIGN_TOP_MID, 0, 38);
  g_over_new = ui_label(gp_over, &lv_font_montserrat_12, C_RED, 0, "New best!"); lv_obj_align(g_over_new, LV_ALIGN_TOP_MID, 0, 96);
  g_over_best = ui_label(gp_over, &lv_font_montserrat_14, C_TEXT, 0, "Best  0"); lv_obj_align(g_over_best, LV_ALIGN_TOP_MID, 0, 116);
  lv_obj_t *ta = ui_label(gp_over, &lv_font_montserrat_12, C_GREY, 0, "Tap to play again"); lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 142);
  lv_obj_t *mb = ui_btn(gp_over, 90, 30, "Menu", NULL); lv_obj_align(mb, LV_ALIGN_TOP_MID, 0, 166);
  lv_obj_add_event_cb(mb, gm_menu_cb, LV_EVENT_CLICKED, NULL);

  gm_set(GM_IDLE);
}

// called every loop; runs a frame every 33 ms (~30 fps) only while playing on-screen
static void gm_tick(uint32_t now, bool visible) {
  static uint32_t last = 0;
  if (gm == GM_PLAY && !visible) { gm_set(GM_PAUSE); return; }
  if (gm != GM_PLAY || now - last < 33) return;
  last = now;
  gm_frame_tick(now);
}
