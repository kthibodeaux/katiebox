#include "Audio.h"

int PIN_I2S_BCLK = 4;
int PIN_I2S_LRCK = 5;
int PIN_I2S_DOUT = 6;

Audio audio;
bool isPlaying = false;
int volumeLevel = 10;

constexpr int MAX_TRACKS = 64;
String playlist[MAX_TRACKS];
int playlistCount = 0;
int playlistIndex = 0;
String currentFolder = "";
bool trackEnded = false;
int maxVolume = 18;

void onAudioEvent(Audio::msg_t m);
void onAudioEvent(Audio::msg_t m) {
  if (m.e == Audio::evt_eof) {
    trackEnded = true;
  }
}

void increaseVolume() {
  if (volumeLevel < maxVolume) {
    volumeLevel++;
    audio.setVolume(volumeLevel);
    Serial.print("Volume: ");
    Serial.println(volumeLevel);
  }
}

void decreaseVolume() {
  if (volumeLevel > 1) {
    volumeLevel--;
    audio.setVolume(volumeLevel);
    Serial.print("Volume: ");
    Serial.println(volumeLevel);
  }
}

void clearPlaylist() {
  for (int i = 0; i < playlistCount; i++) {
    playlist[i] = "";
  }
  playlistCount = 0;
  playlistIndex = 0;
  currentFolder = "";
}

void sortPlaylist() {
  sortStrings(playlist, playlistCount);
}

bool buildPlaylistForFolder(const String &folderPath) {
  clearPlaylist();
  currentFolder = folderPath;

  File dir = SD.open(folderPath.c_str());
  if (!dir) {
    Serial.print("Failed to open folder: ");
    Serial.println(folderPath);
    return false;
  }
  if (!dir.isDirectory()) {
    Serial.print("Not a directory: ");
    Serial.println(folderPath);
    dir.close();
    return false;
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (name.startsWith(folderPath)) {
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
      }

      if (endsWithMp3(name)) {
        if (playlistCount < MAX_TRACKS) {
          playlist[playlistCount++] = name;
        } else {
          Serial.println("Playlist full; ignoring extra files");
          break;
        }
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }

  dir.close();

  if (playlistCount == 0) {
    Serial.print("No .mp3 files found in ");
    Serial.println(folderPath);
    return false;
  }

  sortPlaylist();

  Serial.print("Playlist built (");
  Serial.print(playlistCount);
  Serial.println(" tracks):");
  for (int i = 0; i < playlistCount; i++) {
    Serial.print("  ");
    Serial.println(playlist[i]);
  }

  return true;
}

void stopPlayback() {
  if (!isPlaying) return;
  Serial.println("Stopping playback...");
  audio.stopSong();
  isPlaying = false;
}

void playNextTrack() {
  trackEnded = false;
  isPlaying = false;

  playlistIndex++;
  if (playlistIndex >= playlistCount) {
    Serial.println("Folder finished (end of playlist)");
    Serial.println("Restarting playlist...");
    playlistIndex = 0;
    playCurrentTrack();
  } else {
    Serial.println("Next track...");
    playCurrentTrack();
  }
}

void startFolderPlaybackForUid(const String &uidHex) {
  const String folder = findFolderForUid(uidHex);
  if (folder == "") {
    Serial.print("No folder found for UID prefix: ");
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

void playCurrentTrack() {
  if (playlistCount == 0) return;
  if (playlistIndex < 0 || playlistIndex >= playlistCount) return;

  const String path = currentFolder + "/" + playlist[playlistIndex];

  if (!SD.exists(path.c_str())) {
    Serial.print("Missing file: ");
    Serial.println(path);
    isPlaying = false;
    return;
  }

  Serial.print("Playing: ");
  Serial.println(path);

  audio.connecttoFS(SD, path.c_str());
  isPlaying = true;
}

void audioSetup() {
  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
  audio.setVolume(volumeLevel);

  Audio::audio_info_callback = onAudioEvent;
}

void audioLoop() {
  audio.loop();

  if (trackEnded) {
    playNextTrack();
  }
}
