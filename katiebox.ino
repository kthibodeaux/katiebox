#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  spiSetup();
  filesSetup();
  buttonSetup();
  rfidSetup();
  audioSetup();
  Serial.println("Ready to play!");
}

void loop() {
  buttonLoop();
  audioLoop();
  rfidLoop();
}
