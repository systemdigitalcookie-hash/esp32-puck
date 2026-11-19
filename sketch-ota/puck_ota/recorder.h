#pragma once
#include <Arduino.h>

enum RecStateE { REC_IDLE = 0, REC_RECORDING, REC_FINALIZING };

struct RecInfo {
  volatile int state;          // RecStateE
  volatile int level;          // 0..100 live mic level
  volatile uint32_t secs;      // elapsed seconds of current/last recording
  volatile uint32_t free_secs; // estimated remaining recording time on current storage
  volatile int count;          // number of saved rec_XXXX.wav files
  volatile uint32_t dropped;   // bytes dropped because storage was too slow (should stay 0)
  char storage[8];             // "SD" or "Flash"
  char last[24];               // last saved file name
  char msg[40];                // short status/error message
  char mic[64];                // mic diagnostics (init + self-test result)
  char sd[32];                 // SD diagnostics
  char i2c[72];                // I2C scan result (7-bit hex addresses)
};

extern RecInfo rec;

bool rec_init();    // call once after I2C (Wire) is up
void rec_start();   // no-op unless idle
void rec_stop();    // asks recorder to stop and finalize the WAV

// ---- recordings list / playback / delete ----
struct RecFile { char name[24]; uint32_t bytes; };
int  rec_list(RecFile *out, int max);        // newest first; empty while recording
bool rec_delete(const char *name);
void rec_play(const char *name);             // plays through ES8311 + speaker pads; call from the UI task
void rec_play_stop();
extern volatile bool rec_playing;
