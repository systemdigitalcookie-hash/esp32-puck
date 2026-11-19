// v12 Games hub: PS Vita LiveArea-style round bubbles. Included from puck_ota.ino after nav_apply()/lock_game/ui_style.h.
// Each game module: <x>_open(lv_obj_t*, void(*on_exit)(void)), <x>_close(), <x>_running(), icon lv_img_dsc_t game_icon_<x> (96x96).
// Best scores: Preferences namespace "games", keys flappy / 2048 / snake.
#pragma once
#include <Preferences.h>
#if __has_include("game_flappy.h")
#include "game_flappy_icon.h"
#include "game_flappy.h"
#define HAVE_G_FLAPPY 1
#endif
#if __has_include("game_2048.h")
#include "game_2048_icon.h"
#include "game_2048.h"
#define HAVE_G_2048 1
#endif
#if __has_include("game_snake.h")
#include "game_snake.h"
#define HAVE_G_SNAKE 1
#endif

struct GhGame { const char *name, *key; const lv_img_dsc_t *icon; void (*open)(lv_obj_t *, void (*)(void)); void (*close)(); bool (*running)(); uint32_t col; };
static GhGame gh_games[3]; static int gh_n = 0;
static lv_obj_t *gh_bub[3], *gh_score[3], *gh_cont = NULL, *gh_page = NULL;
static int gh_cur = -1;   // game currently open, -1 = none
static int gh_pending = -1;
static void gh_mem(const char *tag) {
  Serial.printf("[games] %s heap=%u (min %u) psram=%u lvfl=%u\n", tag, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)lv_flush_cnt);
}
static void gh_del_cont_async(void *o) { lv_obj_del((lv_obj_t *)o); }   // runs after the module's own del_async(s) (FIFO), so no dangling child delete

static void gh_register() {
#ifdef HAVE_G_FLAPPY
  gh_games[gh_n++] = { "Flappy Bird", "flappy", &game_icon_flappy, flappy_open, flappy_close, flappy_running, 0xF5A623 };
#endif
#ifdef HAVE_G_2048
  gh_games[gh_n++] = { "2048", "2048", &game_icon_2048, g2048_open, g2048_close, g2048_running, 0xEDC22E };
#endif
#ifdef HAVE_G_SNAKE
  gh_games[gh_n++] = { "Snake", "snake", &game_icon_snake, snake_open, snake_close, snake_running, 0x4CAF50 };
#endif
}
static int gh_best(const char *key) {
  Preferences p; int v = 0;
  if (p.begin("games", true)) {
    if (p.isKey(key)) {
      switch (p.getType(key)) {
        case PT_I32: v = p.getInt(key, 0); break;
        case PT_U32: v = (int)p.getUInt(key, 0); break;
        case PT_I16: v = p.getShort(key, 0); break;
        case PT_U16: v = p.getUShort(key, 0); break;
        case PT_I64: v = (int)p.getLong64(key, 0); break;
        case PT_U64: v = (int)p.getULong64(key, 0); break;
        case PT_U8: v = p.getUChar(key, 0); break;
        case PT_I8: v = p.getChar(key, 0); break;
        default: break;
      }
    }
    p.end();
  }
  return v;
}
static void gh_refresh() {
  for (int i = 0; i < gh_n; i++) {
    int b = gh_best(gh_games[i].key); char t[24];
    if (b > 0) snprintf(t, sizeof t, "Best %d", b); else snprintf(t, sizeof t, "No score yet");
    lv_label_set_text(gh_score[i], t);
  }
}
static void gh_zoom_cb(void *o, int32_t v) { lv_obj_set_style_transform_zoom((lv_obj_t *)o, v, 0); }
static void gh_teardown(bool force_close) {
  if (gh_cur < 0) return;
  GhGame &g = gh_games[gh_cur];
  Serial.printf("[games] close %s (force=%d)\n", g.name, (int)force_close);
  if (force_close) g.close();
  if (gh_cont) { lv_async_call(gh_del_cont_async, gh_cont); gh_cont = NULL; }
  int was = gh_cur; (void)was;
  gh_cur = -1; lock_game = false; nav_apply(); gh_refresh();
  for (int i = 0; i < gh_n; i++) { lv_obj_set_style_transform_zoom(gh_bub[i], 256, 0); lv_obj_set_style_shadow_width(gh_bub[i], 14, 0); }
  gh_mem("after close");
}
static void gh_exit_async(void *) { gh_teardown(false); }   // game exited itself via Back (modules close themselves)
static void gh_on_exit() { lv_async_call(gh_exit_async, NULL); }   // never delete the game's UI from inside its own callback
static void gh_launch_async(void *) {
  int i = gh_pending; gh_pending = -1;
  if (i < 0 || gh_cur >= 0) return;
  gh_cont = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(gh_cont);
  lv_obj_set_size(gh_cont, 360, 360); lv_obj_set_pos(gh_cont, 0, 0);
  lv_obj_set_style_bg_color(gh_cont, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(gh_cont, LV_OPA_COVER, 0);
  lv_obj_clear_flag(gh_cont, LV_OBJ_FLAG_SCROLLABLE);
  gh_cur = i; lock_game = true; nav_apply();
  gh_mem("before open");
  Serial.printf("[games] open %s\n", gh_games[i].name);
  gh_games[i].open(gh_cont, gh_on_exit);
  gh_mem("after open");
}
static void gh_zoom_done(lv_anim_t *a) { lv_async_call(gh_launch_async, NULL); }
static void gh_tap_cb(lv_event_t *e) {
  if (gh_cur >= 0 || gh_pending >= 0) return;
  int i = (int)(intptr_t)lv_event_get_user_data(e); gh_pending = i;
  lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, gh_bub[i]); lv_anim_set_exec_cb(&a, gh_zoom_cb);
  lv_obj_set_style_shadow_width(gh_bub[i], 0, 0); lv_anim_set_values(&a, 256, 320); lv_anim_set_time(&a, 120); lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_ready_cb(&a, gh_zoom_done); lv_anim_start(&a);
}
// Self-test: long-press the "Games" title -> opens/closes every game 10x, logging heap each time (serial 115200)
static int gh_stress = 0; static uint32_t gh_stress_t = 0;
static void gh_stress_cb(lv_event_t *e) { if (!gh_stress && gh_cur < 0 && gh_n) { gh_stress = 1; gh_stress_t = millis(); Serial.println("[games] stress test start"); gh_mem("stress start"); } }
static void gh_stress_step() {
  if (!gh_stress || millis() - gh_stress_t < 500) return;
  gh_stress_t = millis();
  int idx = (gh_stress - 1) / 2;
  if (idx >= gh_n * 10) { gh_stress = 0; gh_mem("stress done"); Serial.println("[games] stress test done"); return; }
  if (gh_stress & 1) { gh_pending = idx % gh_n; gh_launch_async(NULL); }
  else gh_teardown(true);
  gh_stress++;
}
static void gh_build(lv_obj_t *t) {
  gh_register(); gh_page = t;
  { Preferences p; if (p.begin("puck", false)) { p.remove("g_best"); p.remove("g_axis"); p.end(); } }   // old Drive game keys
  lv_obj_t *h = ui_label(t, &lv_font_montserrat_16, C_TEXT, 0, "Games"); lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 22);
  lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(h, 20); lv_obj_add_event_cb(h, gh_stress_cb, LV_EVENT_LONG_PRESSED, NULL);
  const int D = 92;
  // centres: triangle for 3, side by side for 2, middle for 1
  int cx[3], cy[3];
  if (gh_n >= 3) { cx[0] = 180; cy[0] = 100; cx[1] = 112; cy[1] = 236; cx[2] = 248; cy[2] = 236; }
  else if (gh_n == 2) { cx[0] = 112; cy[0] = 170; cx[1] = 248; cy[1] = 170; }
  else { cx[0] = 180; cy[0] = 170; }
  for (int i = 0; i < gh_n; i++) {
    GhGame &g = gh_games[i];
    lv_obj_t *b = lv_obj_create(t); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, D, D); lv_obj_set_pos(b, cx[i] - D / 2, cy[i] - D / 2);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_clip_corner(b, true, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(g.col), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0); lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_shadow_width(b, 14, 0); lv_obj_set_style_shadow_color(b, lv_color_hex(g.col), 0); lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0); lv_obj_set_style_shadow_ofs_y(b, 4, 0);
    lv_obj_set_style_transform_pivot_x(b, D / 2, 0); lv_obj_set_style_transform_pivot_y(b, D / 2, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *im = lv_img_create(b); lv_img_set_src(im, g.icon);
    lv_img_set_zoom(im, 256 * (D - 6) / 96); lv_obj_center(im); lv_obj_clear_flag(im, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *gl = lv_obj_create(b); lv_obj_remove_style_all(gl);   // gloss highlight on the upper half
    lv_obj_set_size(gl, 62, 26); lv_obj_align(gl, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_radius(gl, 13, 0); lv_obj_set_style_bg_color(gl, lv_color_hex(0xFFFFFF), 0); lv_obj_set_style_bg_opa(gl, LV_OPA_30, 0);
    lv_obj_clear_flag(gl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, gh_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    gh_bub[i] = b;
    lv_obj_t *n = ui_label(t, &lv_font_montserrat_14, C_TEXT, 100, g.name); lv_obj_set_pos(n, cx[i] - 50, cy[i] + D / 2 + 5);
    gh_score[i] = ui_label(t, &lv_font_montserrat_12, C_GREY, 100, ""); lv_obj_set_pos(gh_score[i], cx[i] - 50, cy[i] + D / 2 + 23);
  }
  gh_refresh();
}
// call every loop with whether the Games page is on screen; closes a running game if the page is left
static void gh_tick(bool visible) {
  static bool was = false;
  gh_stress_step();
  if (gh_cur >= 0 && !visible) gh_teardown(true);   // page left: close the game
  if (visible && !was) gh_refresh();
  was = visible;
}
