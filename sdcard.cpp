#include <Arduino.h>
#include "FS.h"
#include <SPI.h>
#include <SD.h>
#include "sdcard.h"

// ─────────────────────────────────────────────────────────────
// Bus SPI dédié pour la carte SD
// SCK=13, MISO=47, MOSI=12, CS=5
//
// Sur ESP32-S3, les bus utilisateur sont indexés 0 et 1 :
//   0 → SPI2 (FSPI, utilisé par TFT_eSPI)
//   1 → SPI3 (second bus libre)
// La macro HSPI vaut 2 dans certaines versions du core Arduino,
// ce qui est hors limites et fait échouer begin() silencieusement.
// On utilise donc le numéro de bus explicite.
// ─────────────────────────────────────────────────────────────
#if CONFIG_IDF_TARGET_ESP32S3
  #define SD_SPI_BUS 1   // SPI3 sur ESP32-S3 (0-indexed)
#else
  #define SD_SPI_BUS HSPI // Original ESP32
#endif

static SPIClass spi_sd(SD_SPI_BUS);
static bool _sdOK = false;

bool initSD() {
  Serial.println("[SD] --- initSD() ---");
  Serial.printf("[SD] SD_SPI_BUS=%d (HSPI=%d)\n", SD_SPI_BUS, HSPI);

  // Terminer proprement toute session SD précédente (indispensable pour la ré-init)
  SD.end();
  spi_sd.end();
  delay(50);

  // Forcer CS haut avant tout (stabilité du bus)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Initialiser le bus SPI avec les pins dédiés SD
  spi_sd.begin(SD_CLK, SD_MISO, SD_MOSI, -1);

  // Envoyer 80 impulsions d'horloge avec CS haut pour réveiller la carte
  // (protocole SD : nécessaire avant le premier CMD0)
  spi_sd.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  digitalWrite(SD_CS, HIGH);
  for (int i = 0; i < 10; i++) spi_sd.transfer(0xFF);
  spi_sd.endTransaction();
  delay(50);

  Serial.printf("[SD] Bus SPI3 pret : SCK=%d MISO=%d MOSI=%d CS=%d\n",
                SD_CLK, SD_MISO, SD_MOSI, SD_CS);

  // Montage à 4 MHz — plus robuste que 10 MHz sur câbles longs ou modules SD bas de gamme
  Serial.println("[SD] Tentative SD.begin() @ 4 MHz...");
  if (!SD.begin(SD_CS, spi_sd, 4000000)) {
    Serial.println("[SD] ECHEC @ 4 MHz — SD.begin() a retourne false");
    Serial.println("[SD] Causes possibles :");
    Serial.println("[SD]  1) Mauvais cablage (verifier CS=5 MOSI=12 CLK=13 MISO=47)");
    Serial.println("[SD]  2) Carte SD absente ou non formatee FAT32");
    Serial.println("[SD]  3) Tension insuffisante (verifier 3.3V sur le module SD)");
    Serial.println("[SD]  4) Resistance pull-up manquante sur MISO (10k vers 3.3V)");
    _sdOK = false;
    return false;
  }
  Serial.println("[SD] SD.begin() OK");

  // Vérifier que la carte est bien présente
  uint8_t cardType = SD.cardType();
  Serial.printf("[SD] cardType() = %d\n", cardType);

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

// Signale que la carte SD est inaccessible (ex. : SD.open() a échoué)
// Permet à la boucle principale de déclencher une ré-initialisation.
void sdInvalidate() {
  _sdOK = false;
}
