#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include "Audio.h"

// SPI pins
static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_SPI_MISO = 13;

// SD + RFID
static constexpr int PIN_SD_CS    = 10;
static constexpr int PIN_RFID_SS  = 15;
static constexpr int PIN_RFID_RST = 0;

// I2S (MAX98357)
static constexpr int PIN_I2S_BCLK = 4;
static constexpr int PIN_I2S_LRCK = 5;
static constexpr int PIN_I2S_DOUT = 6;

Audio audio;
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

static bool isPlaying = false;
static String lastUid = "";
static uint32_t lastScanMs = 0;
static constexpr uint32_t SCAN_DEBOUNCE_MS = 800;

static String uidToHexUpper(const MFRC522::Uid &uid) {
  String s;
  s.reserve(uid.size * 2);
  for (byte i = 0; i < uid.size; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", uid.uidByte[i]);
    s += buf;
  }
  return s;
}

static void stopPlayback() {
  if (!isPlaying) return;
  Serial.println("Stopping playback...");
  audio.stopSong();
  isPlaying = false;
}

static void playForUid(const String &uidHex) {
  const String path = "/" + uidHex + ".mp3";

  if (!SD.exists(path.c_str())) {
    Serial.print("❌ File not found: ");
    Serial.println(path);
    return;
  }

  Serial.print("▶ Playing: ");
  Serial.println(path);

  audio.connecttoFS(SD, path.c_str());
  isPlaying = true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n--- RFID -> MP3 Player ---");

  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  pinMode(PIN_RFID_SS, OUTPUT);
  digitalWrite(PIN_RFID_SS, HIGH);

  if (!SD.begin(PIN_SD_CS, SPI)) {
    Serial.println("❌ SD mount failed");
    while (true) delay(1000);
  }
  Serial.println("✅ SD mounted");

  rfid.PCD_Init();
  Serial.print("RC522 Version: 0x");
  Serial.println(rfid.PCD_ReadRegister(rfid.VersionReg), HEX);
  Serial.println("✅ RFID initialized");

  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
  audio.setVolume(21);

  Serial.println("Scan a tag to play /<UID>.mp3 from SD root");
}

void loop() {
  // Keep audio flowing
  audio.loop();

  const uint32_t now = millis();
  if (now - lastScanMs < SCAN_DEBOUNCE_MS) return;

  // Scan tag
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  lastScanMs = now;

  const String uidHex = uidToHexUpper(rfid.uid);
  Serial.print("Scanned UID: ");
  Serial.println(uidHex);

  // If you want "scan same tag toggles stop", uncomment this block:
  // if (uidHex == lastUid && isPlaying) {
  //   stopPlayback();
  //   lastUid = "";
  // } else {
  //   stopPlayback();
  //   playForUid(uidHex);
  //   lastUid = uidHex;
  // }

  // Default behavior: always stop current and play the scanned UID
  stopPlayback();
  playForUid(uidHex);
  lastUid = uidHex;

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
