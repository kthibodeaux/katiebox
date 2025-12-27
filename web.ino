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

static File g_uploadFile;
static String g_uploadFolder;
static String g_uploadName;
static size_t g_uploadBytes = 0;
static String g_uploadError;

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

static bool isSafeSegment(const String &s) {
  if (s.length() == 0) return false;
  if (s.indexOf("..") >= 0) return false;
  if (s.indexOf('/') >= 0) return false;
  if (s.indexOf('\\') >= 0) return false;
  return true;
}

static String joinPath3(const String &a, const String &b, const String &c) {
  String p = a;
  if (!p.endsWith("/")) p += "/";
  p += b;
  if (!p.endsWith("/")) p += "/";
  p += c;
  return p;
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

static bool isSafeFolderName(const String &name) {
  if (name.length() == 0) return false;
  if (name.indexOf("..") >= 0) return false;
  if (name.indexOf('/') >= 0) return false;
  if (name.indexOf('\\') >= 0) return false;
  return true;
}

static String audioPathForFolderName(const String &folderName) {
  String p = AUDIO_ROOT;
  if (!p.endsWith("/")) p += "/";
  p += folderName;
  return p;
}

static void handleRenameFolder() {
  // Expect: POST current=<old> next=<new>
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json; charset=utf-8", "{\"error\":\"method not allowed\"}");
    return;
  }

  if (!server.hasArg("current") || !server.hasArg("next")) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"missing current/next\"}");
    return;
  }

  String current = server.arg("current");
  String next = server.arg("next");
  current.trim();
  next.trim();

  if (!isSafeFolderName(current) || !isSafeFolderName(next)) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"invalid folder name\"}");
    return;
  }

  String oldPath = audioPathForFolderName(current);
  String newPath = audioPathForFolderName(next);

  if (!SD.exists(oldPath.c_str())) {
    server.send(404, "application/json; charset=utf-8", "{\"error\":\"current folder not found\"}");
    return;
  }

  if (SD.exists(newPath.c_str())) {
    server.send(409, "application/json; charset=utf-8", "{\"error\":\"target already exists\"}");
    return;
  }

  bool ok = SD.rename(oldPath.c_str(), newPath.c_str());
  if (!ok) {
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"rename failed\"}");
    return;
  }

  String body = "{";
  body += "\"ok\":true,";
  body += "\"current\":\"" + jsonEscape(current) + "\",";
  body += "\"next\":\"" + jsonEscape(next) + "\"";
  body += "}";

  server.send(200, "application/json; charset=utf-8", body);
}

static void handleRenameFile() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json; charset=utf-8", "{\"error\":\"method not allowed\"}");
    return;
  }

  if (!server.hasArg("folder") || !server.hasArg("current") || !server.hasArg("next")) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"missing folder/current/next\"}");
    return;
  }

  String folder = server.arg("folder");
  String current = server.arg("current");
  String next = server.arg("next");

  folder.trim();
  current.trim();
  next.trim();

  if (!isSafeSegment(folder) || !isSafeSegment(current) || !isSafeSegment(next)) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"invalid folder or filename\"}");
    return;
  }

  // Optional: require .mp3 extension for both old and new names
  // (If you want to rename non-mp3 files too, delete this block.)
  {
    String lcCur = current; lcCur.toLowerCase();
    String lcNext = next;   lcNext.toLowerCase();
    if (!lcCur.endsWith(".mp3") || !lcNext.endsWith(".mp3")) {
      server.send(400, "application/json; charset=utf-8", "{\"error\":\"filenames must end with .mp3\"}");
      return;
    }
  }

  // Build full paths
  // oldPath = /audio/<folder>/<current>
  // newPath = /audio/<folder>/<next>
  String oldPath = joinPath3(AUDIO_ROOT, folder, current);
  String newPath = joinPath3(AUDIO_ROOT, folder, next);

  if (!SD.exists(oldPath.c_str())) {
    server.send(404, "application/json; charset=utf-8", "{\"error\":\"current file not found\"}");
    return;
  }

  if (SD.exists(newPath.c_str())) {
    server.send(409, "application/json; charset=utf-8", "{\"error\":\"target already exists\"}");
    return;
  }

  bool ok = SD.rename(oldPath.c_str(), newPath.c_str());
  if (!ok) {
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"rename failed\"}");
    return;
  }

  String body;
  body.reserve(256);
  body += "{";
  body += "\"ok\":true,";
  body += "\"folder\":\"" + jsonEscape(folder) + "\",";
  body += "\"current\":\"" + jsonEscape(current) + "\",";
  body += "\"next\":\"" + jsonEscape(next) + "\"";
  body += "}";

  server.send(200, "application/json; charset=utf-8", body);
}

static void handleDeleteFile() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json; charset=utf-8", "{\"error\":\"method not allowed\"}");
    return;
  }

  if (!server.hasArg("folder") || !server.hasArg("name")) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"missing folder/name\"}");
    return;
  }

  String folder = server.arg("folder");
  String name = server.arg("name");
  folder.trim();
  name.trim();

  if (!isSafeSegment(folder) || !isSafeSegment(name)) {
    server.send(400, "application/json; charset=utf-8", "{\"error\":\"invalid folder or filename\"}");
    return;
  }

  {
    String lower = name;
    lower.toLowerCase();
    if (!lower.endsWith(".mp3")) {
      server.send(400, "application/json; charset=utf-8", "{\"error\":\"only .mp3 can be deleted\"}");
      return;
    }
  }

  String path = joinPath3(AUDIO_ROOT, folder, name);

  if (!SD.exists(path.c_str())) {
    server.send(404, "application/json; charset=utf-8", "{\"error\":\"file not found\"}");
    return;
  }

  bool ok = SD.remove(path.c_str());
  if (!ok) {
    server.send(500, "application/json; charset=utf-8", "{\"error\":\"delete failed\"}");
    return;
  }

  String body;
  body.reserve(256);
  body += "{";
  body += "\"ok\":true,";
  body += "\"folder\":\"" + jsonEscape(folder) + "\",";
  body += "\"name\":\"" + jsonEscape(name) + "\"";
  body += "}";

  server.send(200, "application/json; charset=utf-8", body);
}

static void handleUploadMp3Upload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    g_uploadError = "";
    g_uploadBytes = 0;
    g_uploadFile = File();
    g_uploadFolder = "";
    g_uploadName = "";

    if (!server.hasArg("folder")) {
      g_uploadError = "missing folder";
      return;
    }

    g_uploadFolder = server.arg("folder");
    g_uploadFolder.trim();

    if (!isSafeSegment(g_uploadFolder)) {
      g_uploadError = "invalid folder";
      return;
    }

    String fname = upload.filename;
    int slash = fname.lastIndexOf('/');
    if (slash >= 0) fname = fname.substring(slash + 1);
    slash = fname.lastIndexOf('\\');
    if (slash >= 0) fname = fname.substring(slash + 1);

    fname.trim();

    if (!isSafeSegment(fname)) {
      g_uploadError = "invalid filename";
      return;
    }
    if (!endsWithMp3(fname)) {
      g_uploadError = "file must end with .mp3";
      return;
    }

    String folderPath = AUDIO_ROOT;
    if (!folderPath.endsWith("/")) folderPath += "/";
    folderPath += g_uploadFolder;

    if (!SD.exists(folderPath.c_str())) {
      g_uploadError = "folder not found";
      return;
    }

    String fullPath = joinPath3(AUDIO_ROOT, g_uploadFolder, fname);

    if (SD.exists(fullPath.c_str())) {
      g_uploadError = "file already exists";
      return;
    }

    g_uploadFile = SD.open(fullPath.c_str(), FILE_WRITE);
    if (!g_uploadFile) {
      g_uploadError = "failed to open file for writing";
      return;
    }

    g_uploadName = fname;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (g_uploadError.length() > 0) return;
    if (!g_uploadFile) {
      g_uploadError = "no open file";
      return;
    }

    size_t written = g_uploadFile.write(upload.buf, upload.currentSize);
    g_uploadBytes += written;

    if (written != upload.currentSize) {
      g_uploadError = "short write";
      return;
    }

    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (g_uploadFile) g_uploadFile.close();
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (g_uploadFile) g_uploadFile.close();
    g_uploadError = "upload aborted";
    return;
  }
}

static void handleUploadMp3Done() {
  if (g_uploadError.length() > 0) {
    String body = "{\"error\":\"" + jsonEscape(g_uploadError) + "\"}";
    server.send(400, "application/json; charset=utf-8", body);
    return;
  }

  String body;
  body.reserve(256);
  body += "{";
  body += "\"ok\":true,";
  body += "\"folder\":\"" + jsonEscape(g_uploadFolder) + "\",";
  body += "\"name\":\"" + jsonEscape(g_uploadName) + "\",";
  body += "\"bytes\":";
  body += String((unsigned long)g_uploadBytes);
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
  server.on("/api/rename_folder.json", HTTP_POST, handleRenameFolder);
  server.on("/api/rename_file.json",   HTTP_POST, handleRenameFile);
  server.on("/api/delete_file.json", HTTP_POST, handleDeleteFile);
  server.on("/api/upload_mp3", HTTP_POST, handleUploadMp3Done, handleUploadMp3Upload);

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
