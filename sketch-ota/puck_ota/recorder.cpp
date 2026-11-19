// Meeting recorder: ES7210 dual mic (I2S, 16 kHz) -> mono 16-bit WAV on microSD or internal FAT.
#include "recorder.h"
#include "ESP_I2S.h"
#include "es7210.h"
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

RecInfo rec;
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
    SD_MMC.setPins(15, 14, 16, 17, 12, 13);   // CLK, CMD, D0..D3 (4-bit)
    sd_ok = SD_MMC.begin("/sdcard", false, false, 20000);
    if (sd_ok && SD_MMC.cardType() == CARD_NONE) { SD_MMC.end(); sd_ok = false; }
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

bool rec_init() {
  memset((void *)&rec, 0, sizeof rec);
  ring = (uint8_t *)heap_caps_malloc(RING_SZ, MALLOC_CAP_SPIRAM);
  if (!ring) { put_msg("no PSRAM"); return false; }
  es7210_i2c_config_t cfg = {}; cfg.i2c_addr = 0x40;
  if (es7210_new_codec(&cfg, &es_h) == ESP_OK) {
    es7210_codec_config_t cc = {};
    cc.i2s_format = ES7210_I2S_FMT_I2S; cc.mclk_ratio = 256; cc.sample_rate_hz = SR;
    cc.bit_width = ES7210_I2S_BITS_16B; cc.mic_bias = ES7210_MIC_BIAS_2V87; cc.mic_gain = ES7210_MIC_GAIN_30DB;
    cc.flags.tdm_enable = false;
    codec_ok = es7210_config_codec(es_h, &cc) == ESP_OK && es7210_config_volume(es_h, 40) == ESP_OK;
  }
  if (!codec_ok) put_msg("mic init failed");
  bool ok = mount_storage();
  if (ok && codec_ok) put_msg("ready");
  return ok && codec_ok;
}

void rec_start() {
  if (rec.state != REC_IDLE || !ring || !codec_ok) return;
  if (!mount_storage()) return;
  int c, maxidx; scan_files(&c, &maxidx, nullptr);
  char path[40]; snprintf(path, sizeof path, "/rec/rec_%04d.wav", maxidx + 1);
  start_free_bytes = free_bytes();
  if (start_free_bytes < 200000) { put_msg("storage full"); return; }
  wf = store->open(path, FILE_WRITE);
  if (!wf) { put_msg("cannot create file"); return; }
  uint8_t h[44]; wav_header(h, 0); wf.write(h, 44);
  data_bytes = 0; head = tail = 0; rec.dropped = 0; rec.secs = 0; rec.level = 0;
  i2s.setPins(PIN_BCK, PIN_WS, PIN_DOUT, PIN_DIN, PIN_MCK);
  i2s.setTimeout(1000);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    put_msg("i2s failed"); wf.close(); store->remove(path); return;
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
