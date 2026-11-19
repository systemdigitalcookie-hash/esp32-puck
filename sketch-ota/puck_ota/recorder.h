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
};

extern RecInfo rec;

bool rec_init();    // call once after I2C (Wire) is up
void rec_start();   // no-op unless idle
void rec_stop();    // asks recorder to stop and finalize the WAV
