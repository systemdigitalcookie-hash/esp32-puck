# esp32-puck firmware

Board: **Waveshare ESP32-S3-Touch-LCD-1.85B** (SKU 34554) - https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85B
Waveshare code: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85B (pinned commit in `release.sh`)

Firmware: Waveshare LVGL demo + Wi-Fi captive portal (WiFiManager) + HTTPS OTA from GitHub releases. Screen shows version and a "Check update" button. No secrets in code; Wi-Fi credentials are stored on the device.

Toolchain: arduino-cli 1.6.0, `esp32:esp32@3.2.0` (Waveshare needs >= 3.2.0), WiFiManager 2.0.17, Waveshare bundled libs (lvgl 8.3.x etc.).
FQBN: `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,FlashMode=qio,USBMode=hwcdc`
(16 MB flash, OPI PSRAM, two 3 MB OTA app slots.)

## 1. First flash (MacBook, USB-C) - once
1. Download `merged.bin` from the latest release: https://github.com/systemdigitalcookie-hash/esp32-puck/releases/latest/download/merged.bin
2. `python3 -m pip install esptool`
3. Boot mode: hold **BOOT**, plug in USB-C (or tap RESET while holding BOOT), release.
4. `ls /dev/cu.usbmodem*` then:
   `python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem* --baud 460800 write_flash 0x0 merged.bin`
5. Press RESET / replug. Browser alternative (Chrome/Edge, also Chrome on Android): https://espressif.github.io/esptool-js/ - Connect, flash address `0`, choose `merged.bin`, Program.
6. On first boot join Wi-Fi "Puck-Setup" from your phone and pick your network (portal opens automatically; else go to 192.168.4.1).

## 2. Updates over Wi-Fi (OTA)
- Puck fetches `https://github.com/systemdigitalcookie-hash/esp32-puck/releases/latest/download/version.txt`; if the number is higher than the running `FW_VERSION`, it downloads `firmware.bin` (follows GitHub redirects) and reboots into the other app slot.
- Tap **Check update** on the screen. (No automatic check yet.)
- TLS is validated against pinned root CAs in `certs.h` (USERTrust, ISRG, DigiCert, Sectigo, Baltimore). Time is set by NTP first. If GitHub changes CA and checks start failing, update `certs.h` and USB-flash once.

## 3. Publish a new version (on the box / any machine with gh + arduino-cli)
`./release.sh` bumps `version.h`, builds, commits, pushes and creates the release with `version.txt`, `firmware.bin` (OTA) and `merged.bin` (USB). `./release.sh 7` sets an explicit number.

## Recorder hardware notes
Dual mic via ES7210 + ES8311 codec/speaker pads; I2S MCLK=2 BCLK=48 WS=38 DIN=39 DOUT=47; I2C SDA=11 SCL=10; microSD SDMMC 4-bit CLK15 CMD14 D0=16 D1=17 D2=12 D3=13; 8 MB PSRAM. Waveshare ESP-IDF example `06_mic_test` and Arduino `06_esp_sr` are the starting points.

## Tauke voice page (v6+)
Page 2 (swipe left from Recorder). Hold **Hold to talk**, speak, release. The puck POSTs 16 kHz mono PCM over HTTPS to the
Tauke server, which runs xAI speech-to-text -> Grok chat ("Grok Tauke" persona, short per-device history) -> xAI text-to-speech
(16 kHz PCM), and plays the answer through the ES8311 speaker output (amp on GPIO9). Face: white rounded head, two black oval
eyes (idle blink / listening look-up / thinking side-to-side / speaking squish+bob). Errors show in red under the face.

- Server code: `tauke-server/` (Node 20+, no deps), deployed on Railway project `puck-tauke`, service `tauke`:
  `https://tauke-production.up.railway.app` (`GET /health`, `POST /talk`). Env: `XAI_API_KEY`, `DEVICE_TOKEN`,
  optional `CHAT_MODEL`, `STT_MODEL`, `TTS_VOICE`, `ALLOWED_DEVICES` (comma list of MACs), `DEVICE_DAILY_LIMIT`, `GLOBAL_DAILY_LIMIT`.
- Device secret: `sketch-ota/puck_ota/secrets.h` (gitignored) defines `TAUKE_URL` and `TAUKE_TOKEN`. Builds without it show
  "Tauke not configured". The token is XOR-scrambled in the binary (published firmware is public, so the server also
  rate-limits and can allow-list device MACs).
- Test: `tauke-server/test_talk.sh tauke-server/sample.wav` prints transcript + reply and saves `reply.wav`.
