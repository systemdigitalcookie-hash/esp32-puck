// v14 Games: simple Notion-minimal list (same look as Settings). Included from puck_ota.ino after nav_apply()/lock_game/ui_style.h.
// Each game module: <x>_open(lv_obj_t*, void(*on_exit)(void)), <x>_close() (idempotent), <x>_running().
// Crash-safety rules: a game's UI is never deleted from inside its own event handler. A module's Back button only calls on_exit;
// on_exit just queues an async job; that job calls <x>_close() (deletes the module UI) and THEN queues the container delete (FIFO).
#pragma once
#include <Preferences.h>
#include "game_flappy.h"
#include "game_snake.h"
#include "game_sudoku.h"

struct GhGame { const char *name, *glyph; void (*open)(lv_obj_t *, void (*)(void)); void (*close)(); };
static const GhGame gh_games[3] = {
  { "Flappy Bird", LV_SYMBOL_UP, flappy_open, flappy_close },
  { "Snake", LV_SYMBOL_SHUFFLE, snake_open, snake_close },
  { "Sudoku", LV_SYMBOL_LIST, sudoku_open, sudoku_close },
};
#define GH_N 3
static lv_obj_t *gh_sub[GH_N], *gh_cont = NULL;
static int gh_cur = -1, gh_pending = -1;

static void gh_mem(const char *tag) {
  Serial.printf("[games] %s heap=%u (min %u) psram=%u\n", tag, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
static int gh_get_int(const char *key) {
  Preferences p; int v = 0;
  if (p.begin("games", true)) {
    if (p.isKey(key)) switch (p.getType(key)) {
      case PT_I32: v = p.getInt(key, 0); break;
      case PT_U32: v = (int)p.getUInt(key, 0); break;
      default: break;
    }
    p.end();
  }
  return v;
}
static void gh_refresh() {
  char t[48];
  for (int i = 0; i < GH_N; i++) {
    if (i == 2) {   // Sudoku: best times per difficulty
      const char *k[3] = {"sdk_e", "sdk_m", "sdk_h"}; char a[3][8];
      for (int d = 0; d < 3; d++) { int s = gh_get_int(k[d]); if (s) snprintf(a[d], 8, "%d:%02d", s / 60, s % 60); else strcpy(a[d], "--"); }
      snprintf(t, sizeof t, "Best  E %s  M %s  H %s", a[0], a[1], a[2]);
    } else { int b = gh_get_int(i == 0 ? "flappy" : "snake"); if (b > 0) snprintf(t, sizeof t, "Best  %d", b); else snprintf(t, sizeof t, "No score yet"); }
    lv_label_set_text(gh_sub[i], t);
  }
}
static void gh_del_cont_async(void *o) { lv_obj_del((lv_obj_t *)o); }
static void gh_teardown() {
  if (gh_cur < 0) return;
  Serial.printf("[games] close %s\n", gh_games[gh_cur].name);
  gh_games[gh_cur].close();                                              // deletes the module's own UI (idempotent)
  if (gh_cont) { lv_async_call(gh_del_cont_async, gh_cont); gh_cont = NULL; }   // container goes last, outside any event
  gh_cur = -1; lock_game = false; nav_apply(); gh_refresh();
  gh_mem("after close");
}
static void gh_exit_async(void *) { gh_teardown(); }
static void gh_on_exit() { lv_async_call(gh_exit_async, NULL); }   // called from the game's button event: only queue work
static void gh_launch_async(void *) {
  int i = gh_pending; gh_pending = -1;
  if (i < 0 || gh_cur >= 0) return;
  gh_cont = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(gh_cont);
  lv_obj_set_size(gh_cont, 360, 360); lv_obj_set_pos(gh_cont, 0, 0);
  lv_obj_set_style_bg_color(gh_cont, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(gh_cont, LV_OPA_COVER, 0);
  lv_obj_clear_flag(gh_cont, LV_OBJ_FLAG_SCROLLABLE);
  gh_cur = i; lock_game = true; nav_apply();
  gh_mem("before open"); Serial.printf("[games] open %s\n", gh_games[i].name);
  gh_games[i].open(gh_cont, gh_on_exit);
  gh_mem("after open");
}
static void gh_row_cb(lv_event_t *e) {
  if (gh_cur >= 0 || gh_pending >= 0) return;
  gh_pending = (int)(intptr_t)lv_event_get_user_data(e);
  lv_async_call(gh_launch_async, NULL);
}
// Self-test: long-press the "Games" title -> opens/closes Flappy, Snake and Sudoku 10x each, logging heap (serial 115200)
static int gh_stress = 0; static uint32_t gh_stress_t = 0;
static void gh_stress_cb(lv_event_t *e) { if (!gh_stress && gh_cur < 0) { gh_stress = 1; gh_stress_t = millis(); Serial.println("[games] stress test start"); gh_mem("stress start"); } }
static void gh_stress_step() {
  if (!gh_stress || millis() - gh_stress_t < 500) return;
  gh_stress_t = millis();
  int idx = (gh_stress - 1) / 2;
  if (idx >= GH_N * 10) { gh_stress = 0; gh_mem("stress done"); Serial.println("[games] stress test done"); return; }
  if (gh_stress & 1) { gh_pending = idx % GH_N; gh_launch_async(NULL); } else gh_teardown();
  gh_stress++;
}
static void gh_build(lv_obj_t *t) {
  { Preferences p; if (p.begin("puck", false)) { p.remove("g_best"); p.remove("g_axis"); p.end(); } }                 // Drive game keys (v12)
  { Preferences p; if (p.begin("games", false)) { p.remove("2048"); p.end(); } }                                   // 2048 best (v14)
  lv_obj_t *h = ui_title(t, "Games");
  lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(h, 20); lv_obj_add_event_cb(h, gh_stress_cb, LV_EVENT_LONG_PRESSED, NULL);
  ui_divider(t, 220, 62);
  lv_obj_t *list = lv_obj_create(t); lv_obj_remove_style_all(list);
  lv_obj_set_size(list, 250, 252); lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < GH_N; i++) {
    lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 250, 56);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_width(r, i < GH_N - 1 ? 1 : 0, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(C_DIV), 0);
    lv_obj_add_event_cb(r, gh_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *ic = ui_label(r, &lv_font_montserrat_16, C_GREY, 0, gh_games[i].glyph); lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *ti = ui_label(r, &lv_font_montserrat_16, C_TEXT, 0, gh_games[i].name);
    lv_obj_set_style_text_align(ti, LV_TEXT_ALIGN_LEFT, 0); lv_obj_align(ti, LV_ALIGN_TOP_LEFT, 48, 8);
    gh_sub[i] = ui_label(r, &lv_font_montserrat_12, C_GREY, 190, "");
    lv_label_set_long_mode(gh_sub[i], LV_LABEL_LONG_DOT); lv_obj_set_style_text_align(gh_sub[i], LV_TEXT_ALIGN_LEFT, 0); lv_obj_align(gh_sub[i], LV_ALIGN_TOP_LEFT, 48, 32);
    lv_obj_t *ch = ui_label(r, &lv_font_montserrat_12, C_LINE, 0, LV_SYMBOL_RIGHT); lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -10, 0);
  }
  gh_refresh();
}
// every loop; closes a running game when the page is left
static void gh_tick(bool visible) {
  gh_stress_step();
  if (gh_cur >= 0 && !visible) gh_teardown();
  // v16: best scores are cached in the row labels (read at boot + after every game closes); no NVS reads mid-swipe
}
