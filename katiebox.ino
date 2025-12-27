#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  spiSetup();
  filesSetup();
  configSetup();
  webSetup();
  buttonSetup();
  rfidSetup();
  audioSetup();
  Serial.println("Ready to play!");
}

void loop() {
  if (webIsEnabled) {
    webLoop();
  } else {
    buttonLoop();
    audioLoop();
  }

  rfidLoop();
}
