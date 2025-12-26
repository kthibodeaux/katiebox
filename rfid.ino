#include <MFRC522.h>

int PIN_RFID_SS = 15;
int PIN_RFID_RST = 8;
uint32_t SCAN_DEBOUNCE_MS = 800;

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

static String lastUid = "";
static uint32_t lastScanMs = 0;

String uidToHexUpper(const byte *uidBytes, byte size) {
  String s;
  s.reserve(size * 2);
  for (byte i = 0; i < size; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", uidBytes[i]);
    s += buf;
  }
  return s;
}

void rfidSetup() {
  pinMode(PIN_RFID_SS, OUTPUT);
  digitalWrite(PIN_RFID_SS, HIGH);

  rfid.PCD_Init();
  Serial.print("RC522 Version: 0x");
  Serial.println(rfid.PCD_ReadRegister(rfid.VersionReg), HEX);
  Serial.println("RFID initialized");
}

void rfidLoop() {
  const uint32_t now = millis();
  if (now - lastScanMs < SCAN_DEBOUNCE_MS) return;

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  lastScanMs = now;

  const String uidHex = uidToHexUpper(rfid.uid.uidByte, rfid.uid.size);
  Serial.print("Scanned UID: ");
  Serial.println(uidHex);

  if (adminRfid.length() > 0 && uidHex == adminRfid) {
    Serial.println("Admin tag scanned");

    stopPlayback();
    clearPlaylist();

    if (webIsEnabled()) {
      webDisable();
    } else {
      webEnable();
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  if (webIsEnabled()) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  if (!(uidHex == lastUid && isPlaying)) {
    stopPlayback();
    clearPlaylist();
    startFolderPlaybackForUid(uidHex);
    lastUid = uidHex;
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
