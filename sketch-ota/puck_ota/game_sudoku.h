// Sudoku for the puck Games list. Header-only, prefix sudoku_ (v14).
// API: sudoku_open(parent,on_exit), sudoku_close(), sudoku_running(). NVS "games": sdk_e/sdk_m/sdk_h (best secs), sdk_s (saved game blob).
// Lightweight: all widgets created once; only text/colour updated when changed; one 1 s lv_timer (paused outside play).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef SUDOKU_LOGIC_ONLY
#include <time.h>
static uint32_t sudoku_rnd() { return (uint32_t)rand(); }
static uint32_t sudoku_ms() { return (uint32_t)(clock() * 1000.0 / CLOCKS_PER_SEC); }
#else
#include "lvgl.h"
#include <Preferences.h>
static uint32_t sudoku_rnd() { return esp_random(); }
static uint32_t sudoku_ms() { return millis(); }
#endif

// ---------------- puzzle logic (unique-solution check) ----------------
typedef struct { uint16_t r[9], c[9], b[9]; } SdkMask;
static inline int sdk_box(int i) { return (i / 27) * 3 + (i % 9) / 3; }
static void sdk_mask_set(SdkMask *m, int i, int d, bool on) {
  uint16_t bit = 1u << d; int r = i / 9, c = i % 9, b = sdk_box(i);
  if (on) { m->r[r] |= bit; m->c[c] |= bit; m->b[b] |= bit; } else { m->r[r] &= ~bit; m->c[c] &= ~bit; m->b[b] &= ~bit; }
}
static void sdk_mask_build(const uint8_t *g, SdkMask *m) {
  memset(m, 0, sizeof *m);
  for (int i = 0; i < 81; i++) if (g[i]) sdk_mask_set(m, i, g[i] - 1, true);
}
// count solutions (up to limit); rnd!=0 randomises value order and stops at the first solution leaving it in g
// v15: ITERATIVE (explicit static stack). v14's recursive version used ~112 B of stack per level x up to 81 levels
// (~9 KB) inside an LVGL click handler on the 8 KB loopTask stack -> stack overflow crash when picking a difficulty.
static int sdk_solve(uint8_t *g, SdkMask *m, int limit, long *nodes, bool rnd) {
  static uint8_t f_cell[81], f_ds[81][9], f_nd[81], f_k[81];
  int sp = 0, cnt = 0; bool descend = true;
  for (;;) {
    if (descend) {
      int best = -1, bc = 10; uint16_t bcand = 0;
      for (int i = 0; i < 81; i++) if (!g[i]) {
        uint16_t cand = (~(m->r[i / 9] | m->c[i % 9] | m->b[sdk_box(i)])) & 0x1FF;
        int n = __builtin_popcount(cand);
        if (n < bc) { bc = n; best = i; bcand = cand; if (n <= 1) break; }
      }
      descend = false;
      if (best < 0) { cnt++; if (rnd) return cnt; }          // solved: rnd keeps the filled grid
      else if (bc == 0) { }                                   // dead end
      else if (++(*nodes) > 60000) cnt = limit;               // too hard to judge: treat as "not unique"
      else {
        int nd = 0; for (int d = 0; d < 9; d++) if (bcand & (1u << d)) f_ds[sp][nd++] = (uint8_t)d;
        if (rnd) for (int i = nd - 1; i > 0; i--) { int j = sudoku_rnd() % (i + 1); uint8_t t = f_ds[sp][i]; f_ds[sp][i] = f_ds[sp][j]; f_ds[sp][j] = t; }
        f_cell[sp] = (uint8_t)best; f_nd[sp] = (uint8_t)nd; f_k[sp] = 0; sp++;
      }
    }
    if (cnt >= limit) {   // unwind: restore grid + masks to how the caller passed them
      while (sp > 0) { sp--; if (f_k[sp]) { sdk_mask_set(m, f_cell[sp], f_ds[sp][f_k[sp] - 1], false); g[f_cell[sp]] = 0; } }
      return cnt;
    }
    if (sp == 0) return cnt;
    int f = sp - 1;
    if (f_k[f]) { sdk_mask_set(m, f_cell[f], f_ds[f][f_k[f] - 1], false); g[f_cell[f]] = 0; }   // undo previous try
    if (f_k[f] < f_nd[f]) { uint8_t d = f_ds[f][f_k[f]++]; g[f_cell[f]] = d + 1; sdk_mask_set(m, f_cell[f], d, true); descend = true; }
    else sp--;
  }
}
// difficulty 0 easy / 1 medium / 2 hard -> sol (full grid) and giv (puzzle, 0 = blank)
static void sudoku_generate(int diff, uint8_t *sol, uint8_t *giv) {
  SdkMask m; long nodes = 0;
  memset(sol, 0, 81); memset(&m, 0, sizeof m);
  sdk_solve(sol, &m, 1, &nodes, true);
  memcpy(giv, sol, 81);
  int clues = diff == 0 ? 40 : (diff == 1 ? 33 : 28), have = 81;
  static int ord[81]; for (int i = 0; i < 81; i++) ord[i] = i;
  for (int i = 80; i > 0; i--) { int j = sudoku_rnd() % (i + 1); int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
  uint32_t t0 = sudoku_ms();
  for (int k = 0; k < 81 && have > clues; k++) {
    if (sudoku_ms() - t0 > 1800) break;   // time cap: ship what we have (still unique)
    int i = ord[k]; uint8_t keep = giv[i]; giv[i] = 0;
    static uint8_t tmp[81]; memcpy(tmp, giv, 81); sdk_mask_build(tmp, &m); nodes = 0;
    if (sdk_solve(tmp, &m, 2, &nodes, false) == 1) have--; else giv[i] = keep;
  }
}

#ifndef SUDOKU_LOGIC_ONLY
#define SDK_BX 63
#define SDK_BY 44
#define SDK_CS 26
#define SDK_SEL  0xBBD7F0
#define SDK_SAME 0xDCEBF7
#define SDK_PEER 0xF1F1EF
#define SDK_BLUE 0x2F6FB3
struct SdkSave { uint32_t magic; uint32_t secs; uint8_t diff; uint8_t giv[81], sol[81], val[81]; };
static lv_obj_t *sudoku_root = NULL, *sdk_p_start, *sdk_p_play, *sdk_p_win, *sdk_cell[81], *sdk_cl[81], *sdk_hdr, *sdk_row_cont, *sdk_row_l[4], *sdk_w_time, *sdk_w_best, *sdk_w_new;
static lv_timer_t *sdk_tm = NULL;
static void (*sdk_exit_cb)(void) = NULL;
static uint8_t sdk_sol[81], sdk_giv[81], sdk_val[81];
static uint32_t sdk_c_bg[81], sdk_c_fg[81]; static uint8_t sdk_c_v[81];
static int sdk_diff = 0, sdk_sel = -1; static uint32_t sdk_secs = 0; static bool sdk_playing = false;
static const char *SDK_NAME[3] = {"Easy", "Medium", "Hard"};
static const char *SDK_KEY[3] = {"sdk_e", "sdk_m", "sdk_h"};

static void sdk_fmt(char *o, size_t n, uint32_t s) {
  if (s >= 3600) snprintf(o, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else snprintf(o, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}
static uint32_t sdk_best(int d) { Preferences p; uint32_t v = 0; if (p.begin("games", true)) { v = p.getUInt(SDK_KEY[d], 0); p.end(); } return v; }
static bool sdk_has_save() {
  Preferences p; bool ok = false;
  if (p.begin("games", true)) { ok = p.getBytesLength("sdk_s") == sizeof(SdkSave); p.end(); }
  return ok;
}
static bool sdk_dirty = false; static uint32_t sdk_dirty_t = 0;   // v16: debounced save after a move
static void sdk_save() {
  sdk_dirty = false;
  if (!sdk_playing) return;
  SdkSave s; s.magic = 0x5344; s.secs = sdk_secs; s.diff = sdk_diff;
  memcpy(s.giv, sdk_giv, 81); memcpy(s.sol, sdk_sol, 81); memcpy(s.val, sdk_val, 81);
  Preferences p; if (p.begin("games", false)) { p.putBytes("sdk_s", &s, sizeof s); p.end(); }
}
static void sdk_save_clear() { Preferences p; if (p.begin("games", false)) { p.remove("sdk_s"); p.end(); } }
static bool sdk_load() {
  SdkSave s; Preferences p; bool ok = false;
  if (p.begin("games", true)) { if (p.getBytesLength("sdk_s") == sizeof s && p.getBytes("sdk_s", &s, sizeof s) == sizeof s && s.magic == 0x5344 && s.diff < 3) ok = true; p.end(); }
  if (!ok) return false;
  sdk_diff = s.diff; sdk_secs = s.secs; memcpy(sdk_giv, s.giv, 81); memcpy(sdk_sol, s.sol, 81); memcpy(sdk_val, s.val, 81);
  return true;
}
static void sdk_hdr_update() {
  char t[16], b[32]; sdk_fmt(t, sizeof t, sdk_secs); snprintf(b, sizeof b, "%s  %s", SDK_NAME[sdk_diff], t);
  lv_label_set_text(sdk_hdr, b);
}
static void sdk_paint() {   // only touches cells whose look changed
  int sr = sdk_sel >= 0 ? sdk_sel / 9 : -1, sc = sdk_sel >= 0 ? sdk_sel % 9 : -1, sb = sdk_sel >= 0 ? sdk_box(sdk_sel) : -1;
  int sv = sdk_sel >= 0 ? sdk_val[sdk_sel] : 0;
  for (int i = 0; i < 81; i++) {
    uint32_t bg = 0xFFFFFF;
    if (sdk_sel >= 0) {
      if (i == sdk_sel) bg = SDK_SEL;
      else if (sv && sdk_val[i] == sv) bg = SDK_SAME;
      else if (i / 9 == sr || i % 9 == sc || sdk_box(i) == sb) bg = SDK_PEER;
    }
    uint32_t fg = sdk_giv[i] ? C_TEXT : (sdk_val[i] == sdk_sol[i] ? SDK_BLUE : C_RED);
    if (bg != sdk_c_bg[i]) { sdk_c_bg[i] = bg; lv_obj_set_style_bg_color(sdk_cell[i], lv_color_hex(bg), 0); }
    if (fg != sdk_c_fg[i]) { sdk_c_fg[i] = fg; lv_obj_set_style_text_color(sdk_cl[i], lv_color_hex(fg), 0); }
    if (sdk_val[i] != sdk_c_v[i]) {
      sdk_c_v[i] = sdk_val[i];
      if (sdk_val[i]) { char b[2] = { (char)('0' + sdk_val[i]), 0 }; lv_label_set_text(sdk_cl[i], b); } else lv_label_set_text(sdk_cl[i], "");
    }
  }
}
static void sdk_start_refresh() {
  char t[16], b[40];
  for (int d = 0; d < 3; d++) { uint32_t s = sdk_best(d); if (s) sdk_fmt(t, sizeof t, s); else strcpy(t, "--"); snprintf(b, sizeof b, "Best  %s", t); lv_label_set_text(sdk_row_l[d], b); }
  if (sdk_has_save()) lv_obj_clear_flag(sdk_row_cont, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(sdk_row_cont, LV_OBJ_FLAG_HIDDEN);
}
static void sdk_show(lv_obj_t *p) {
  lv_obj_add_flag(sdk_p_start, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(sdk_p_play, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(sdk_p_win, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);
}
static void sdk_enter_play() {
  sdk_playing = true; sdk_sel = -1;
  for (int i = 0; i < 81; i++) { sdk_c_bg[i] = 0xFFFFFFF; sdk_c_fg[i] = 0xFFFFFFF; sdk_c_v[i] = 255; }
  sdk_paint(); sdk_hdr_update(); sdk_show(sdk_p_play); lv_timer_resume(sdk_tm);
}
static void sdk_row_cb(lv_event_t *e) {
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  if (d == 3) { if (!sdk_load()) return; }
  else {
    Serial.printf("[sudoku] generating %s\n", SDK_NAME[d]); uint32_t t0 = millis();
    sdk_diff = d; sudoku_generate(d, sdk_sol, sdk_giv); memcpy(sdk_val, sdk_giv, 81); sdk_secs = 0;
    int cl = 0; for (int i = 0; i < 81; i++) if (sdk_giv[i]) cl++;
    Serial.printf("[sudoku] %d clues in %u ms\n", cl, (unsigned)(millis() - t0));
  }
  sdk_enter_play();
}
static void sdk_win() {
  sdk_playing = false; lv_timer_pause(sdk_tm); sdk_save_clear();
  uint32_t b = sdk_best(sdk_diff); bool nb = (b == 0 || sdk_secs < b);
  if (nb) { Preferences p; if (p.begin("games", false)) { p.putUInt(SDK_KEY[sdk_diff], sdk_secs); p.end(); } b = sdk_secs; }
  char t[16], s[32]; sdk_fmt(t, sizeof t, sdk_secs); lv_label_set_text(sdk_w_time, t);
  sdk_fmt(t, sizeof t, b); snprintf(s, sizeof s, "Best %s  %s", SDK_NAME[sdk_diff], t); lv_label_set_text(sdk_w_best, s);
  if (nb) lv_obj_clear_flag(sdk_w_new, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(sdk_w_new, LV_OBJ_FLAG_HIDDEN);
  sdk_show(sdk_p_win);
}
static void sdk_cell_cb(lv_event_t *e) { if (!sdk_playing) return; sdk_sel = (int)(intptr_t)lv_event_get_user_data(e); sdk_paint(); }
static void sdk_set(int v) {
  if (!sdk_playing || sdk_sel < 0 || sdk_giv[sdk_sel]) return;
  sdk_val[sdk_sel] = v; sdk_paint();
  if (v) { bool ok = true; for (int i = 0; i < 81; i++) if (sdk_val[i] != sdk_sol[i]) { ok = false; break; } if (ok) { sdk_win(); return; } }
  sdk_dirty = true; sdk_dirty_t = millis();   // v16: saved ~2 s later by the 1 s timer (and always on Back/close), not on every tap
}
static void sdk_pad_cb(lv_event_t *e) { sdk_set((int)(intptr_t)lv_event_get_user_data(e)); }
static void sdk_play_back_cb(lv_event_t *e) {
  sdk_save(); sdk_playing = false; lv_timer_pause(sdk_tm); sdk_start_refresh(); sdk_show(sdk_p_start);
}
static void sdk_menu_cb(lv_event_t *e) { sdk_start_refresh(); sdk_show(sdk_p_start); }
static void sdk_exit_btn_cb(lv_event_t *e) { void (*cb)(void) = sdk_exit_cb; if (cb) cb(); }   // hub closes us from an async call
static void sdk_timer_cb(lv_timer_t *) {
  if (!sdk_playing) return;
  sdk_secs++; sdk_hdr_update();
  if (sdk_secs % 20 == 0 || (sdk_dirty && millis() - sdk_dirty_t >= 2000)) sdk_save();
}
bool sudoku_running(void) { return sudoku_root && sdk_playing; }
void sudoku_close(void) {
  if (sdk_playing) sdk_save();
  sdk_playing = false;
  if (sdk_tm) { lv_timer_del(sdk_tm); sdk_tm = NULL; }
  if (sudoku_root) { lv_obj_t *r = sudoku_root; sudoku_root = NULL; lv_obj_del(r); }   // never called from inside one of our own event handlers
  sdk_exit_cb = NULL;
}
static lv_obj_t *sdk_panel(lv_obj_t *p) {
  lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o); lv_obj_set_size(o, 360, 360);
  lv_obj_set_style_bg_color(o, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
static lv_obj_t *sdk_rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t col) {
  lv_obj_t *o = lv_obj_create(p); lv_obj_remove_style_all(o); lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
static lv_obj_t *sdk_row(lv_obj_t *list, const char *name, int id, lv_obj_t **sub) {
  lv_obj_t *r = lv_obj_create(list); lv_obj_remove_style_all(r); lv_obj_set_size(r, 240, 46);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(r, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(C_PRESS), LV_STATE_PRESSED);
  lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0); lv_obj_set_style_border_width(r, 1, 0); lv_obj_set_style_border_color(r, lv_color_hex(C_DIV), 0);
  lv_obj_add_event_cb(r, sdk_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  lv_obj_t *n = ui_label(r, &lv_font_montserrat_16, C_TEXT, 0, name); lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_LEFT, 0); lv_obj_align(n, LV_ALIGN_LEFT_MID, 10, 0);
  if (sub) { *sub = ui_label(r, &lv_font_montserrat_12, C_GREY, 0, ""); lv_obj_align(*sub, LV_ALIGN_RIGHT_MID, -10, 0); }
  return r;
}
void sudoku_open(lv_obj_t *parent, void (*on_exit)(void)) {
  sudoku_close();
  sdk_exit_cb = on_exit; sdk_playing = false;
  sudoku_root = lv_obj_create(parent); lv_obj_remove_style_all(sudoku_root);
  lv_obj_set_size(sudoku_root, 360, 360); lv_obj_set_pos(sudoku_root, 0, 0); lv_obj_clear_flag(sudoku_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(sudoku_root, lv_color_hex(C_BG), 0); lv_obj_set_style_bg_opa(sudoku_root, LV_OPA_COVER, 0);

  // ---- start: difficulty list ----
  sdk_p_start = sdk_panel(sudoku_root);
  ui_title(sdk_p_start, "Sudoku"); ui_divider(sdk_p_start, 220, 62);
  lv_obj_t *list = lv_obj_create(sdk_p_start); lv_obj_remove_style_all(list); lv_obj_set_size(list, 240, 196); lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  sdk_row_cont = sdk_row(list, "Continue", 3, NULL);
  static const char *nm[3] = {"Easy", "Medium", "Hard"};
  for (int d = 0; d < 3; d++) sdk_row(list, nm[d], d, &sdk_row_l[d]);
  lv_obj_t *bk = ui_btn(sdk_p_start, 80, 28, "Back", NULL); lv_obj_align(bk, LV_ALIGN_TOP_MID, 0, 292);
  lv_obj_add_event_cb(bk, sdk_exit_btn_cb, LV_EVENT_CLICKED, NULL);

  // ---- play ----
  sdk_p_play = sdk_panel(sudoku_root);
  sdk_hdr = ui_label(sdk_p_play, &lv_font_montserrat_14, C_TEXT, 0, ""); lv_obj_set_style_text_align(sdk_hdr, LV_TEXT_ALIGN_LEFT, 0); lv_obj_set_pos(sdk_hdr, 104, 24);
  lv_obj_t *er = ui_btn(sdk_p_play, 56, 22, "Erase", NULL); lv_obj_set_pos(er, 204, 20); lv_obj_set_style_text_font(er, &lv_font_montserrat_12, 0);
  lv_obj_add_event_cb(er, sdk_pad_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
  for (int i = 0; i < 81; i++) {
    lv_obj_t *c = lv_obj_create(sdk_p_play); lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, SDK_BX + (i % 9) * SDK_CS, SDK_BY + (i / 9) * SDK_CS); lv_obj_set_size(c, SDK_CS, SDK_CS);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(c, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_side(c, (lv_border_side_t)(LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), 0);
    lv_obj_set_style_border_width(c, 1, 0); lv_obj_set_style_border_color(c, lv_color_hex(C_LINE), 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(c, sdk_cell_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    sdk_cell[i] = c; sdk_cl[i] = ui_label(c, &lv_font_montserrat_16, C_TEXT, 0, ""); lv_obj_center(sdk_cl[i]);
    lv_obj_clear_flag(sdk_cl[i], LV_OBJ_FLAG_CLICKABLE);
  }
  for (int k = 1; k <= 2; k++) {   // thick 3x3 lines + outer frame
    sdk_rect(sdk_p_play, SDK_BX + k * 78 - 1, SDK_BY, 2, 234, C_TEXT);
    sdk_rect(sdk_p_play, SDK_BX, SDK_BY + k * 78 - 1, 234, 2, C_TEXT);
  }
  sdk_rect(sdk_p_play, SDK_BX - 1, SDK_BY - 1, 236, 2, C_TEXT); sdk_rect(sdk_p_play, SDK_BX - 1, SDK_BY + 233, 236, 2, C_TEXT);
  sdk_rect(sdk_p_play, SDK_BX - 1, SDK_BY - 1, 2, 236, C_TEXT); sdk_rect(sdk_p_play, SDK_BX + 233, SDK_BY - 1, 2, 236, C_TEXT);
  for (int d = 1; d <= 9; d++) {
    lv_obj_t *b = lv_obj_create(sdk_p_play); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, SDK_BX + (d - 1) * SDK_CS, 282); lv_obj_set_size(b, SDK_CS, 28);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(b, lv_color_hex(C_SURF), 0); lv_obj_set_style_bg_color(b, lv_color_hex(C_LINE), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 1, 0); lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, sdk_pad_cb, LV_EVENT_CLICKED, (void *)(intptr_t)d);
    char t[2] = { (char)('0' + d), 0 }; lv_obj_t *l = ui_label(b, &lv_font_montserrat_16, C_TEXT, 0, t); lv_obj_center(l); lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_t *pb = ui_btn(sdk_p_play, 70, 24, "Back", NULL); lv_obj_set_pos(pb, 145, 314);
  lv_obj_add_event_cb(pb, sdk_play_back_cb, LV_EVENT_CLICKED, NULL);

  // ---- win ----
  sdk_p_win = sdk_panel(sudoku_root);
  ui_label(sdk_p_win, &lv_font_montserrat_20, C_TEXT, 0, "Solved!"); lv_obj_align(lv_obj_get_child(sdk_p_win, -1), LV_ALIGN_TOP_MID, 0, 50);
  sdk_w_time = ui_label(sdk_p_win, &lv_font_montserrat_48, C_TEXT, 0, "00:00"); lv_obj_align(sdk_w_time, LV_ALIGN_TOP_MID, 0, 90);
  sdk_w_new = ui_label(sdk_p_win, &lv_font_montserrat_14, C_RED, 0, "New best!"); lv_obj_align(sdk_w_new, LV_ALIGN_TOP_MID, 0, 152);
  sdk_w_best = ui_label(sdk_p_win, &lv_font_montserrat_14, C_GREY, 0, ""); lv_obj_align(sdk_w_best, LV_ALIGN_TOP_MID, 0, 176);
  lv_obj_t *np = ui_btn(sdk_p_win, 150, 40, "New puzzle", NULL); lv_obj_align(np, LV_ALIGN_TOP_MID, 0, 206);
  lv_obj_add_event_cb(np, sdk_menu_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *wb = ui_btn(sdk_p_win, 150, 36, "Back", NULL); lv_obj_align(wb, LV_ALIGN_TOP_MID, 0, 254);
  lv_obj_add_event_cb(wb, sdk_exit_btn_cb, LV_EVENT_CLICKED, NULL);

  sdk_tm = lv_timer_create(sdk_timer_cb, 1000, NULL); lv_timer_pause(sdk_tm);
  sdk_start_refresh(); sdk_show(sdk_p_start);
}
#endif
