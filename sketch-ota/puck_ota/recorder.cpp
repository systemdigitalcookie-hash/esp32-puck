// Meeting recorder: ES7210 dual mic (I2S, 16 kHz) -> mono 16-bit WAV on microSD or internal FAT.
#include "recorder.h"
#include "ESP_I2S.h"
#include "es7210.h"
#include "es8311.h"
#include <algorithm>
#include "FS.h"
#include "SD_MMC.h"
#include "FFat.h"
#include "Wire.h"
#include "esp_heap_caps.h"

#define PIN_MCK GPIO_NUM_2
#define PIN_BCK GPIO_NUM_48
#define PIN_WS  GPIO_NUM_38
#define PIN_DIN GPIO_NUM_39
#define PIN_DOUT GPIO_NUM_47
#define SR 16000
#define BYTES_PER_SEC (SR * 2)
#define RING_SZ (256 * 1024)
#define CHUNK_FRAMES 512

extern int es7210_last_wire_err, es7210_last_reg;
RecInfo rec;
static uint8_t es_addr = 0x40;
static I2SClass i2s;
static es7210_dev_handle_t es_h = NULL;
static fs::FS *store = nullptr;
static bool sd_ok = false, ffat_ok = false, codec_ok = false;
static uint8_t *ring = nullptr;
static volatile uint32_t head = 0, tail = 0;     // monotonically increasing byte counters
static volatile bool stop_req = false, cap_done = true;
static File wf;
static uint32_t data_bytes = 0;
static uint32_t start_free_bytes = 0;

static void put_msg(const char *m) { strncpy(rec.msg, m, sizeof rec.msg - 1); rec.msg[sizeof rec.msg - 1] = 0; }

static void wav_header(uint8_t *h, uint32_t data_len) {
  uint32_t riff = 36 + data_len, sr = SR, br = BYTES_PER_SEC, fmtlen = 16;
  uint16_t fmt = 1, ch = 1, ba = 2, bps = 16;
  memcpy(h, "RIFF", 4); memcpy(h + 4, &riff, 4); memcpy(h + 8, "WAVEfmt ", 8);
  memcpy(h + 16, &fmtlen, 4); memcpy(h + 20, &fmt, 2); memcpy(h + 22, &ch, 2);
  memcpy(h + 24, &sr, 4); memcpy(h + 28, &br, 4); memcpy(h + 32, &ba, 2); memcpy(h + 34, &bps, 2);
  memcpy(h + 36, "data", 4); memcpy(h + 40, &data_len, 4);
}

static void rewrite_header(uint32_t n) {
  uint8_t h[44]; wav_header(h, n);
  size_t pos = wf.position();
  wf.seek(0); wf.write(h, 44); wf.flush(); wf.seek(pos);
}

static void scan_files(int *count, int *maxidx, char *lastname) {
  *count = 0; *maxidx = 0; if (lastname) lastname[0] = 0;
  if (!store) return;
  File d = store->open("/rec");
  if (!d || !d.isDirectory()) return;
  File f = d.openNextFile();
  while (f) {
    const char *nm = f.name();
    int idx;
    if (!f.isDirectory() && sscanf(nm, "rec_%d.wav", &idx) == 1) {
      (*count)++;
      if (idx >= *maxidx) { *maxidx = idx; if (lastname) strncpy(lastname, nm, 23), lastname[23] = 0; }
    }
    f = d.openNextFile();
  }
}

static uint32_t free_bytes() {
  if (store == &SD_MMC) return (uint32_t)min<uint64_t>(SD_MMC.totalBytes() - SD_MMC.usedBytes(), 0xFFFFFFFFULL);
  return (uint32_t)FFat.freeBytes();
}

static bool mount_storage() {
  if (!sd_ok) {
    bool pins = SD_MMC.setPins(15, 14, 16, 17, 12, 13);
    sd_ok = SD_MMC.begin("/sdcard", false, false, 20000);          // 4-bit, 20 MHz
    if (!sd_ok) sd_ok = SD_MMC.begin("/sdcard", true, false, 5000); // 1-bit, 5 MHz fallback
    if (sd_ok && SD_MMC.cardType() == CARD_NONE) { SD_MMC.end(); sd_ok = false; }
    snprintf(rec.sd, sizeof rec.sd, sd_ok ? "SD card ok" : (pins ? "no SD card" : "SD pins err"));
  }
  if (sd_ok) { store = &SD_MMC; strcpy(rec.storage, "SD"); }
  else {
    if (!ffat_ok) ffat_ok = FFat.begin(true);
    if (ffat_ok) { store = &FFat; strcpy(rec.storage, "Flash"); } else store = nullptr;
  }
  if (!store) { put_msg("no storage"); return false; }
  if (!store->exists("/rec")) store->mkdir("/rec");
  int c, m; scan_files(&c, &m, rec.last);
  rec.count = c;
  rec.free_secs = free_bytes() / BYTES_PER_SEC;
  return true;
}

static void capture_task(void *) {
  static int16_t st[CHUNK_FRAMES * 2];
  static int16_t mono[CHUNK_FRAMES];
  int skip = 4;  // discard first chunks (codec settling)
  while (!stop_req) {
    size_t got = i2s.readBytes((char *)st, sizeof st);
    if (got < sizeof st) { continue; }
    if (skip > 0) { skip--; continue; }
    int peak = 0;
    for (int i = 0; i < CHUNK_FRAMES; i++) {
      int m = ((int)st[2 * i] + (int)st[2 * i + 1]) / 2;   // mix both mics to mono
      mono[i] = (int16_t)m;
      int a = m < 0 ? -m : m; if (a > peak) peak = a;
    }
    rec.level = (int)(sqrtf(peak / 12000.0f) * 100.0f); if (rec.level > 100) rec.level = 100;
    uint32_t n = sizeof mono;
    if (RING_SZ - (head - tail) < n) { rec.dropped += n; continue; }
    uint32_t off = head % RING_SZ, first = min<uint32_t>(n, RING_SZ - off);
    memcpy(ring + off, mono, first); if (n > first) memcpy(ring, (uint8_t *)mono + first, n - first);
    head += n;
  }
  rec.level = 0;
  cap_done = true;
  vTaskDelete(NULL);
}

static void writer_task(void *) {
  static uint8_t blk[16384];
  uint32_t last_hdr = 0;
  for (;;) {
    uint32_t avail = head - tail;
    bool finishing = cap_done;
    if (avail >= 8192 || (finishing && avail > 0)) {
      uint32_t n = min<uint32_t>(avail, sizeof blk);
      uint32_t off = tail % RING_SZ, first = min<uint32_t>(n, RING_SZ - off);
      memcpy(blk, ring + off, first); if (n > first) memcpy(blk + first, ring, n - first);
      if (wf.write(blk, n) != n) { put_msg("write error"); stop_req = true; }
      tail += n; data_bytes += n;
      rec.secs = data_bytes / BYTES_PER_SEC;
      if (data_bytes + 65536 > start_free_bytes && !stop_req) { put_msg("storage full"); stop_req = true; }
      if (rec.secs >= last_hdr + 15) { last_hdr = rec.secs; rewrite_header(data_bytes); }
      uint32_t used_secs = data_bytes / BYTES_PER_SEC, total = start_free_bytes / BYTES_PER_SEC;
      rec.free_secs = total > used_secs ? total - used_secs : 0;
    } else if (finishing) {
      break;
    } else vTaskDelay(pdMS_TO_TICKS(10));
  }
  rewrite_header(data_bytes);
  wf.close();
  i2s.end();
  int c, m; scan_files(&c, &m, rec.last);
  rec.count = c;
  if (!strcmp(rec.msg, "recording")) put_msg("saved");
  rec.free_secs = free_bytes() / BYTES_PER_SEC;
  rec.state = REC_IDLE;
  vTaskDelete(NULL);
}

static void i2c_scan() {
  char *p = rec.i2c; size_t left = sizeof rec.i2c;
  int n = snprintf(p, left, "I2C:"); p += n; left -= n;
  for (int a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0 && left > 5) { n = snprintf(p, left, " %02X", a); p += n; left -= n; }
  }
}

static bool addr_present(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }

// NOTE: es7210_config_volume() only accepts -95..32 dB; v2/v3 passed 40 (copied from Waveshare's sample) -> ESP_ERR_INVALID_ARG
// with no I2C error, shown as "es7210 cfg err". Now 0 dB (analog gain is set separately to 30 dB).
// Configure ES7210 at es_addr. Call with I2S already running so MCLK is present (as in Waveshare's apps).
static bool codec_configure(char *err, size_t en) {
  if (es_h) { es7210_del_codec(es_h); es_h = NULL; }
  es7210_i2c_config_t cfg = {}; cfg.i2c_addr = es_addr;
  es7210_last_wire_err = 0; es7210_last_reg = -1;
  if (es7210_new_codec(&cfg, &es_h) != ESP_OK) { snprintf(err, en, "es7210 alloc fail"); return false; }
  es7210_codec_config_t cc = {};
  cc.i2s_format = ES7210_I2S_FMT_I2S; cc.mclk_ratio = 256; cc.sample_rate_hz = SR;
  cc.bit_width = ES7210_I2S_BITS_16B; cc.mic_bias = ES7210_MIC_BIAS_2V87; cc.mic_gain = ES7210_MIC_GAIN_30DB;
  cc.flags.tdm_enable = false;
  if (es7210_config_codec(es_h, &cc) != ESP_OK || es7210_config_volume(es_h, 0) != ESP_OK) {
    if (es7210_last_wire_err) snprintf(err, en, "i2c e%d @0x%02X reg 0x%02X", es7210_last_wire_err, es_addr, es7210_last_reg);
    else snprintf(err, en, "es7210 cfg err @0x%02X", es_addr);
    return false;
  }
  return true;
}

static bool i2s_open() {
  i2s.setPins(PIN_BCK, PIN_WS, PIN_DOUT, PIN_DIN, PIN_MCK);
  i2s.setTimeout(1000);
  return i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

// Open I2S (MCLK on), configure codec; tries addresses 0x40-0x43 that answer on the bus.
static bool mic_open(char *err, size_t en) {
  if (!i2s_open()) { snprintf(err, en, "i2s begin failed"); return false; }
  delay(30);
  bool found = false;
  for (uint8_t a = 0x40; a <= 0x43; a++) {
    if (!addr_present(a)) continue;
    found = true; es_addr = a;
    if (codec_configure(err, en)) { delay(20); return true; }
  }
  if (!found) snprintf(err, en, "no ES7210 at 0x40-0x43");
  i2s.end();
  return false;
}

bool rec_init() {
  memset((void *)&rec, 0, sizeof rec);
  strcpy(rec.mic, "mic: not tested");
  i2c_scan();
  ring = (uint8_t *)heap_caps_malloc(RING_SZ, MALLOC_CAP_SPIRAM);
  if (!ring) { put_msg("no PSRAM"); snprintf(rec.mic, sizeof rec.mic, "mic: no PSRAM"); return false; }
  char err[48];
  if (mic_open(err, sizeof err)) {
    // self-test: read ~0.25 s and report level so we know audio really flows
    static int16_t t[CHUNK_FRAMES * 2]; int peak = 0, ok = 0;
    for (int i = 0; i < 12; i++) {
      size_t g = i2s.readBytes((char *)t, sizeof t);
      if (g == sizeof t) { ok++; if (i >= 4) for (int k = 0; k < CHUNK_FRAMES * 2; k++) { int a = abs((int)t[k]); if (a > peak) peak = a; } }
    }
    i2s.end();
    codec_ok = ok >= 8;
    if (codec_ok) snprintf(rec.mic, sizeof rec.mic, "mic: ok @0x%02X peak %d", es_addr, peak);
    else snprintf(rec.mic, sizeof rec.mic, "mic: i2s read timeout @0x%02X (%d/12)", es_addr, ok);
  } else snprintf(rec.mic, sizeof rec.mic, "mic: %s", err);
  bool ok = mount_storage();
  if (ok && codec_ok) put_msg("ready"); else if (!codec_ok) put_msg("mic problem");
  return ok && codec_ok;
}

void rec_start() {
  if (rec.state != REC_IDLE || !ring) return;
  if (rec_playing) { put_msg("stop playback first"); return; }
  if (!mount_storage()) return;
  int c, maxidx; scan_files(&c, &maxidx, nullptr);
  char path[40]; snprintf(path, sizeof path, "/rec/rec_%04d.wav", maxidx + 1);
  start_free_bytes = free_bytes();
  if (start_free_bytes < 200000) { put_msg("storage full"); return; }
  wf = store->open(path, FILE_WRITE);
  if (!wf) { put_msg("cannot create file"); return; }
  uint8_t h[44]; wav_header(h, 0); wf.write(h, 44);
  data_bytes = 0; head = tail = 0; rec.dropped = 0; rec.secs = 0; rec.level = 0;
  char err[48];
  if (!mic_open(err, sizeof err)) {
    snprintf(rec.mic, sizeof rec.mic, "mic: %s", err);
    put_msg("mic start failed"); wf.close(); store->remove(path); return;
  }
  stop_req = false; cap_done = false;
  put_msg("recording");
  rec.state = REC_RECORDING;
  xTaskCreatePinnedToCore(writer_task, "wr", 6144, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(capture_task, "cap", 6144, NULL, 5, NULL, 0);
}

void rec_stop() {
  if (rec.state != REC_RECORDING) return;
  rec.state = REC_FINALIZING;
  stop_req = true;
}

// ---------------- recordings list / delete ----------------
int rec_list(RecFile *out, int max) {
  int n = 0;
  if (!store || rec.state != REC_IDLE) return 0;
  File d = store->open("/rec");
  if (!d || !d.isDirectory()) return 0;
  File f = d.openNextFile();
  while (f && n < max) {
    int idx;
    if (!f.isDirectory() && sscanf(f.name(), "rec_%d.wav", &idx) == 1) {
      strncpy(out[n].name, f.name(), 23); out[n].name[23] = 0; out[n].bytes = f.size(); n++;
    }
    f = d.openNextFile();
  }
  std::sort(out, out + n, [](const RecFile &a, const RecFile &b) { return strcmp(a.name, b.name) > 0; });
  return n;
}

bool rec_delete(const char *name) {
  if (!store || rec.state != REC_IDLE || rec_playing) return false;
  char p[40]; snprintf(p, sizeof p, "/rec/%s", name);
  bool ok = store->remove(p);
  int c, m; scan_files(&c, &m, rec.last); rec.count = c;
  rec.free_secs = free_bytes() / BYTES_PER_SEC;
  put_msg(ok ? "deleted" : "delete failed");
  return ok;
}

// ---------------- playback (ES8311 DAC + speaker amp on GPIO9) ----------------
#define PIN_PA 9
volatile bool rec_playing = false;
static volatile bool play_stop = false;
static File pf;
static es8311_handle_t dac_h = NULL;

static void play_task(void *) {
  static uint8_t buf[4096];
  while (!play_stop) {
    int n = pf.read(buf, sizeof buf);
    if (n <= 0) break;
    i2s.write(buf, n & ~1);
  }
  delay(120);                       // let DMA drain
  digitalWrite(PIN_PA, LOW);
  pf.close();
  i2s.end();
  rec_playing = false;
  vTaskDelete(NULL);
}

void rec_play(const char *name) {
  if (rec.state != REC_IDLE || rec_playing || !store) return;
  char p[40]; snprintf(p, sizeof p, "/rec/%s", name);
  pf = store->open(p, FILE_READ);
  if (!pf) { put_msg("cannot open file"); return; }
  uint8_t h[44];
  if (pf.read(h, 44) != 44 || memcmp(h, "RIFF", 4)) { pf.close(); put_msg("bad wav"); return; }
  pinMode(PIN_PA, OUTPUT); digitalWrite(PIN_PA, LOW);
  i2s.setPins(PIN_BCK, PIN_WS, PIN_DOUT, PIN_DIN, PIN_MCK);
  i2s.setTimeout(1000);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    pf.close(); put_msg("play: i2s failed"); return;
  }
  // codec setup runs here (UI task) so I2C is never used from two tasks at once
  if (!dac_h) dac_h = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  const es8311_clock_config_t clk = { .mclk_inverted = false, .sclk_inverted = false, .mclk_from_mclk_pin = true,
                                      .mclk_frequency = SR * 256, .sample_frequency = SR };
  if (!dac_h || es8311_init(dac_h, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
      es8311_voice_volume_set(dac_h, 70, NULL) != ESP_OK || es8311_microphone_config(dac_h, false) != ESP_OK) {
    pf.close(); i2s.end(); put_msg("play: es8311 failed"); return;
  }
  digitalWrite(PIN_PA, HIGH);
  play_stop = false; rec_playing = true; put_msg("playing");
  xTaskCreatePinnedToCore(play_task, "play", 6144, NULL, 3, NULL, 0);
}

void rec_play_stop() { play_stop = true; }
