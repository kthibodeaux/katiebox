#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

extern String wifiSsid;
extern String wifiPass;

static WebServer server(80);
static bool webEnabled = false;

static bool serveIndexHtml() {
  if (!SD.exists("/www/index.html")) {
    server.send(404, "text/plain", "Missing /www/index.html");
    return true;
  }

  File f = SD.open("/www/index.html", FILE_READ);
  if (!f) {
    server.send(500, "text/plain", "Failed to open index.html");
    return true;
  }

  server.streamFile(f, "text/html");
  f.close();
  return true;
}

static bool connectWifi() {
  if (wifiSsid.length() == 0) {
    Serial.println("No SSID configured");
    return false;
  }

  Serial.print("Connecting WiFi: ");
  Serial.println(wifiSsid);

  WiFi.mode(WIFI_STA);

  if (wifiPass.length() == 0) {
    WiFi.begin(wifiSsid.c_str());
  } else {
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  }

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    return false;
  }

  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static void handleRoot() {
  serveIndexHtml();
}

static void handleStatusJson() {
  String body = "{";
  body += "\"lastUid\":\"";
  body += lastScannedUid;
  body += "\"}";
  server.send(200, "application/json", body);
}

void webSetup() {
  webEnabled = false;
}

void webEnable() {
  if (webEnabled) return;

  Serial.println("Enabling web mode");

  if (!connectWifi()) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status.json", HTTP_GET, handleStatusJson);
  server.begin();

  Serial.println("HTTP server started");
  webEnabled = true;
}

void webDisable() {
  if (!webEnabled) return;

  Serial.println("Disabling web mode");

  server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  webEnabled = false;
}

void webLoop() {
  if (webEnabled) {
    server.handleClient();
  }
}

bool webIsEnabled() {
  return webEnabled;
}
