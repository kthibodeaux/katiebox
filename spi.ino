#include <SPI.h>

int PIN_SPI_SCK = 12;
int PIN_SPI_MOSI = 11;
int PIN_SPI_MISO = 13;

void spiSetup() {
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
}
