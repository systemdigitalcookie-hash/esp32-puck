// ================= Flappy Bird game module (header-only) =================
// API: flappy_open(parent, on_exit) / flappy_close() / flappy_running(). Best score: NVS "games"/"flappy".
// Needs ui_style.h (ui_btn/ui_label/C_*) and <Preferences.h> included before. Own pixel art, ~30 fps single lv_timer.
#pragma once
#include <Preferences.h>
#include "game_flappy_icon.h"

#define FLAPPY_NP 3          // pipe pairs
#define FLAPPY_PW 52         // pipe width
#define FLAPPY_BX 110        // bird x (left)
#define FLAPPY_GY 300        // ground top
enum { FLAPPY_IDLE = 0, FLAPPY_PLAY, FLAPPY_OVER };
struct FlappyPipe { lv_obj_t *c, *t, *tc, *b, *bc; float x; int gy, gap; bool passed; };
static lv_obj_t *flappy_root = NULL, *flappy_bird, *flappy_hud, *flappy_idle, *flappy_card, *flappy_cs, *flappy_cb, *flappy_cn;
static lv_obj_t *flappy_stripe[16];
static FlappyPipe flappy_pipe[FLAPPY_NP];
static lv_timer_t *flappy_tm = NULL;
static void (*flappy_exit_cb)(void) = NULL;
static int flappy_state = FLAPPY_IDLE, flappy_score = 0, flappy_best = 0;
static float flappy_y = 150, flappy_vy = 0, flappy_spd = 2.6f, flappy_gx = 0;
static uint32_t flappy_last = 0;
static bool flappy_want_exit = false;
inline void flappy_close(void);

static lv_obj_t *flappy_rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t col, int bw = 0, uint32_t bcol = 0x000000) {
  lv_obj_t *r = lv_obj_create(p); lv_obj_remove_style_all(r);
  lv_obj_set_pos(r, x, y); lv_obj_set_size(r, w, h);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, lv_color_hex(col), 0);
  if (bw) { lv_obj_set_style_border_width(r, bw, 0); lv_obj_set_style_border_color(r, lv_color_hex(bcol), 0); }
  lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return r;
}
static lv_obj_t *flappy_make_bird(lv_obj_t *p) {   // 30x24 pixel bird
  lv_obj_t *c = flappy_rect(p, 0, 0, 30, 24, 0xF8C830, 2, 0x4A3410);
  flappy_rect(c, -2, 8, 14, 9, 0xFFF2B0, 2, 0x4A3410);       // wing
  flappy_rect(c, 17, 2, 9, 9, 0xFFFFFF, 2, 0x4A3410);        // eye
  flappy_rect(c, 22, 5, 4, 4, 0x202020);                      // pupil
  flappy_rect(c, 24, 13, 10, 7, 0xF05A28, 2, 0x4A3410);      // beak
  return c;
}
static void flappy_pipe_layout(FlappyPipe &p) {
  int top = p.gy - p.gap / 2, bot = p.gy + p.gap / 2;
  lv_obj_set_pos(p.t, 4, 0);            lv_obj_set_size(p.t, FLAPPY_PW - 8, top);
  lv_obj_set_pos(p.tc, 0, top - 16);    lv_obj_set_size(p.tc, FLAPPY_PW, 16);
  lv_obj_set_pos(p.bc, 0, bot);         lv_obj_set_size(p.bc, FLAPPY_PW, 16);
  lv_obj_set_pos(p.b, 4, bot + 16);     lv_obj_set_size(p.b, FLAPPY_PW - 8, FLAPPY_GY - bot - 16);
}
static int flappy_gap() { int g = 112 - flappy_score * 2; return g < 86 ? 86 : g; }
static void flappy_respawn(FlappyPipe &p, float x) {
  p.x = x; p.gap = flappy_gap(); p.passed = false;
  int lo = 40 + p.gap / 2, hi = FLAPPY_GY - 40 - p.gap / 2;
  p.gy = lo + (int)(esp_random() % (uint32_t)(hi - lo));
  flappy_pipe_layout(p); lv_obj_set_x(p.c, (int)x);
}
static void flappy_reset() {
  flappy_score = 0; flappy_y = 150; flappy_vy = 0; flappy_spd = 2.6f; flappy_gx = 0;
  for (int i = 0; i < FLAPPY_NP; i++) flappy_respawn(flappy_pipe[i], 400 + i * 175);
  lv_obj_set_pos(flappy_bird, FLAPPY_BX, (int)flappy_y);
  lv_label_set_text(flappy_hud, "0");
}
static void flappy_set_state(int s) {
  flappy_state = s;
  if (s == FLAPPY_IDLE) lv_obj_clear_flag(flappy_idle, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(flappy_idle, LV_OBJ_FLAG_HIDDEN);
  if (s == FLAPPY_OVER) lv_obj_clear_flag(flappy_card, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(flappy_card, LV_OBJ_FLAG_HIDDEN);
  if (s == FLAPPY_PLAY) lv_obj_clear_flag(flappy_hud, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(flappy_hud, LV_OBJ_FLAG_HIDDEN);
}
static void flappy_game_over() {
  bool nb = flappy_score > flappy_best;
  if (nb) { flappy_best = flappy_score; Preferences p; if (p.begin("games", false)) { p.putInt("flappy", flappy_best); p.end(); } }
  char b[24];
  snprintf(b, sizeof b, "%d", flappy_score); lv_label_set_text(flappy_cs, b);
  snprintf(b, sizeof b, "Best  %d", flappy_best); lv_label_set_text(flappy_cb, b);
  if (nb && flappy_score > 0) lv_obj_clear_flag(flappy_cn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(flappy_cn, LV_OBJ_FLAG_HIDDEN);
  flappy_set_state(FLAPPY_OVER);
}
static void flappy_tap_cb(lv_event_t *e) {
  if (flappy_state == FLAPPY_IDLE) { flappy_reset(); flappy_set_state(FLAPPY_PLAY); flappy_last = lv_tick_get(); flappy_vy = -6.4f; }
  else if (flappy_state == FLAPPY_PLAY) flappy_vy = -6.4f;
}
static void flappy_again_cb(lv_event_t *e) { flappy_reset(); flappy_set_state(FLAPPY_PLAY); flappy_last = lv_tick_get(); flappy_vy = -6.4f; }
static void flappy_back_cb(lv_event_t *e) { flappy_want_exit = true; }   // handled in timer: never delete inside own event

static void flappy_step(float k) {
  flappy_vy += 0.42f * k; if (flappy_vy > 9) flappy_vy = 9;
  flappy_y += flappy_vy * k;
  if (flappy_y < 0) { flappy_y = 0; if (flappy_vy < 0) flappy_vy = 0; }
  lv_obj_set_y(flappy_bird, (int)flappy_y);
  float sp = flappy_spd * k;
  flappy_gx += sp; if (flappy_gx >= 24) flappy_gx -= 24;
  for (int i = 0; i < 16; i++) lv_obj_set_x(flappy_stripe[i], i * 24 - (int)flappy_gx);
  const int bx0 = FLAPPY_BX + 3, bx1 = FLAPPY_BX + 28, by0 = (int)flappy_y + 3, by1 = (int)flappy_y + 22;
  bool hit = by1 >= FLAPPY_GY;
  float far = 0;
  for (auto &p : flappy_pipe) if (p.x > far) far = p.x;
  for (auto &p : flappy_pipe) {
    p.x -= sp;
    if (p.x < -FLAPPY_PW) { flappy_respawn(p, far + 175); far = p.x; continue; }
    lv_obj_set_x(p.c, (int)p.x);
    int px0 = (int)p.x + 2, px1 = (int)p.x + FLAPPY_PW - 2;
    if (bx1 > px0 && bx0 < px1 && (by0 < p.gy - p.gap / 2 || by1 > p.gy + p.gap / 2)) hit = true;
    if (!p.passed && px1 < bx0) {
      p.passed = true; flappy_score++;
      char b[12]; snprintf(b, sizeof b, "%d", flappy_score); lv_label_set_text(flappy_hud, b);
      if (flappy_spd < 4.4f) flappy_spd += 0.08f;
    }
  }
  if (hit) flappy_game_over();
}
static void flappy_timer_cb(lv_timer_t *t) {
  if (flappy_want_exit) {
    flappy_want_exit = false;
    void (*cb)(void) = flappy_exit_cb;
    flappy_close();
    if (cb) cb();
    return;
  }
  uint32_t now = lv_tick_get();
  if (flappy_state != FLAPPY_PLAY) { flappy_last = now; return; }
  uint32_t dt = now - flappy_last; flappy_last = now;
  float k = dt / 33.0f; if (k > 2.5f) k = 2.5f;
  flappy_step(k);
}
static void flappy_root_del_cb(lv_event_t *e) {   // root deleted (by us or by the hub): drop timer + pointers
  if (flappy_tm) { lv_timer_del(flappy_tm); flappy_tm = NULL; }
  flappy_root = NULL; flappy_state = FLAPPY_IDLE;
}

inline bool flappy_running(void) { return flappy_root && flappy_state == FLAPPY_PLAY; }
inline void flappy_close(void) {
  if (flappy_tm) { lv_timer_del(flappy_tm); flappy_tm = NULL; }
  if (flappy_root) { lv_obj_t *r = flappy_root; flappy_root = NULL; lv_obj_del(r); }
  flappy_state = FLAPPY_IDLE; flappy_want_exit = false;
}
inline void flappy_open(lv_obj_t *parent, void (*on_exit)(void)) {
  flappy_close();
  flappy_exit_cb = on_exit;
  { Preferences p; if (p.begin("games", true)) { flappy_best = p.getInt("flappy", 0); p.end(); } }
  lv_obj_t *r = flappy_root = lv_obj_create(parent); lv_obj_remove_style_all(r);
  lv_obj_set_size(r, 360, 360); lv_obj_set_pos(r, 0, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, lv_color_hex(0x6EC6E8), 0);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(r, flappy_tap_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(r, flappy_root_del_cb, LV_EVENT_DELETE, NULL);
  // clouds + distant hills
  const int cl[][3] = {{30, 70, 56}, {230, 50, 64}, {150, 120, 44}, {290, 150, 48}};
  for (auto &c : cl) { flappy_rect(r, c[0], c[1], c[2], 14, 0xFFFFFF); flappy_rect(r, c[0] + 10, c[1] - 8, c[2] - 22, 10, 0xFFFFFF); }
  for (int i = 0; i < 6; i++) flappy_rect(r, i * 64 - 10, 262 - (i & 1) * 14, 64, 40, 0x8FDB8A);
  // pipes
  for (auto &p : flappy_pipe) {
    p.c = flappy_rect(r, 0, 0, FLAPPY_PW, FLAPPY_GY, 0, 0); lv_obj_set_style_bg_opa(p.c, LV_OPA_TRANSP, 0);
    p.t = flappy_rect(p.c, 0, 0, 10, 10, 0x73BF2E, 2, 0x35601A);
    p.tc = flappy_rect(p.c, 0, 0, 10, 10, 0x8BD43C, 2, 0x35601A);
    p.b = flappy_rect(p.c, 0, 0, 10, 10, 0x73BF2E, 2, 0x35601A);
    p.bc = flappy_rect(p.c, 0, 0, 10, 10, 0x8BD43C, 2, 0x35601A);
  }
  // ground
  flappy_rect(r, 0, FLAPPY_GY, 360, 60, 0xDED895, 2, 0x4A3410);
  flappy_rect(r, 0, FLAPPY_GY, 360, 10, 0x6CC03A);
  for (int i = 0; i < 16; i++) { flappy_stripe[i] = flappy_rect(r, i * 24, FLAPPY_GY + 10, 12, 6, 0x5AA52E); }
  flappy_bird = flappy_make_bird(r);
  flappy_hud = ui_label(r, &lv_font_montserrat_48, 0xFFFFFF, 0, "0");
  lv_obj_align(flappy_hud, LV_ALIGN_TOP_MID, 0, 24); lv_obj_clear_flag(flappy_hud, LV_OBJ_FLAG_CLICKABLE);
  // start screen
  flappy_idle = lv_obj_create(r); lv_obj_remove_style_all(flappy_idle);
  lv_obj_set_size(flappy_idle, 360, 360); lv_obj_clear_flag(flappy_idle, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *tb = flappy_rect(flappy_idle, 80, 48, 200, 78, 0xFFF2B0, 3, 0x4A3410);
  lv_obj_set_style_radius(tb, 10, 0);
  lv_obj_t *tl = ui_label(tb, &lv_font_montserrat_20, 0x4A3410, 0, "Flappy Bird"); lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 12);
  char bs[24]; snprintf(bs, sizeof bs, "Best  %d", flappy_best);
  lv_obj_t *bl = ui_label(tb, &lv_font_montserrat_16, 0x4A3410, 0, bs); lv_obj_align(bl, LV_ALIGN_TOP_MID, 0, 42);
  lv_obj_t *tt = ui_label(flappy_idle, &lv_font_montserrat_20, 0xFFFFFF, 0, "Tap to start"); lv_obj_align(tt, LV_ALIGN_TOP_MID, 0, 232);
  lv_obj_set_style_bg_opa(tt, LV_OPA_50, 0); lv_obj_set_style_bg_color(tt, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(tt, 6, 0); lv_obj_set_style_radius(tt, 8, 0);
  // game over card
  flappy_card = lv_obj_create(r); lv_obj_remove_style_all(flappy_card);
  lv_obj_set_size(flappy_card, 236, 226); lv_obj_align(flappy_card, LV_ALIGN_CENTER, 0, -6);
  lv_obj_set_style_bg_opa(flappy_card, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(flappy_card, lv_color_hex(C_BG), 0);
  lv_obj_set_style_radius(flappy_card, 14, 0); lv_obj_set_style_border_width(flappy_card, 1, 0);
  lv_obj_set_style_border_color(flappy_card, lv_color_hex(C_LINE), 0);
  lv_obj_clear_flag(flappy_card, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(flappy_card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *go = ui_label(flappy_card, &lv_font_montserrat_16, C_GREY, 0, "Game over"); lv_obj_align(go, LV_ALIGN_TOP_MID, 0, 12);
  flappy_cs = ui_label(flappy_card, &lv_font_montserrat_48, C_TEXT, 0, "0"); lv_obj_align(flappy_cs, LV_ALIGN_TOP_MID, 0, 34);
  flappy_cn = ui_label(flappy_card, &lv_font_montserrat_12, C_RED, 0, "New best!"); lv_obj_align(flappy_cn, LV_ALIGN_TOP_MID, 0, 92);
  flappy_cb = ui_label(flappy_card, &lv_font_montserrat_14, C_TEXT, 0, "Best  0"); lv_obj_align(flappy_cb, LV_ALIGN_TOP_MID, 0, 112);
  lv_obj_t *pa = ui_btn(flappy_card, 150, 34, "Play again", NULL); lv_obj_align(pa, LV_ALIGN_TOP_MID, 0, 140);
  lv_obj_add_event_cb(pa, flappy_again_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *bk = ui_btn(flappy_card, 150, 34, "Back", NULL); lv_obj_align(bk, LV_ALIGN_TOP_MID, 0, 182);
  lv_obj_add_event_cb(bk, flappy_back_cb, LV_EVENT_CLICKED, NULL);
  flappy_reset();
  flappy_set_state(FLAPPY_IDLE);
  flappy_want_exit = false;
  flappy_tm = lv_timer_create(flappy_timer_cb, 33, NULL);
}
