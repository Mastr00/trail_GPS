/*
 * map.cpp — Affichage de tuiles OpenStreetMap depuis la SD
 *
 * Rendu multi-tuiles : charge jusqu'à 2×2 tuiles adjacentes pour que
 * l'écran 240×320 soit toujours entièrement rempli, même quand la
 * position GPS est au bord d'une tuile.
 *
 * Cache LRU de 4 tuiles en PSRAM (4 × 128 Ko = 512 Ko sur 8 Mo).
 *
 * Format des tuiles : /tiles/<zoom>/<tileX>/<tileY>.bmp
 * BMP 16-bit RGB565, 256×256 pixels (standard OSM)
 */

#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include "map.h"
#include "sdcard.h"

// Taille standard d'une tuile OSM en pixels
#define TILE_SIZE     256
#define TILE_SHIFT    8       // log2(256) pour division/modulo rapide par shift

// Zone d'affichage carte sur l'écran (en dessous du header de 32px)
#define MAP_DRAW_W    240
#define MAP_DRAW_H    264
#define MAP_DRAW_X    0
#define MAP_DRAW_Y    32

// ── Cache de 4 tuiles en PSRAM ──────────────────────────────
#define TILE_CACHE_SIZE 4

struct TileSlot {
  int      zoom, tx, ty;
  uint16_t* buffer;
  bool     valid;
  uint32_t lastUsed;
};

static TileSlot  _cache[TILE_CACHE_SIZE];
static uint32_t  _cacheCounter = 0;
static uint16_t  lineBuffer[MAP_DRAW_W];
static TFT_eSPI* _tft = nullptr;

// -------------------------------------------------------
void initMap(TFT_eSPI* tft) {
  _tft = tft;
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    if (!_cache[i].buffer) {
      _cache[i].buffer = (uint16_t*)ps_malloc(TILE_SIZE * TILE_SIZE * sizeof(uint16_t));
    }
    _cache[i].valid = false;
    _cache[i].lastUsed = 0;
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

  int dxGlobal = (pTX - cTX) * TILE_SIZE + (pPX - cPX);
  int dyGlobal = (pTY - cTY) * TILE_SIZE + (pPY - cPY);

  sx = MAP_DRAW_W / 2 + dxGlobal;
  sy = MAP_DRAW_Y + MAP_DRAW_H / 2 + dyGlobal;

  return (sx >= 0 && sx < MAP_DRAW_W && sy >= MAP_DRAW_Y && sy < MAP_DRAW_Y + MAP_DRAW_H);
}

// ── Chargement BMP dans un buffer cible ─────────────────────
static bool loadTileBMPTo(int zoom, int tx, int ty, uint16_t* dest) {
  if (!sdOK() || !dest) return false;

  char path[64];
  snprintf(path, sizeof(path), "/tiles/%d/%d/%d.bmp", zoom, tx, ty);

  File f = SD.open(path, FILE_READ);
  if (!f) {
    // Fichier absent = tuile non disponible (pas forcément une erreur SD)
    return false;
  }

  // Lire l'en-tête BMP (54 bytes standard)
  uint8_t header[54];
  if (f.read(header, 54) < 54) { f.close(); return false; }

  if (header[0] != 'B' || header[1] != 'M') { f.close(); return false; }

  uint32_t dataOffset = header[10] | (header[11] << 8) |
                        (header[12] << 16) | (header[13] << 24);
  int32_t imgW = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  int32_t imgH = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
  uint16_t bpp  = header[28] | (header[29] << 8);
  uint32_t comp = header[30] | (header[31] << 8) | (header[32] << 16) | (header[33] << 24);

  if (bpp != 16 || imgW != TILE_SIZE || abs(imgH) != TILE_SIZE || (comp != 0 && comp != 3)) {
    f.close();
    return false;
  }

  f.seek(dataOffset);

  bool bottomUp = (imgH > 0);
  int rows = abs(imgH);
  int rowStride = (imgW * 2 + 3) & ~3;
  static uint8_t rowBuf[TILE_SIZE * 2 + 4];

  if (bottomUp) {
    for (int row = rows - 1; row >= 0; row--) {
      f.read(rowBuf, rowStride);
      for (int col = 0; col < imgW; col++) {
        uint16_t px = rowBuf[col * 2] | (rowBuf[col * 2 + 1] << 8);
        dest[row * TILE_SIZE + col] = (px >> 8) | (px << 8);
      }
    }
  } else {
    for (int row = 0; row < rows; row++) {
      f.read(rowBuf, rowStride);
      for (int col = 0; col < imgW; col++) {
        uint16_t px = rowBuf[col * 2] | (rowBuf[col * 2 + 1] << 8);
        dest[row * TILE_SIZE + col] = (px >> 8) | (px << 8);
      }
    }
  }

  f.close();
  return true;
}

// ── Cache : récupérer une tuile ─────────────────────────────
// Retourne le buffer de la tuile, ou nullptr si introuvable sur la SD.
static uint16_t* getTile(int zoom, int tx, int ty) {
  // Chercher dans le cache
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    if (_cache[i].valid && _cache[i].zoom == zoom &&
        _cache[i].tx == tx && _cache[i].ty == ty) {
      _cache[i].lastUsed = ++_cacheCounter;
      return _cache[i].buffer;
    }
  }

  // Trouver un slot libre ou le LRU (least recently used)
  int lru = 0;
  for (int i = 0; i < TILE_CACHE_SIZE; i++) {
    if (!_cache[i].valid) { lru = i; break; }
    if (_cache[i].lastUsed < _cache[lru].lastUsed) lru = i;
  }

  // Charger la tuile depuis la SD
  if (loadTileBMPTo(zoom, tx, ty, _cache[lru].buffer)) {
    _cache[lru].zoom = zoom;
    _cache[lru].tx   = tx;
    _cache[lru].ty   = ty;
    _cache[lru].valid = true;
    _cache[lru].lastUsed = ++_cacheCounter;
    return _cache[lru].buffer;
  }

  return nullptr;
}

// ── Rendu carte multi-tuiles ────────────────────────────────
void drawMap(float lat, float lon, int zoom) {
  if (!_tft) return;

  // Calculer la position globale en pixels (Web Mercator)
  float n = pow(2.0f, zoom);
  float xFrac  = (lon + 180.0f) / 360.0f * n;
  float latRad = lat * DEG_TO_RAD;
  float yFrac  = (1.0f - log(tan(latRad) + 1.0f / cos(latRad)) / M_PI) / 2.0f * n;

  int centerGX = (int)(xFrac * TILE_SIZE);
  int centerGY = (int)(yFrac * TILE_SIZE);

  // Fenêtre d'affichage en coordonnées globales
  int startGX = centerGX - MAP_DRAW_W / 2;
  int startGY = centerGY - MAP_DRAW_H / 2;

  // Rendu ligne par ligne, chaque ligne peut traverser 2 tuiles max
  _tft->startWrite();
  _tft->setAddrWindow(MAP_DRAW_X, MAP_DRAW_Y, MAP_DRAW_W, MAP_DRAW_H);

  for (int row = 0; row < MAP_DRAW_H; row++) {
    int gy = startGY + row;
    int ty = gy >> TILE_SHIFT;          // floor(gy / 256) via arithmetic shift
    int py = gy - (ty << TILE_SHIFT);   // gy mod 256 (correct pour négatifs)

    int col = 0;
    while (col < MAP_DRAW_W) {
      int gx = startGX + col;
      int tx = gx >> TILE_SHIFT;
      int px = gx - (tx << TILE_SHIFT);

      // Pixels consécutifs restant dans la même tuile
      int remaining = TILE_SIZE - px;
      int count = (remaining < MAP_DRAW_W - col) ? remaining : (MAP_DRAW_W - col);

      uint16_t* tile = getTile(zoom, tx, ty);
      if (tile) {
        memcpy(&lineBuffer[col], &tile[py * TILE_SIZE + px], count * sizeof(uint16_t));
      } else {
        // Tuile absente : gris foncé
        for (int i = 0; i < count; i++) lineBuffer[col + i] = 0x4208;
      }

      col += count;
    }

    _tft->pushColors(lineBuffer, MAP_DRAW_W, false);
  }

  _tft->endWrite();

  // Crosshair GPS au centre de l'écran
  int cx = MAP_DRAW_X + MAP_DRAW_W / 2;
  int cy = MAP_DRAW_Y + MAP_DRAW_H / 2;
  _tft->drawLine(cx - 8, cy, cx + 8, cy, TFT_RED);
  _tft->drawLine(cx, cy - 8, cx, cy + 8, TFT_RED);
  _tft->drawCircle(cx, cy, 4, TFT_RED);
}
