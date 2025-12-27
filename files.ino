#include <SD.h>

String AUDIO_ROOT = "/audio";

int PIN_SD_CS = 10;

bool endsWithMp3(const String &name) {
  if (name.length() < 4) return false;
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3");
}

static int countMp3FilesInFolder(const String &folderPath) {
  File dir = SD.open(folderPath.c_str());
  if (!dir) return 0;
  if (!dir.isDirectory()) { dir.close(); return 0; }

  int count = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = basenameOf(entry.name());
      if (endsWithMp3(name)) count++;
    }
    entry.close();
    entry = dir.openNextFile();
  }

  dir.close();
  return count;
}

static String findFolderForUid(const String &uidHex) {
  const String prefix = uidHex + "-";

  File root = SD.open(AUDIO_ROOT);
  if (!root) {
    Serial.println("Failed to open audio root");
    return "";
  }

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);

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

static String findOrCreateFolderForUid(const String &uidHex) {
  String existing = findFolderForUid(uidHex);
  if (existing.length() > 0) return existing;

  String folderPath = AUDIO_ROOT;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += uidHex;
  folderPath += "-new";

  if (SD.exists(folderPath.c_str())) {
    Serial.print("Folder already exists (unexpected): ");
    Serial.println(folderPath);
    return folderPath;
  }

  if (!SD.mkdir(folderPath.c_str())) {
    Serial.print("Failed to create folder: ");
    Serial.println(folderPath);
    return "";
  }

  Serial.print("Created folder: ");
  Serial.println(folderPath);
  return folderPath;
}

static int collectMp3Files(const String &folderPath, String *outFiles, int maxFiles) {
  File dir = SD.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  int n = 0;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = basenameOf(entry.name());
      if (endsWithMp3(name)) {
        if (n < maxFiles) {
          outFiles[n++] = name;
        } else {
          // too many files; ignore extras
          entry.close();
          break;
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }

  dir.close();
  return n;
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
