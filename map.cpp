/*
 * map.cpp — Affichage de tuiles OpenStreetMap depuis la SD
 *
 * Format des tuiles attendu : /tiles/<zoom>/<tileX>/<tileY>.bmp
 * BMP 16-bit RGB565, 256×256 pixels (standard OSM)
 *
 * L'écran fait 128×160 px → on affiche une fenêtre de 128×128 centrée
 * dans la tuile, plus 32 px verticaux pour le header + infos.
 *
 * Grâce aux 8 Mo de PSRAM du N16R8, on alloue un buffer de tuile complet.
 */

#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include "map.h"
#include "sdcard.h"

// Taille standard d'une tuile OSM en pixels
#define TILE_SIZE 256

// Zone d'affichage carte sur l'écran (240 large, 264 haut, en bas du header)
#define MAP_DRAW_W  240
#define MAP_DRAW_H  264
#define MAP_DRAW_X  0
#define MAP_DRAW_Y  32   // en dessous du header de 32px

// Buffer tuile en PSRAM (256×256 × 2 bytes = 131072 bytes)
static uint16_t* tileBuffer  = nullptr;
// Buffer d'une ligne pour pushColors (128 pixels x 2 bytes)
static uint16_t  lineBuffer[256];
static TFT_eSPI* _tft = nullptr;

// -------------------------------------------------------
// Coordonnées de la tuile actuellement en buffer
static int _cachedZoom = -1;
static int _cachedTX   = -1;
static int _cachedTY   = -1;

// -------------------------------------------------------
void initMap(TFT_eSPI* tft) {
  _tft = tft;
  // Allouer le buffer tuile en PSRAM
  if (tileBuffer == nullptr) {
    tileBuffer = (uint16_t*)ps_malloc(TILE_SIZE * TILE_SIZE * sizeof(uint16_t));
  }
}

// -------------------------------------------------------
// Conversion lat/lon → numéro de tuile XYZ (Web Mercator)
void latLonToTile(float lat, float lon, int zoom, int &tileX, int &tileY) {
  float n = pow(2.0f, zoom);
  tileX = (int)((lon + 180.0f) / 360.0f * n);
  float latRad = lat * DEG_TO_RAD;
  tileY = (int)((1.0f - log(tan(latRad) + 1.0f / cos(latRad)) / M_PI) / 2.0f * n);
}

// -------------------------------------------------------
// Pixel offset dans la tuile (0..255)
void latLonToPixelOffset(float lat, float lon, int zoom,
                          int tileX, int tileY,
                          int &px, int &py) {
  float n = pow(2.0f, zoom);

  float xFrac = (lon + 180.0f) / 360.0f * n;
  float latRad = lat * DEG_TO_RAD;
  float yFrac  = (1.0f - log(tan(latRad) + 1.0f / cos(latRad)) / M_PI) / 2.0f * n;

  px = (int)((xFrac - tileX) * TILE_SIZE);
  py = (int)((yFrac - tileY) * TILE_SIZE);

  // Clamp
  if (px < 0) px = 0;
  if (py < 0) py = 0;
  if (px >= TILE_SIZE) px = TILE_SIZE - 1;
  if (py >= TILE_SIZE) py = TILE_SIZE - 1;
}

// -------------------------------------------------------
// Projette un point géo sur l'écran par rapport au centre affiché
bool latLonToScreen(float lat, float lon,
                    float centerLat, float centerLon,
                    int zoom, int &sx, int &sy) {
  int cTX, cTY, cPX, cPY, pTX, pTY, pPX, pPY;

  latLonToTile(centerLat, centerLon, zoom, cTX, cTY);
  latLonToPixelOffset(centerLat, centerLon, zoom, cTX, cTY, cPX, cPY);

  latLonToTile(lat, lon, zoom, pTX, pTY);
  latLonToPixelOffset(lat, lon, zoom, pTX, pTY, pPX, pPY);

  // Différence en pixels globaux
  int dxGlobal = (pTX - cTX) * TILE_SIZE + (pPX - cPX);
  int dyGlobal = (pTY - cTY) * TILE_SIZE + (pPY - cPY);

  // Centré sur le milieu de la zone d'affichage
  sx = MAP_DRAW_W / 2 + dxGlobal;
  sy = MAP_DRAW_Y + MAP_DRAW_H / 2 + dyGlobal;

  return (sx >= 0 && sx < MAP_DRAW_W && sy >= MAP_DRAW_Y && sy < MAP_DRAW_Y + MAP_DRAW_H);
}

// -------------------------------------------------------
// Charge un fichier BMP 16-bit depuis la SD dans tileBuffer
// Retourne true si succès
static bool loadTileBMP(int zoom, int tx, int ty) {
  if (_cachedZoom == zoom && _cachedTX == tx && _cachedTY == ty) {
    return true;  // déjà en cache
  }

  if (!sdOK() || tileBuffer == nullptr) return false;

  // Construire le chemin : /tiles/<zoom>/<tx>/<ty>.bmp
  char path[64];
  snprintf(path, sizeof(path), "/tiles/%d/%d/%d.bmp", zoom, tx, ty);

  File f = SD.open(path, FILE_READ);
  if (!f) {
    // Signaler la défaillance SD pour que loop() déclenche la ré-init
    // au prochain cycle (évite un reinit SPI en plein milieu du rendu).
    sdInvalidate();
    return false;
  }

  // Lire l'en-tête BMP (54 bytes standard)
  uint8_t header[54];
  if (f.read(header, 54) < 54) { f.close(); return false; }

  // Vérification signature 'BM'
  if (header[0] != 'B' || header[1] != 'M') { f.close(); return false; }

  // Offset données pixel
  uint32_t dataOffset = header[10] | (header[11] << 8) |
                        (header[12] << 16) | (header[13] << 24);
  // Dimensions
  int32_t imgW = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  int32_t imgH = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
  uint16_t bpp  = header[28] | (header[29] << 8);
  // Compression : 0=BI_RGB, 3=BI_BITFIELDS (seuls les deux sont acceptés en 16-bit)
  uint32_t comp = header[30] | (header[31] << 8) | (header[32] << 16) | (header[33] << 24);

  if (bpp != 16 || imgW != TILE_SIZE || abs(imgH) != TILE_SIZE || (comp != 0 && comp != 3)) {
    f.close();
    return false;  // format non supporté (BMP 16-bit RGB565 uniquement)
  }

  // Aller à l'offset données
  f.seek(dataOffset);

  // BMP stocke les lignes de bas en haut (imgH positif = bottom-up)
  bool bottomUp = (imgH > 0);
  int rows = abs(imgH);
  int rowStride = (imgW * 2 + 3) & ~3;
  // static → évite d'allouer 516 bytes sur la stack (risque stack overflow ESP32)
  static uint8_t rowBuf[TILE_SIZE * 2 + 4];

  if (bottomUp) {
    for (int row = rows - 1; row >= 0; row--) {
      f.read(rowBuf, rowStride);
      for (int col = 0; col < imgW; col++) {
        uint16_t px = rowBuf[col * 2] | (rowBuf[col * 2 + 1] << 8);
        // WAVE 2 OPTIMIZATION: Endianness swap AT LOAD TIME (Once!)
        // Au lieu de le faire 10 fois par seconde dans la boucle de rendu
        tileBuffer[row * TILE_SIZE + col] = (px >> 8) | (px << 8);
      }
    }
  } else {
    for (int row = 0; row < rows; row++) {
      f.read(rowBuf, rowStride);
      for (int col = 0; col < imgW; col++) {
        uint16_t px = rowBuf[col * 2] | (rowBuf[col * 2 + 1] << 8);
        // WAVE 2 OPTIMIZATION: Endianness swap AT LOAD TIME
        tileBuffer[row * TILE_SIZE + col] = (px >> 8) | (px << 8);
      }
    }
  }

  f.close();
  _cachedZoom = zoom;
  _cachedTX = tx;
  _cachedTY = ty;
  return true;
}

// -------------------------------------------------------
void drawMap(float lat, float lon, int zoom) {
  if (_tft == nullptr) return;

  int tileX, tileY;
  latLonToTile(lat, lon, zoom, tileX, tileY);

  int px, py;
  latLonToPixelOffset(lat, lon, zoom, tileX, tileY, px, py);

  // Calcul de la fenêtre à couper dans la tuile
  // On veut centrer (px, py) sous le centre de l'écran (64, 64 relatif)
  int startX = px - MAP_DRAW_W / 2;
  int startY = py - MAP_DRAW_H / 2;

  bool loaded = loadTileBMP(zoom, tileX, tileY);

  if (!loaded) {
    // Pas de tuile : fond gris + info
    _tft->fillRect(MAP_DRAW_X, MAP_DRAW_Y, MAP_DRAW_W, MAP_DRAW_H, 0x2104);
    _tft->setTextColor(0x8410, 0x2104);
    _tft->setTextSize(2);
    _tft->drawCentreString("No tile", MAP_DRAW_X + MAP_DRAW_W / 2, MAP_DRAW_Y + MAP_DRAW_H / 2 - 20, 1);
    char info[32];
    snprintf(info, sizeof(info), "Z%d X%d Y%d", zoom, tileX, tileY);
    _tft->drawCentreString(info, MAP_DRAW_X + MAP_DRAW_W / 2, MAP_DRAW_Y + MAP_DRAW_H / 2 + 10, 1);
  } else {
    // Afficher la portion de la tuile via pushColors (très rapide)
    _tft->startWrite();
    _tft->setAddrWindow(MAP_DRAW_X, MAP_DRAW_Y, MAP_DRAW_W, MAP_DRAW_H);

    for (int row = 0; row < MAP_DRAW_H; row++) {
      int srcRow = startY + row;
      for (int col = 0; col < MAP_DRAW_W; col++) {
        int srcCol = startX + col;
        uint16_t px;
        if (srcRow < 0 || srcRow >= TILE_SIZE || srcCol < 0 || srcCol >= TILE_SIZE) {
          px = 0x4208;  // gris foncé hors-tuile
        } else {
          // WAVE 2 OPTIMIZATION: L'inversion d'octets a déjà été faite au chargement !
          // On passe de 2 opérations bitwise par pixel (16000 pixels = 32000 ops)
          // à une simple lecture mémoire.
          px = tileBuffer[srcRow * TILE_SIZE + srcCol];
        }
        lineBuffer[col] = px;
      }
      // Envoyer toute la ligne en une seule transaction SPI
      _tft->pushColors(lineBuffer, MAP_DRAW_W, false);
    }
    _tft->endWrite();
  }

  // Crosshair GPS (position courante) au centre de l'écran
  int cx = MAP_DRAW_X + MAP_DRAW_W / 2;
  int cy = MAP_DRAW_Y + MAP_DRAW_H / 2;
  _tft->drawLine(cx - 8, cy, cx + 8, cy, TFT_RED);
  _tft->drawLine(cx, cy - 8, cx, cy + 8, TFT_RED);
  _tft->drawCircle(cx, cy, 4, TFT_RED);
}
