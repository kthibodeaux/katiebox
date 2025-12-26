#include <EasyButton.h>

int PIN_BUTTON_VOL_DOWN = 16;
int PIN_BUTTON_VOL_UP = 17;
int PIN_BUTTON_NEXT_SONG = 18;

EasyButton buttonVolDown(PIN_BUTTON_VOL_DOWN);
EasyButton buttonVolUp(PIN_BUTTON_VOL_UP);
EasyButton buttonNextSong(PIN_BUTTON_NEXT_SONG);

void buttonSetup() {
  pinMode(PIN_BUTTON_VOL_DOWN, INPUT);
  buttonVolDown.begin();
  buttonVolDown.onPressed(decreaseVolume);

  pinMode(PIN_BUTTON_VOL_UP, INPUT);
  buttonVolUp.begin();
  buttonVolUp.onPressed(increaseVolume);

  pinMode(PIN_BUTTON_NEXT_SONG, INPUT);
  buttonNextSong.begin();
  buttonNextSong.onPressed(playNextTrack);
}

void buttonLoop() {
  buttonVolDown.read();
  buttonVolUp.read();
  buttonNextSong.read();
}
