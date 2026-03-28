#include <Arduino.h>
#include "FS.h"
#include <SPI.h>
#include <SD.h>
#include "sdcard.h"

// ─────────────────────────────────────────────────────────────
// SPI2 (HSPI) dédié pour la carte SD
// SCK=13, MISO=47, MOSI=12, CS=5
// ─────────────────────────────────────────────────────────────
static SPIClass spi_sd(HSPI);
static bool _sdOK = false;

bool initSD() {
  // Forcer CS haut avant tout (stabilité du bus)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Initialiser le bus SPI2 avec les pins dédiés SD
  spi_sd.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  delay(100);
  Serial.println("[SD] Init SPI2 : SCK=13 MISO=47 MOSI=12 CS=5");

  // Montage simple à 10 MHz (approche stable du code fonctionnel)
  if (!SD.begin(SD_CS, spi_sd, 10000000)) {
    Serial.println("[SD] ECHEC montage carte SD !");
    Serial.println("[SD] Verifiez : CS=5, MOSI=12, CLK=13, MISO=47");
    _sdOK = false;
    return false;
  }

  // Vérifier que la carte est bien présente
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] Aucune carte detectee (CARD_NONE)");
    _sdOK = false;
    return false;
  }

  // Succès — afficher les infos
  Serial.println("[SD] Carte SD initialisee avec succes !");
  Serial.print("[SD] Type : ");
  if      (cardType == CARD_MMC)  Serial.println("MMC");
  else if (cardType == CARD_SD)   Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else                            Serial.println("Inconnue");

  uint64_t sz = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[SD] Taille: %llu MB\n", sz);

  _sdOK = true;
  return true;
}

bool sdOK() {
  return _sdOK;
}

