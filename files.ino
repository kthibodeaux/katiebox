#include <SD.h>

String AUDIO_ROOT = "/audio";

int PIN_SD_CS = 10;

bool endsWithMp3(const String &name) {
  if (name.length() < 4) return false;
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3");
}

String findFolderForUid(const String &uidHex) {
  const String prefix = uidHex + "-";

  File root = SD.open(AUDIO_ROOT);
  if (!root) {
    Serial.println("Failed to open root directory");
    return "";
  }

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      if (name.startsWith(prefix)) {
        entry.close();
        root.close();
        return AUDIO_ROOT + "/" + name;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();
  return "";
}

void filesSetup() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  if (!SD.begin(PIN_SD_CS, SPI)) {
    Serial.println("SD mount failed");
    while (true) delay(1000);
  }
  Serial.println("SD mounted");
}
