#include <Arduino.h>
#include <EasyButton.h>
#include <SPI.h>
#include <SD.h>
#include <MFRC522.h>
#include "Audio.h"

String AUDIO_ROOT = "/audio";

// Button pins
static constexpr int PIN_BUTTON_VOL_DOWN  = 16;
static constexpr int PIN_BUTTON_VOL_UP    = 17;
static constexpr int PIN_BUTTON_NEXT_SONG = 18;
EasyButton buttonVolDown(PIN_BUTTON_VOL_DOWN);
EasyButton buttonVolUp(PIN_BUTTON_VOL_UP);
EasyButton buttonNextSong(PIN_BUTTON_NEXT_SONG);

// SPI pins
static constexpr int PIN_SPI_SCK  = 12;
static constexpr int PIN_SPI_MOSI = 11;
static constexpr int PIN_SPI_MISO = 13;

// SD + RFID
static constexpr int PIN_SD_CS    = 10;
static constexpr int PIN_RFID_SS  = 15;
static constexpr int PIN_RFID_RST = 8;

// I2S (MAX98357)
static constexpr int PIN_I2S_BCLK = 4;
static constexpr int PIN_I2S_LRCK = 5;
static constexpr int PIN_I2S_DOUT = 6;

Audio audio;
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

static bool isPlaying = false;
static String lastUid = "";
static int volumeLevel = 10;
static uint32_t lastScanMs = 0;
static constexpr uint32_t SCAN_DEBOUNCE_MS = 800;

// ---------- Playlist state ----------
static constexpr int MAX_TRACKS = 32;
static String playlist[MAX_TRACKS];
static int playlistCount = 0;
static int playlistIndex = 0;
static String currentFolder = "";

// Set by EOF event, handled in loop()
static volatile bool trackEnded = false;

// Forward declare
static void onAudioEvent(Audio::msg_t m);

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

static void increaseVolume() {
  if (volumeLevel < 21) {
    volumeLevel++;
    audio.setVolume(volumeLevel);
    Serial.print("Volume: ");
    Serial.println(volumeLevel);
  }
}

static void decreaseVolume() {
  if (volumeLevel > 1) {
    volumeLevel--;
    audio.setVolume(volumeLevel);
    Serial.print("Volume: ");
    Serial.println(volumeLevel);
  }
}

static bool endsWithMp3(const String &name) {
  if (name.length() < 4) return false;
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3");
}

static void sortPlaylist() {
  // Simple bubble sort (small N)
  for (int i = 0; i < playlistCount - 1; i++) {
    for (int j = 0; j < playlistCount - i - 1; j++) {
      if (playlist[j] > playlist[j + 1]) {
        String tmp = playlist[j];
        playlist[j] = playlist[j + 1];
        playlist[j + 1] = tmp;
      }
    }
  }
}

static String findFolderForUid(const String &uidHex) {
  const String prefix = uidHex + "-";

  File root = SD.open(AUDIO_ROOT);
  if (!root) {
    Serial.println("❌ Failed to open root directory");
    return "";
  }

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name(); // should be like "04A1C29F-desc"
      if (name.startsWith(prefix)) {
        entry.close();
        root.close();
        return AUDIO_ROOT + "/" + name; // return full path
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  return "";
}

static void clearPlaylist() {
  for (int i = 0; i < playlistCount; i++) {
    playlist[i] = "";
  }
  playlistCount = 0;
  playlistIndex = 0;
  currentFolder = "";
}

static bool buildPlaylistForFolder(const String &folderPath) {
  clearPlaylist();
  currentFolder = folderPath;

  File dir = SD.open(folderPath.c_str());
  if (!dir) {
    Serial.print("❌ Failed to open folder: ");
    Serial.println(folderPath);
    return false;
  }
  if (!dir.isDirectory()) {
    Serial.print("❌ Not a directory: ");
    Serial.println(folderPath);
    dir.close();
    return false;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name(); // may include path depending on FS impl; handle both
      // Normalize to just filename if it includes folder prefix
      if (name.startsWith(folderPath)) {
        // sometimes name is like "/04A1...-x/file.mp3"
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
      }

      if (endsWithMp3(name)) {
        if (playlistCount < MAX_TRACKS) {
          playlist[playlistCount++] = name;
        } else {
          Serial.println("⚠️ Playlist full; ignoring extra files");
          break;
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }

  dir.close();

  if (playlistCount == 0) {
    Serial.print("❌ No .mp3 files found in ");
    Serial.println(folderPath);
    return false;
  }

  sortPlaylist();

  Serial.print("✅ Playlist built (");
  Serial.print(playlistCount);
  Serial.println(" tracks):");
  for (int i = 0; i < playlistCount; i++) {
    Serial.print("  ");
    Serial.println(playlist[i]);
  }

  return true;
}

static void stopPlayback() {
  if (!isPlaying) return;
  Serial.println("Stopping playback...");
  audio.stopSong();
  isPlaying = false;
}

static void playNextTrack() {
  trackEnded = false;
  isPlaying = false;

  playlistIndex++;
  if (playlistIndex >= playlistCount) {
    Serial.println("✅ Folder finished (end of playlist)");
    Serial.println("Restarting playlist...");
    playlistIndex = 0;
    playCurrentTrack();
  } else {
    Serial.println("Next track...");
    playCurrentTrack();
  }
}

static void playCurrentTrack() {
  if (playlistCount == 0) return;
  if (playlistIndex < 0 || playlistIndex >= playlistCount) return;

  const String path = currentFolder + "/" + playlist[playlistIndex];

  if (!SD.exists(path.c_str())) {
    Serial.print("❌ Missing file: ");
    Serial.println(path);
    isPlaying = false;
    return;
  }

  Serial.print("▶ Playing: ");
  Serial.println(path);

  audio.connecttoFS(SD, path.c_str());
  isPlaying = true;
}

static void startFolderPlaybackForUid(const String &uidHex) {
  const String folder = findFolderForUid(uidHex);
  if (folder == "") {
    Serial.print("❌ No folder found for UID prefix: ");
    Serial.println(uidHex + "-");
    return;
  }

  Serial.print("Found folder: ");
  Serial.println(folder);

  if (!buildPlaylistForFolder(folder)) {
    return;
  }

  playlistIndex = 0;
  trackEnded = false;
  playCurrentTrack();
}

static void onAudioEvent(Audio::msg_t m) {
  if (m.e == Audio::evt_eof) {
    // Don't touch SD / Audio here—just signal loop()
    trackEnded = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_BUTTON_VOL_DOWN, INPUT);
  buttonVolDown.begin();
  buttonVolDown.onPressed(decreaseVolume);
  pinMode(PIN_BUTTON_VOL_UP, INPUT);
  buttonVolUp.begin();
  buttonVolUp.onPressed(increaseVolume);
  pinMode(PIN_BUTTON_NEXT_SONG, INPUT);
  buttonNextSong.begin();
  buttonNextSong.onPressed(playNextTrack);

  Serial.println("\n--- RFID -> Folder Playlist Player ---");

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
  audio.setVolume(volumeLevel);

  Audio::audio_info_callback = onAudioEvent;

  Serial.println("Ready to play!");
}

void loop() {
  // Check for input
  buttonVolDown.read();
  buttonVolUp.read();
  buttonNextSong.read();

  // Keep audio flowing
  audio.loop();

  // Advance playlist on EOF (handled here, not inside callback)
  if (trackEnded) {
    playNextTrack();
  }

  const uint32_t now = millis();
  if (now - lastScanMs < SCAN_DEBOUNCE_MS) return;

  // Scan tag
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  lastScanMs = now;

  const String uidHex = uidToHexUpper(rfid.uid);
  Serial.print("Scanned UID: ");
  Serial.println(uidHex);

  // Ignore same tag while currently playing
  if (!(uidHex == lastUid && isPlaying)) {
    stopPlayback();
    clearPlaylist();
    startFolderPlaybackForUid(uidHex);
    lastUid = uidHex;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
