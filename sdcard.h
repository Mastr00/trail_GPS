#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>

// Pins SPI dédiés pour la carte SD
#define SD_CS   5
#define SD_MOSI 12
#define SD_MISO 47
#define SD_CLK  13

// Initialise le bus SPI SD et monte la carte
// Retourne true si la carte est détectée et montée
bool initSD();

// Retourne true si la SD est disponible
bool sdOK();

#endif
