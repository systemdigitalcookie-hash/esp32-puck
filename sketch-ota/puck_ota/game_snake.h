// ================= Snake: pixel snake for the round puck (header-only, prefix snake_) =================
// snake_open(parent,on_exit) builds start screen -> play -> game-over inside a 360x360 container.
// Control: swipe (default) or tilt (QMI8658 @0x6B, minimal own read code; Wire is already started). NVS "games": snake, snake_ctl.
#pragma once
#include <Preferences.h>
#include <Wire.h>
#include "lvgl.h"
#include "game_snake_icon.h"

#define SNAKE_N 20          // 20x20 grid
#define SNAKE_C 12          // 12 px cells -> 240 px square, fits inside the 360 circle
#define SNAKE_OX 60
#define SNAKE_OY 60
enum { SNAKE_IDLE = 0, SNAKE_PLAY, SNAKE_PAUSE, SNAKE_OVER };

static lv_obj_t *snake_root = NULL, *snake_p_idle, *snake_p_play, *snake_p_over, *snake_cv, *snake_score_l, *snake_best_l,
                *snake_ctl_l, *snake_pause_l, *snake_o_score, *snake_o_best, *snake_o_new;
static lv_timer_t *snake_tm = NULL;
static void (*snake_exit_cb)(void) = NULL;
static lv_color_t *snake_buf = NULL;
static int snake_state = SNAKE_IDLE, snake_best = 0, snake_score = 0, snake_ctl = 0;   // ctl 0 swipe, 1 tilt
static uint8_t snake_x[SNAKE_N * SNAKE_N], snake_y[SNAKE_N * SNAKE_N];
static int snake_len, snake_dx, snake_dy, snake_ndx, snake_ndy, snake_fx, snake_fy;
static uint32_t snake_last = 0;
static lv_point_t snake_pt0; static bool snake_moved = false;
static bool snake_imu_ok = false, snake_imu_tried = false;
static float snake_a0[2];

static bool snake_qw(uint8_t r, uint8_t v) { Wire.beginTransmission(0x6B); Wire.write(r); Wire.write(v); return Wire.endTransmission(true) == 0; }
static bool snake_qr(uint8_t r, uint8_t *b, int n) {
  Wire.beginTransmission(0x6B); Wire.write(r);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x6B, (uint8_t)n) != n) return false;
  for (int i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}
static bool snake_imu_read(float *g) {   // g[0] = X, g[1] = Y in g
  if (!snake_imu_tried) {
    snake_imu_tried = true; uint8_t id = 0;
    snake_imu_ok = snake_qr(0x00, &id, 1) && id == 0x05 && snake_qw(0x02, 0x40) && snake_qw(0x03, 0x15) && snake_qw(0x08, 0x01);
  }
  uint8_t b[6];
  if (!snake_imu_ok || !snake_qr(0x35, b, 6)) return false;
  for (int i = 0; i < 2; i++) g[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8)) / 8192.0f;
  return true;
}

static void snake_load() {
  Preferences p;
  if (p.begin("games", true)) { snake_best = p.getInt("snake", 0); snake_ctl = p.getInt("snake_ctl", 0) & 1; p.end(); }
}
static void snake_save(const char *k, int v) { Preferences p; if (p.begin("games", false)) { p.putInt(k, v); p.end(); } }

// v14: cells are written straight into the canvas buffer; only the touched cells are invalidated
#define SNAKE_W (SNAKE_N * SNAKE_C)
static void snake_fillpx(int x, int y, int w, int h, lv_color_t c) {
  for (int j = 0; j < h; j++) { lv_color_t *p = snake_buf + (y + j) * SNAKE_W + x; for (int i = 0; i < w; i++) p[i] = c; }
}
static void snake_inval(int cx, int cy) {
  lv_area_t a; lv_obj_get_coords(snake_cv, &a);
  a.x1 += cx * SNAKE_C; a.y1 += cy * SNAKE_C; a.x2 = a.x1 + SNAKE_C - 1; a.y2 = a.y1 + SNAKE_C - 1;
  lv_obj_invalidate_area(snake_cv, &a);
}
static lv_color_t snake_bgc(int x, int y) { return lv_color_hex(((x + y) & 1) ? 0x2b6c3a : 0x266234); }
static void snake_bg(int cx, int cy) { snake_fillpx(cx * SNAKE_C, cy * SNAKE_C, SNAKE_C, SNAKE_C, snake_bgc(cx, cy)); snake_inval(cx, cy); }
static void snake_cell(int cx, int cy, uint32_t col, int r) {   // r>0: trim the 4 corner pixels
  snake_fillpx(cx * SNAKE_C, cy * SNAKE_C, SNAKE_C, SNAKE_C, snake_bgc(cx, cy));
  snake_fillpx(cx * SNAKE_C + 1, cy * SNAKE_C + 1, SNAKE_C - 1, SNAKE_C - 1, lv_color_hex(col));
  if (r > 0) { lv_color_t b = snake_bgc(cx, cy); int x0 = cx * SNAKE_C + 1, y0 = cy * SNAKE_C + 1, e = SNAKE_C - 2;
    snake_buf[y0 * SNAKE_W + x0] = b; snake_buf[y0 * SNAKE_W + x0 + e] = b; snake_buf[(y0 + e) * SNAKE_W + x0] = b; snake_buf[(y0 + e) * SNAKE_W + x0 + e] = b; }
  snake_inval(cx, cy);
}
static void snake_head() {
  int cx = snake_x[0], cy = snake_y[0];
  snake_cell(cx, cy, 0xFFFFFF, 1);
  int hx = cx * SNAKE_C, hy = cy * SNAKE_C; lv_color_t k = lv_color_hex(0);
  int ex = snake_dx > 0 ? 8 : (snake_dx < 0 ? 2 : 0), ey = snake_dy > 0 ? 8 : (snake_dy < 0 ? 2 : 0);
  if (snake_dx) { snake_fillpx(hx + ex, hy + 3, 2, 2, k); snake_fillpx(hx + ex, hy + 8, 2, 2, k); }
  else { snake_fillpx(hx + 3, hy + ey, 2, 2, k); snake_fillpx(hx + 8, hy + ey, 2, 2, k); }
}
static uint32_t snake_bodyc(int x, int y) { return ((x + y) & 1) ? 0xF2F2EA : 0xC8D8B0; }
static void snake_draw() {   // full redraw: only on start
  if (!snake_cv || !snake_buf) return;
  for (int y = 0; y < SNAKE_N; y++) for (int x = 0; x < SNAKE_N; x++) snake_fillpx(x * SNAKE_C, y * SNAKE_C, SNAKE_C, SNAKE_C, snake_bgc(x, y));
  lv_obj_invalidate(snake_cv);
  snake_cell(snake_fx, snake_fy, 0xE03E3E, 1);
  for (int i = snake_len - 1; i > 0; i--) snake_cell(snake_x[i], snake_y[i], snake_bodyc(snake_x[i], snake_y[i]), 0);
  snake_head();
}
static void snake_place_food() {
  for (int tries = 0; tries < 2000; tries++) {
    int x = lv_rand(0, SNAKE_N - 1), y = lv_rand(0, SNAKE_N - 1); bool ok = true;
    for (int i = 0; i < snake_len; i++) if (snake_x[i] == x && snake_y[i] == y) { ok = false; break; }
    if (ok) { snake_fx = x; snake_fy = y; return; }
  }
}
static void snake_turn(int dx, int dy) {   // no 180 reversal (checked against current heading)
  if (dx == -snake_dx && dy == -snake_dy) return;
  if (dx == snake_dx && dy == snake_dy) return;
  snake_ndx = dx; snake_ndy = dy;
}
static void snake_show(int st) {
  snake_state = st;
  lv_obj_add_flag(snake_p_idle, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(snake_p_play, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(snake_p_over, LV_OBJ_FLAG_HIDDEN);
  if (st == SNAKE_IDLE) lv_obj_clear_flag(snake_p_idle, LV_OBJ_FLAG_HIDDEN);
  else if (st == SNAKE_OVER) lv_obj_clear_flag(snake_p_over, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(snake_p_play, LV_OBJ_FLAG_HIDDEN);
  if (snake_pause_l) { if (st == SNAKE_PAUSE) lv_obj_clear_flag(snake_pause_l, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(snake_pause_l, LV_OBJ_FLAG_HIDDEN); }
}
static uint32_t snake_iv() { int iv = 200 - 6 * snake_score; return iv < 85 ? 85 : iv; }
static void snake_start() {
  snake_len = 3; snake_score = 0; snake_dx = 1; snake_dy = 0; snake_ndx = 1; snake_ndy = 0;
  for (int i = 0; i < 3; i++) { snake_x[i] = 10 - i; snake_y[i] = 10; }
  snake_place_food();
  if (snake_ctl) { float g[2]; float a0 = 0, a1 = 0; int n = 0; for (int i = 0; i < 6; i++) { if (snake_imu_read(g)) { a0 += g[0]; a1 += g[1]; n++; } delay(4); }
    snake_a0[0] = n ? a0 / n : 0; snake_a0[1] = n ? a1 / n : 0; }
  lv_label_set_text(snake_score_l, "0");
  snake_last = lv_tick_get(); snake_moved = false;
  snake_show(SNAKE_PLAY); snake_draw();
  if (snake_tm) { lv_timer_set_period(snake_tm, snake_iv()); lv_timer_reset(snake_tm); }   // first move one interval after start
}
static void snake_end() {
  bool nb = snake_score > snake_best;
  if (nb) { snake_best = snake_score; snake_save("snake", snake_best); }
  char b[32];
  snprintf(b, sizeof b, "Score  %d", snake_score); lv_label_set_text(snake_o_score, b);
  snprintf(b, sizeof b, "Best  %d", snake_best); lv_label_set_text(snake_o_best, b);
  if (nb && snake_score > 0) lv_obj_clear_flag(snake_o_new, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(snake_o_new, LV_OBJ_FLAG_HIDDEN);
  snake_show(SNAKE_OVER);
}
static void snake_step() {
  snake_dx = snake_ndx; snake_dy = snake_ndy;
  int nx = snake_x[0] + snake_dx, ny = snake_y[0] + snake_dy;
  if (nx < 0 || ny < 0 || nx >= SNAKE_N || ny >= SNAKE_N) { snake_end(); return; }
  bool eat = (nx == snake_fx && ny == snake_fy);
  int otx = snake_x[snake_len - 1], oty = snake_y[snake_len - 1], ohx = snake_x[0], ohy = snake_y[0];
  int chk = eat ? snake_len : snake_len - 1;   // tail cell frees up when not growing
  for (int i = 0; i < chk; i++) if (snake_x[i] == nx && snake_y[i] == ny) { snake_end(); return; }
  if (eat && snake_len < SNAKE_N * SNAKE_N) snake_len++;
  for (int i = snake_len - 1; i > 0; i--) { snake_x[i] = snake_x[i - 1]; snake_y[i] = snake_y[i - 1]; }
  snake_x[0] = nx; snake_y[0] = ny;
  if (eat) {
    snake_score++; char b[12]; snprintf(b, sizeof b, "%d", snake_score); lv_label_set_text(snake_score_l, b);
    if (snake_len >= SNAKE_N * SNAKE_N) { snake_end(); return; }
    snake_place_food(); snake_cell(snake_fx, snake_fy, 0xE03E3E, 1);
  } else snake_bg(otx, oty);
  snake_cell(ohx, ohy, snake_bodyc(ohx, ohy), 0);
  snake_head();
}
static void snake_tick(lv_timer_t *) {
  if (snake_state != SNAKE_PLAY) return;
  if (snake_ctl) {
    float g[2];
    if (snake_imu_read(g)) {
      float tx = g[0] - snake_a0[0], ty = g[1] - snake_a0[1];
      if (fabsf(tx) > 0.25f || fabsf(ty) > 0.25f) {
        if (fabsf(tx) >= fabsf(ty)) snake_turn(tx > 0 ? 1 : -1, 0); else snake_turn(0, ty > 0 ? -1 : 1);
      }
    }
  }
  snake_last = lv_tick_get(); snake_step();
  if (snake_tm) lv_timer_set_period(snake_tm, snake_iv());   // v16: the timer period IS the move interval (no 20 ms polling jitter)
}
static void snake_touch_cb(lv_event_t *e) {
  if (snake_state != SNAKE_PLAY && snake_state != SNAKE_PAUSE) return;
  lv_indev_t *in = lv_indev_get_act(); if (!in) return;
  lv_point_t p; lv_indev_get_point(in, &p);
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_PRESSED) { snake_pt0 = p; snake_moved = false; }
  else if (c == LV_EVENT_PRESSING && snake_state == SNAKE_PLAY && !snake_ctl) {
    int dx = p.x - snake_pt0.x, dy = p.y - snake_pt0.y;
    if (abs(dx) >= 22 || abs(dy) >= 22) {
      if (abs(dx) > abs(dy)) snake_turn(dx > 0 ? 1 : -1, 0); else snake_turn(0, dy > 0 ? 1 : -1);
      snake_pt0 = p; snake_moved = true;
    }
  }   // v14: no tap-to-pause
}
static void snake_btn_cb(lv_event_t *e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  if (id == 0) snake_start();
  else if (id == 1) { void (*cb)(void) = snake_exit_cb; if (cb) cb(); }
  else if (id == 2) {
    // (pause removed)
  } else if (id == 3) {
    snake_ctl ^= 1; snake_save("snake_ctl", snake_ctl);
    lv_label_set_text(snake_ctl_l, snake_ctl ? "Control: Tilt" : "Control: Swipe");
  }
}
static lv_obj_t *snake_btn(lv_obj_t *p, int w, int h, const char *t, int id, lv_obj_t **lbl = NULL) {
  lv_obj_t *b = lv_btn_create(p); lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0xF7F6F3), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0xDCDBD8), 0); lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_shadow_width(b, 0, 0); lv_obj_set_style_radius(b, 8, 0);
  lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, t); lv_obj_center(l);
  lv_obj_set_style_text_color(l, lv_color_hex(0x37352F), 0);
  lv_obj_add_event_cb(b, snake_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  if (lbl) *lbl = l;
  return b;
}
static lv_obj_t *snake_lbl(lv_obj_t *p, const lv_font_t *f, uint32_t col, const char *t, lv_align_t a, int y) {
  lv_obj_t *l = lv_label_create(p); lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_label_set_text(l, t); lv_obj_align(l, a, 0, y); return l;
}
static lv_obj_t *snake_panel(lv_obj_t *p) {
  lv_obj_t *o = lv_obj_create(p); lv_obj_set_size(o, 360, 360); lv_obj_set_pos(o, 0, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(0xFFFFFF), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_radius(o, 0, 0); lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

bool snake_running(void) { return snake_root != NULL && (snake_state == SNAKE_PLAY || snake_state == SNAKE_PAUSE); }

void snake_close(void) {
  if (snake_tm) { lv_timer_del(snake_tm); snake_tm = NULL; }
  if (snake_root) { lv_obj_del(snake_root); snake_root = NULL; }
  if (snake_buf) { heap_caps_free(snake_buf); snake_buf = NULL; }
  snake_cv = NULL; snake_pause_l = NULL; snake_state = SNAKE_IDLE; snake_exit_cb = NULL;
}

void snake_open(lv_obj_t *parent, void (*on_exit)(void)) {
  snake_close();
  snake_exit_cb = on_exit; snake_load();
  snake_buf = (lv_color_t *)heap_caps_malloc(SNAKE_N * SNAKE_C * SNAKE_N * SNAKE_C * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!snake_buf) { snake_buf = (lv_color_t *)malloc(SNAKE_N * SNAKE_C * SNAKE_N * SNAKE_C * sizeof(lv_color_t)); }
  if (!snake_buf) { Serial.println("[snake] no memory"); snake_exit_cb = NULL; if (on_exit) on_exit(); return; }
  snake_root = lv_obj_create(parent); lv_obj_set_size(snake_root, 360, 360); lv_obj_set_pos(snake_root, 0, 0);
  lv_obj_set_style_bg_opa(snake_root, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(snake_root, 0, 0); lv_obj_set_style_pad_all(snake_root, 0, 0);
  lv_obj_clear_flag(snake_root, LV_OBJ_FLAG_SCROLLABLE);

  // ---- start screen ----
  snake_p_idle = snake_panel(snake_root);
  snake_lbl(snake_p_idle, &lv_font_montserrat_20, 0x37352F, "Snake", LV_ALIGN_TOP_MID, 40);
  lv_obj_t *ic = lv_img_create(snake_p_idle); lv_img_set_src(ic, &game_icon_snake); lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 72);
  char b[24]; snprintf(b, sizeof b, "Best  %d", snake_best);
  snake_best_l = snake_lbl(snake_p_idle, &lv_font_montserrat_16, 0x787774, b, LV_ALIGN_TOP_MID, 176);
  snake_btn(snake_p_idle, 120, 40, "Tap to start", 0); lv_obj_align(lv_obj_get_child(snake_p_idle, -1), LV_ALIGN_TOP_MID, 0, 206);
  lv_obj_t *cb = snake_btn(snake_p_idle, 150, 32, snake_ctl ? "Control: Tilt" : "Control: Swipe", 3, &snake_ctl_l);
  lv_obj_align(cb, LV_ALIGN_TOP_MID, 0, 254);
  lv_obj_t *bk = snake_btn(snake_p_idle, 70, 28, "Back", 1); lv_obj_align(bk, LV_ALIGN_TOP_MID, 0, 292);

  // ---- play ----
  snake_p_play = snake_panel(snake_root);
  lv_obj_add_event_cb(snake_p_play, snake_touch_cb, LV_EVENT_ALL, NULL);
  snake_cv = lv_canvas_create(snake_p_play);
  lv_canvas_set_buffer(snake_cv, snake_buf, SNAKE_N * SNAKE_C, SNAKE_N * SNAKE_C, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(snake_cv, SNAKE_OX, SNAKE_OY);
  lv_obj_clear_flag(snake_cv, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_width(snake_cv, 0, 0);
  snake_score_l = snake_lbl(snake_p_play, &lv_font_montserrat_20, 0x37352F, "0", LV_ALIGN_TOP_MID, 22);
  lv_obj_t *pb = snake_btn(snake_p_play, 64, 26, "Back", 1); lv_obj_align(pb, LV_ALIGN_BOTTOM_MID, 0, -14);
  snake_pause_l = snake_lbl(snake_p_play, &lv_font_montserrat_20, 0xFFFFFF, "Paused - tap", LV_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(snake_pause_l, lv_color_hex(0x37352F), 0); lv_obj_set_style_bg_opa(snake_pause_l, LV_OPA_80, 0);
  lv_obj_set_style_pad_all(snake_pause_l, 8, 0); lv_obj_add_flag(snake_pause_l, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(snake_pause_l, LV_OBJ_FLAG_CLICKABLE);

  // ---- game over ----
  snake_p_over = snake_panel(snake_root);
  snake_lbl(snake_p_over, &lv_font_montserrat_20, 0xE03E3E, "Game over", LV_ALIGN_TOP_MID, 50);
  snake_o_score = snake_lbl(snake_p_over, &lv_font_montserrat_20, 0x37352F, "Score  0", LV_ALIGN_TOP_MID, 94);
  snake_o_best = snake_lbl(snake_p_over, &lv_font_montserrat_16, 0x787774, "Best  0", LV_ALIGN_TOP_MID, 130);
  snake_o_new = snake_lbl(snake_p_over, &lv_font_montserrat_16, 0x2b6c3a, "New best!", LV_ALIGN_TOP_MID, 160);
  lv_obj_t *pa = snake_btn(snake_p_over, 130, 40, "Play again", 0); lv_obj_align(pa, LV_ALIGN_TOP_MID, 0, 200);
  lv_obj_t *ob = snake_btn(snake_p_over, 130, 36, "Back", 1); lv_obj_align(ob, LV_ALIGN_TOP_MID, 0, 250);

  snake_show(SNAKE_IDLE);
  snake_tm = lv_timer_create(snake_tick, 200, NULL);
}
