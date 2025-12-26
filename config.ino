#include <Arduino.h>
#include <SD.h>

String wifiSsid = "";
String wifiPass = "";
String adminRfid = "";

static void parseConfigLine(const String& line) {
  int eq = line.indexOf('=');
  if (eq < 0) return;

  String key = line.substring(0, eq);
  String val = line.substring(eq + 1);

  key.trim();
  key.toLowerCase();
  val.trim();

  if (key == "ssid") {
    wifiSsid = val;
  } else if (key == "pass" || key == "password") {
    wifiPass = val;
  } else if (key == "adminrfid") {
    val.toUpperCase();
    adminRfid = val;
  }
}

static bool loadConfigFromSd() {
  File f = SD.open("/config.txt", FILE_READ);
  if (!f) {
    Serial.println("config.txt not found (web disabled)");
    return false;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;
    if (line.startsWith("#")) continue;

    parseConfigLine(line);
  }

  f.close();
  return true;
}

void configSetup() {
  wifiSsid = "";
  wifiPass = "";
  adminRfid = "";

  if (!loadConfigFromSd()) return;

  Serial.println("Config loaded:");
  Serial.print("  ssid: ");
  Serial.println(wifiSsid.length() ? wifiSsid : "(none)");

  Serial.print("  adminrfid: ");
  Serial.println(adminRfid.length() ? adminRfid : "(none)");
}
