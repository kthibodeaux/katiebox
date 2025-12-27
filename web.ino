#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

extern String wifiSsid;
extern String wifiPass;
extern String lastScannedUid;
extern String AUDIO_ROOT;

static WebServer server(80);
static bool webEnabled = false;
static constexpr int MAX_FILES = 128;

static String contentTypeForPath(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  return "application/octet-stream";
}

static String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

static String basenameOf(const String &path) {
  int slash = path.lastIndexOf('/');
  if (slash >= 0) return path.substring(slash + 1);
  return path;
}

static void splitFolderName(const String &folderName, String &uidOut, String &descOut) {
  int dash = folderName.indexOf('-');
  if (dash < 0) {
    uidOut = folderName;
    descOut = "";
    return;
  }

  uidOut = folderName.substring(0, dash);
  descOut = folderName.substring(dash + 1);
}

static void appendStringArrayJson(String &out, String *arr, int count) {
  out += "[";
  for (int i = 0; i < count; i++) {
    if (i > 0) out += ",";
    out += "\"";
    out += jsonEscape(arr[i]);
    out += "\"";
  }
  out += "]";
}

static bool serveFileFromWww(String uri) {
  if (uri == "/") uri = "/index.html";

  // Prevent weird path traversal attempts
  if (uri.indexOf("..") >= 0) {
    server.send(400, "text/plain; charset=utf-8", "Bad path");
    return true;
  }

  const String path = "/www" + uri;

  if (!SD.exists(path.c_str())) {
    server.send(404, "text/plain; charset=utf-8", "Not found");
    return true;
  }

  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) {
    server.send(500, "text/plain; charset=utf-8", "Failed to open file");
    return true;
  }

  server.streamFile(f, contentTypeForPath(path));
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

  if (wifiPass.length() == 0) WiFi.begin(wifiSsid.c_str());
  else WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

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

static void handleNotFound() {
  serveFileFromWww(server.uri());
}

static void handleIndexJson() {
  // Open audio root
  File root = SD.open(AUDIO_ROOT.c_str());
  if (!root) {
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"failed to open audio root\"}");
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"audio root not a directory\"}");
    return;
  }

  String body;
  body.reserve(2048);
  body += "{\"folders\":[";

  bool first = true;

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String folderName = basenameOf(entry.name()); // e.g. "04A1C29F-pokemon"
      String uid, desc;
      splitFolderName(folderName, uid, desc);

      // Determine "isLastScanned" by comparing UID portion to lastScannedUid
      bool isLast = (lastScannedUid.length() > 0 && uid == lastScannedUid);

      // Count mp3 files inside the directory
      // Build full folder path: AUDIO_ROOT + "/" + folderName
      String folderPath = AUDIO_ROOT;
      if (!folderPath.endsWith("/")) folderPath += "/";
      folderPath += folderName;

      int mp3Count = countMp3FilesInFolder(folderPath);

      if (!first) body += ",";
      first = false;

      body += "{";
      body += "\"name\":\"" + jsonEscape(folderName) + "\",";
      body += "\"uid\":\"" + jsonEscape(uid) + "\",";
      body += "\"description\":\"" + jsonEscape(desc) + "\",";
      body += "\"isLastScanned\":";
      body += (isLast ? "true" : "false");
      body += ",";
      body += "\"mp3Count\":";
      body += String(mp3Count);
      body += "}";
    }

    entry.close();
    entry = root.openNextFile();
  }

  root.close();

  body += "]}";
  server.send(200, "application/json; charset=utf-8", body);
}

static void appendMp3FilesJsonArray(String &out, const String &folderPath) {
  File dir = SD.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    out += "[]";
    return;
  }

  out += "[";

  bool first = true;
  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = basenameOf(entry.name());

      if (endsWithMp3(name)) {
        if (!first) out += ",";
        first = false;
        out += "\"";
        out += jsonEscape(name);
        out += "\"";
      }
    }

    entry.close();
    entry = dir.openNextFile();
  }

  out += "]";
  dir.close();
}

static void handleFolderJson() {
  if (!server.hasArg("name")) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"missing name param\"}");
    return;
  }

  String name = server.arg("name");
  name.trim();

  if (name.length() == 0) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"empty name param\"}");
    return;
  }

  // Basic safety: don't allow slashes or traversal
  if (name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"invalid name param\"}");
    return;
  }

  // If passed just a UID, find first folder starting with UID-
  const bool looksLikeUidOnly = (name.indexOf('-') < 0);
  const String prefix = looksLikeUidOnly ? (name + "-") : name;

  File root = SD.open(AUDIO_ROOT.c_str());
  if (!root) {
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"failed to open audio root\"}");
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"audio root not a directory\"}");
    return;
  }

  String foundFolderName = "";

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String folderName = basenameOf(entry.name());

      if (looksLikeUidOnly) {
        if (folderName.startsWith(prefix)) {
          foundFolderName = folderName;
          entry.close();
          break;
        }
      } else {
        if (folderName == prefix) {
          foundFolderName = folderName;
          entry.close();
          break;
        }
      }
    }

    entry.close();
    entry = root.openNextFile();
  }

  root.close();

  if (foundFolderName.length() == 0) {
    server.send(404, "application/json; charset=utf-8", "{\"error\":\"folder not found\"}");
    return;
  }

  String uid, desc;
  splitFolderName(foundFolderName, uid, desc);

  bool isLast = (lastScannedUid.length() > 0 && uid == lastScannedUid);

  String folderPath = AUDIO_ROOT;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += foundFolderName;

  String body;
  body.reserve(2048);
  body += "{";
  body += "\"name\":\"" + jsonEscape(foundFolderName) + "\",";
  body += "\"uid\":\"" + jsonEscape(uid) + "\",";
  body += "\"description\":\"" + jsonEscape(desc) + "\",";
  String files[MAX_FILES];
  int fileCount = collectMp3Files(folderPath, files, MAX_FILES);
  sortStrings(files, fileCount);

  body += "\"files\":";
  appendStringArrayJson(body, files, fileCount);
  body += "}";

  server.send(200, "application/json; charset=utf-8", body);
}


void webSetup() {
  webEnabled = false;
}

void webEnable() {
  if (webEnabled) return;

  Serial.println("Enabling web mode");

  if (!connectWifi()) return;

  server.on("/api/index.json",  HTTP_GET, handleIndexJson);
  server.on("/api/folder.json", HTTP_GET, handleFolderJson);

  // Static file fallback (index.html, style.css, folder.html, etc)
  server.onNotFound(handleNotFound);

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
  if (webEnabled) server.handleClient();
}

bool webIsEnabled() {
  return webEnabled;
}
