#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "Audio.h"

// ---------- SD SPI ----------
static constexpr int PIN_SD_CS   = 10;
static constexpr int PIN_SD_SCK  = 12;
static constexpr int PIN_SD_MOSI = 11;
static constexpr int PIN_SD_MISO = 13;

// ---------- I2S ----------
static constexpr int PIN_I2S_BCLK = 4;
static constexpr int PIN_I2S_LRCK = 5;
static constexpr int PIN_I2S_DOUT = 6;

Audio audio;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n--- MP3 Playback Test ---");

  // SD init
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, SPI)) {
    Serial.println("❌ SD mount failed");
    while (true) delay(1000);
  }

  Serial.println("✅ SD mounted");

  if (!SD.exists("/Everything Goes With Blue.mp3")) {
    Serial.println("❌ /Everything Goes With Blue.mp3 not found");
    while (true) delay(1000);
  }

  // I2S init
  audio.setPinout(
    PIN_I2S_BCLK,
    PIN_I2S_LRCK,
    PIN_I2S_DOUT
  );

  audio.setVolume(21);  // 0–21 safe range

  Serial.println("▶ Playing Everything Goes With Blue.mp3");
  audio.connecttoFS(SD, "/Everything Goes With Blue.mp3");
}

void loop() {
  // REQUIRED: keeps audio flowing
  audio.loop();
}
