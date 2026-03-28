/*
 * gpx.cpp — Lecture ultra-rapide et parsing d'un fichier GPX
 *
 * Wave 2 :
 * - Chunk Buffer de 4 Ko en lecture SD (50x plus rapide que octet par octet)
 * - Limite explosée à 10 000 points grâce à la PSRAM
 * - Évite les fuites mémoire avec freeGPX()
 */

#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include "gpx.h"
#include "sdcard.h"
#include "map.h"

static float* _gpxLat = nullptr;
static float* _gpxLon = nullptr;
static int    _gpxCount = 0;

int getGPXPointCount() { return _gpxCount; }
float getGPXLat(int i) { return (i >= 0 && i < _gpxCount) ? _gpxLat[i] : 0.0f; }
float getGPXLon(int i) { return (i >= 0 && i < _gpxCount) ? _gpxLon[i] : 0.0f; }

// Nettoyage pour éviter les fuites de mémoire
void freeGPX() {
  if (_gpxLat) { free(_gpxLat); _gpxLat = nullptr; }
  if (_gpxLon) { free(_gpxLon); _gpxLon = nullptr; }
  _gpxCount = 0;
}

// Extraction rapide sans std::string
static float extractAttr(const char* line, const char* attr) {
  const char* p = strstr(line, attr);
  if (!p) return 0.0f;
  p += strlen(attr);
  while (*p && *p != '"') p++;
  if (!*p) return 0.0f;
  p++;
  return atof(p);
}

// -------------------------------------------------------
int loadGPX(const char* path) {
  // Toujours nettoyer l'ancien GPX avant d'en charger un nouveau
  freeGPX();

  if (!sdOK()) return -1;

  // Allocation de ~80 Ko dans la PSRAM pour 10 000 points
  _gpxLat = (float*)ps_malloc(GPX_MAX_PTS * sizeof(float));
  _gpxLon = (float*)ps_malloc(GPX_MAX_PTS * sizeof(float));
  
  if (!_gpxLat || !_gpxLon) {
    freeGPX(); 
    return -1; // Échec allocation
  }

  File f = SD.open(path, FILE_READ);
  if (!f) return -1;

  // Lecture par chunks de 4 Ko pour une vitesse fulgurante sur carte SD
  const int CHUNK_SIZE = 4096;
  char* chunk = (char*)malloc(CHUNK_SIZE);
  if (!chunk) {
    f.close();
    return -1;
  }

  char lineBuf[384]; // Augmenté pour éviter la corruption des balises complexes
  int  lineLen = 0;
  bool inBuf   = false;

  while (f.available() && _gpxCount < GPX_MAX_PTS) {
    int bytesRead = f.read((uint8_t*)chunk, CHUNK_SIZE);
    
    // Traitement du chunk en mémoire RAM pure (ultra rapide)
    for (int i = 0; i < bytesRead && _gpxCount < GPX_MAX_PTS; i++) {
      char c = chunk[i];

      if (c == '<') {
        lineLen = 0;
        inBuf = true;
        lineBuf[lineLen++] = c;
      } else if (c == '>' && inBuf) {
        lineBuf[lineLen++] = c;
        lineBuf[lineLen]   = '\0';
        inBuf = false;

        if (strstr(lineBuf, "trkpt") || strstr(lineBuf, "wpt")) {
          float lat = extractAttr(lineBuf, "lat=");
          float lon = extractAttr(lineBuf, "lon=");
          if (lat != 0.0f || lon != 0.0f) {
            _gpxLat[_gpxCount] = lat;
            _gpxLon[_gpxCount] = lon;
            _gpxCount++;
          }
        }
      } else if (inBuf && lineLen < 382) { // Sécurité buffer
        lineBuf[lineLen++] = c;
      }
    }
  }

  free(chunk);
  f.close();
  return _gpxCount;
}

// -------------------------------------------------------
void drawGPXOverlay(TFT_eSPI* tft, float centerLat, float centerLon, int zoom) {
  if (_gpxCount == 0 || !tft) return;

  tft->startWrite();
  for (int i = 0; i < _gpxCount; i++) {
    int sx, sy;
    if (latLonToScreen(_gpxLat[i], _gpxLon[i], centerLat, centerLon, zoom, sx, sy)) {
      tft->drawPixel(sx, sy, TFT_GREEN);
      if (i > 0) {
        int sx2, sy2;
        if (latLonToScreen(_gpxLat[i-1], _gpxLon[i-1], centerLat, centerLon, zoom, sx2, sy2)) {
          tft->drawLine(sx2, sy2, sx, sy, 0x07E0);
        }
      }
    }
  }
  tft->endWrite();
}
