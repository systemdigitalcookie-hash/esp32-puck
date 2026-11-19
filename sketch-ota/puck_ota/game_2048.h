// 2048 for the puck Games hub. Header-only, all symbols prefixed g2048_.
// API: g2048_open(parent,on_exit), g2048_close(), g2048_running(). Best score: NVS "games"/"2048".
#pragma once
#include <stdint.h>
#include <stdlib.h>

// ---------------- pure game logic (host-testable: #define G2048_LOGIC_ONLY) ----------------
typedef struct { uint8_t b[4][4]; uint32_t score; } g2048_board_t;   // cell = exponent (0 empty, 1=2, 2=4 ...)

// slide+merge one line toward index 0; returns true if changed; adds merge points to *score
static bool g2048_line(uint8_t *l, uint32_t *score, int *merged_mask) {
  uint8_t o[4] = {0, 0, 0, 0}; int n = 0; bool lastm = false; *merged_mask = 0;
  for (int i = 0; i < 4; i++) {
    if (!l[i]) continue;
    if (n > 0 && !lastm && o[n - 1] == l[i]) { o[n - 1]++; *score += 1u << o[n - 1]; *merged_mask |= 1 << (n - 1); lastm = true; }
    else { o[n++] = l[i]; lastm = false; }
  }
  bool ch = false;
  for (int i = 0; i < 4; i++) { if (l[i] != o[i]) ch = true; l[i] = o[i]; }
  return ch;
}
// dir: 0 left, 1 right, 2 up, 3 down. merged[r][c] marks cells created by a merge.
static bool g2048_move(g2048_board_t *g, int dir, bool merged[4][4]) {
  bool any = false;
  for (int k = 0; k < 4; k++) {
    uint8_t l[4]; int mm;
    for (int i = 0; i < 4; i++) {
      int r = dir < 2 ? k : (dir == 2 ? i : 3 - i), c = dir < 2 ? (dir == 0 ? i : 3 - i) : k;
      l[i] = g->b[r][c];
    }
    if (g2048_line(l, &g->score, &mm)) any = true;
    for (int i = 0; i < 4; i++) {
      int r = dir < 2 ? k : (dir == 2 ? i : 3 - i), c = dir < 2 ? (dir == 0 ? i : 3 - i) : k;
      g->b[r][c] = l[i]; if (merged) merged[r][c] = (mm >> i) & 1;
    }
  }
  return any;
}
static bool g2048_can_move(const g2048_board_t *g) {
  for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
    if (!g->b[r][c]) return true;
    if (c < 3 && g->b[r][c] == g->b[r][c + 1]) return true;
    if (r < 3 && g->b[r][c] == g->b[r + 1][c]) return true;
  }
  return false;
}
static int g2048_spawn(g2048_board_t *g) {   // returns cell index or -1
  int e[16], n = 0;
  for (int i = 0; i < 16; i++) if (!g->b[i / 4][i % 4]) e[n++] = i;
  if (!n) return -1;
  int p = e[rand() % n]; g->b[p / 4][p % 4] = (rand() % 10) ? 1 : 2; return p;
}

#ifndef G2048_LOGIC_ONLY
#include "lvgl.h"
#include <Preferences.h>

void g2048_open(lv_obj_t *parent, void (*on_exit)(void));
void g2048_close(void);
bool g2048_running(void);
static lv_obj_t *g2048_root = NULL, *g2048_idle, *g2048_play, *g2048_over, *g2048_grid;
static lv_obj_t *g2048_tile[16], *g2048_tl[16];
static lv_obj_t *g2048_score_l, *g2048_best_l, *g2048_idle_best, *g2048_o_title, *g2048_o_score, *g2048_o_best, *g2048_o_new, *g2048_o_btn1l;
static void (*g2048_exit_cb)(void) = NULL;
static g2048_board_t g2048_g;
static uint32_t g2048_best = 0;
static bool g2048_playing = false, g2048_won = false, g2048_cont = false;
static lv_point_t g2048_p0;

static const uint32_t g2048_col[12] = {0xCDC1B4, 0xEEE4DA, 0xEDE0C8, 0xF2B179, 0xF59563, 0xF67C5F, 0xF65E3B, 0xEDCF72, 0xEDCC61, 0xEDC850, 0xEDC53F, 0xEDC22E};

static void g2048_load() { Preferences p; if (p.begin("games", true)) { g2048_best = p.getUInt("2048", 0); p.end(); } }
static void g2048_save() { Preferences p; if (p.begin("games", false)) { p.putUInt("2048", g2048_best); p.end(); } }

static void g2048_pop_cb(void *o, int32_t v) { lv_obj_set_style_transform_zoom((lv_obj_t *)o, v, 0); }
static void g2048_pop(lv_obj_t *o, int from) {   // short zoom pop for new/merged tiles
  lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, o); lv_anim_set_exec_cb(&a, g2048_pop_cb);
  lv_anim_set_values(&a, from, 256); lv_anim_set_time(&a, 110); lv_anim_start(&a);
}
static void g2048_refresh(bool merged[4][4], int spawned) {
  char buf[12];
  for (int i = 0; i < 16; i++) {
    int v = g2048_g.b[i / 4][i % 4];
    lv_obj_t *t = g2048_tile[i];
    lv_obj_set_style_bg_color(t, lv_color_hex(g2048_col[v > 11 ? 11 : v]), 0);
    if (v) {
      snprintf(buf, sizeof buf, "%u", 1u << v);
      lv_label_set_text(g2048_tl[i], buf);
      lv_obj_set_style_text_font(g2048_tl[i], v < 7 ? &lv_font_montserrat_28 : (v < 10 ? &lv_font_montserrat_20 : &lv_font_montserrat_16), 0);
      lv_obj_set_style_text_color(g2048_tl[i], lv_color_hex(v < 3 ? 0x776E65 : 0xFFFFFF), 0);
      lv_obj_center(g2048_tl[i]);
    } else lv_label_set_text(g2048_tl[i], "");
    if (i == spawned) g2048_pop(t, 100);
    else if (merged && merged[i / 4][i % 4]) g2048_pop(t, 300);
  }
  snprintf(buf, sizeof buf, "%u", (unsigned)g2048_g.score); lv_label_set_text(g2048_score_l, buf);
  snprintf(buf, sizeof buf, "%u", (unsigned)g2048_best); lv_label_set_text(g2048_best_l, buf);
}
static void g2048_show(lv_obj_t *s) {
  lv_obj_add_flag(g2048_idle, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g2048_over, LV_OBJ_FLAG_HIDDEN);
  if (s == g2048_play) lv_obj_clear_flag(g2048_play, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);
  if (s == g2048_idle) lv_obj_add_flag(g2048_play, LV_OBJ_FLAG_HIDDEN);
}
static void g2048_new() {
  memset(&g2048_g, 0, sizeof g2048_g); g2048_won = g2048_cont = false;
  g2048_spawn(&g2048_g); int s = g2048_spawn(&g2048_g);
  g2048_playing = true; g2048_show(g2048_play); g2048_refresh(NULL, s);
}
static void g2048_card(bool win) {
  char b[16];
  g2048_playing = false;
  lv_label_set_text(g2048_o_title, win ? "You made 2048!" : "Game over");
  lv_label_set_text(g2048_o_btn1l, win ? "Keep going" : "Play again");
  snprintf(b, sizeof b, "%u", (unsigned)g2048_g.score); lv_label_set_text(g2048_o_score, b);
  snprintf(b, sizeof b, "Best  %u", (unsigned)g2048_best); lv_label_set_text(g2048_o_best, b);
  if (g2048_g.score >= g2048_best && g2048_g.score > 0) lv_obj_clear_flag(g2048_o_new, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g2048_o_new, LV_OBJ_FLAG_HIDDEN);
  g2048_show(g2048_over);
}
static void g2048_do_move(int dir) {
  bool m[4][4];
  if (!g2048_playing || !g2048_move(&g2048_g, dir, m)) return;
  int s = g2048_spawn(&g2048_g);
  if (g2048_g.score > g2048_best) { g2048_best = g2048_g.score; g2048_save(); }
  g2048_refresh(m, s);
  bool w = false;
  if (!g2048_won) for (int i = 0; i < 16; i++) if (g2048_g.b[i / 4][i % 4] >= 11) w = true;
  if (w) { g2048_won = true; g2048_card(true); }
  else if (!g2048_can_move(&g2048_g)) g2048_card(false);
}
static void g2048_touch_cb(lv_event_t *e) {
  if (!g2048_playing) return;
  lv_event_code_t c = lv_event_get_code(e);
  lv_indev_t *in = lv_indev_get_act(); if (!in) return;
  if (c == LV_EVENT_PRESSED) lv_indev_get_point(in, &g2048_p0);
  else if (c == LV_EVENT_RELEASED) {
    lv_point_t p; lv_indev_get_point(in, &p);
    int dx = p.x - g2048_p0.x, dy = p.y - g2048_p0.y;
    if (abs(dx) < 22 && abs(dy) < 22) return;
    if (abs(dx) > abs(dy)) g2048_do_move(dx < 0 ? 0 : 1); else g2048_do_move(dy < 0 ? 2 : 3);
  }
}
static void g2048_exit_do() {
  void (*cb)(void) = g2048_exit_cb;
  g2048_close();
  if (cb) cb();
}
static void g2048_start_cb(lv_event_t *e) { g2048_new(); }
static void g2048_back_cb(lv_event_t *e) { g2048_exit_do(); }
static void g2048_again_cb(lv_event_t *e) { if (g2048_won && !g2048_cont && g2048_can_move(&g2048_g)) { g2048_cont = true; g2048_playing = true; g2048_show(g2048_play); } else g2048_new(); }

void g2048_close(void) {
  g2048_playing = false;
  if (!g2048_root) return;
  lv_obj_t *r = g2048_root; g2048_root = NULL;
  lv_anim_del(NULL, g2048_pop_cb);
  lv_obj_del_async(r);
}
bool g2048_running(void) { return g2048_root && g2048_playing; }

static lv_obj_t *g2048_panel(lv_obj_t *p) {
  lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o); lv_obj_set_size(o, 360, 360);
  lv_obj_set_style_bg_color(o, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}
void g2048_open(lv_obj_t *parent, void (*on_exit)(void)) {
  if (g2048_root) g2048_close();
  g2048_exit_cb = on_exit; g2048_load(); g2048_playing = false;
  g2048_root = lv_obj_create(parent); lv_obj_remove_style_all(g2048_root);
  lv_obj_set_size(g2048_root, 360, 360); lv_obj_center(g2048_root);
  lv_obj_clear_flag(g2048_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g2048_root, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(g2048_root, LV_OPA_COVER, 0);

  // play panel (swipes land here)
  g2048_play = g2048_panel(g2048_root); lv_obj_clear_flag(g2048_play, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g2048_play, g2048_touch_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(g2048_play, g2048_touch_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_t *l;
  l = ui_label(g2048_play, &lv_font_montserrat_12, C_GREY, 0, "SCORE"); lv_obj_align(l, LV_ALIGN_TOP_MID, -50, 28);
  g2048_score_l = ui_label(g2048_play, &lv_font_montserrat_20, C_TEXT, 90, "0"); lv_obj_align(g2048_score_l, LV_ALIGN_TOP_MID, -50, 44);
  l = ui_label(g2048_play, &lv_font_montserrat_12, C_GREY, 0, "BEST"); lv_obj_align(l, LV_ALIGN_TOP_MID, 50, 28);
  g2048_best_l = ui_label(g2048_play, &lv_font_montserrat_20, C_TEXT, 90, "0"); lv_obj_align(g2048_best_l, LV_ALIGN_TOP_MID, 50, 44);
  g2048_grid = lv_obj_create(g2048_play); lv_obj_remove_style_all(g2048_grid);
  lv_obj_set_size(g2048_grid, 230, 230); lv_obj_set_pos(g2048_grid, 65, 78);
  lv_obj_set_style_bg_color(g2048_grid, lv_color_hex(0xBBADA0), 0); lv_obj_set_style_bg_opa(g2048_grid, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g2048_grid, 8, 0);
  for (int i = 0; i < 16; i++) {
    lv_obj_t *t = lv_obj_create(g2048_grid); lv_obj_remove_style_all(t);
    lv_obj_set_size(t, 50, 50); lv_obj_set_pos(t, 6 + (i % 4) * 56, 6 + (i / 4) * 56);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0); lv_obj_set_style_radius(t, 5, 0);
    lv_obj_set_style_transform_pivot_x(t, 25, 0); lv_obj_set_style_transform_pivot_y(t, 25, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g2048_tile[i] = t; g2048_tl[i] = ui_label(t, &lv_font_montserrat_28, 0x776E65, 0, ""); lv_obj_center(g2048_tl[i]);
  }
  lv_obj_t *bb = ui_btn(g2048_play, 70, 26, "Back", NULL); lv_obj_align(bb, LV_ALIGN_TOP_MID, 0, 316);
  lv_obj_add_event_cb(bb, g2048_back_cb, LV_EVENT_CLICKED, NULL);

  // start screen
  g2048_idle = g2048_panel(g2048_root);
  ui_title(g2048_idle, "2048");
  l = ui_label(g2048_idle, &lv_font_montserrat_48, 0xF2B179, 0, "2048"); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 80);
  g2048_idle_best = ui_label(g2048_idle, &lv_font_montserrat_16, C_TEXT, 0, ""); lv_obj_align(g2048_idle_best, LV_ALIGN_TOP_MID, 0, 150);
  { char b[24]; snprintf(b, sizeof b, "Best  %u", (unsigned)g2048_best); lv_label_set_text(g2048_idle_best, b); }
  lv_obj_t *sb = ui_btn(g2048_idle, 150, 50, LV_SYMBOL_PLAY "  Tap to start", NULL); lv_obj_align(sb, LV_ALIGN_TOP_MID, 0, 190);
  lv_obj_add_event_cb(sb, g2048_start_cb, LV_EVENT_CLICKED, NULL);
  l = ui_label(g2048_idle, &lv_font_montserrat_12, C_GREY, 200, "Swipe to slide and merge tiles"); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 252);
  lv_obj_t *b2 = ui_btn(g2048_idle, 70, 26, "Back", NULL); lv_obj_align(b2, LV_ALIGN_TOP_MID, 0, 290);
  lv_obj_add_event_cb(b2, g2048_back_cb, LV_EVENT_CLICKED, NULL);

  // game-over / win card
  g2048_over = g2048_panel(g2048_root);
  g2048_o_title = ui_label(g2048_over, &lv_font_montserrat_16, C_GREY, 0, "Game over"); lv_obj_align(g2048_o_title, LV_ALIGN_TOP_MID, 0, 36);
  g2048_o_score = ui_label(g2048_over, &lv_font_montserrat_48, C_TEXT, 0, "0"); lv_obj_align(g2048_o_score, LV_ALIGN_TOP_MID, 0, 66);
  g2048_o_new = ui_label(g2048_over, &lv_font_montserrat_14, C_RED, 0, "New best!"); lv_obj_align(g2048_o_new, LV_ALIGN_TOP_MID, 0, 128);
  g2048_o_best = ui_label(g2048_over, &lv_font_montserrat_14, C_TEXT, 0, "Best  0"); lv_obj_align(g2048_o_best, LV_ALIGN_TOP_MID, 0, 152);
  lv_obj_t *pa = ui_btn(g2048_over, 140, 44, "Play again", &g2048_o_btn1l); lv_obj_align(pa, LV_ALIGN_TOP_MID, 0, 188);
  lv_obj_add_event_cb(pa, g2048_again_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *b3 = ui_btn(g2048_over, 140, 40, "Back", NULL); lv_obj_align(b3, LV_ALIGN_TOP_MID, 0, 240);
  lv_obj_add_event_cb(b3, g2048_back_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_add_flag(g2048_play, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g2048_over, LV_OBJ_FLAG_HIDDEN);
}
#endif
