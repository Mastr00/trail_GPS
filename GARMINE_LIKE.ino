/*
 * GARMINE_LIKE - GPS Device pour ESP32-S3 N16R8
 * Écran : ST7735S 1.8" 128x160 SPI
 * SD    : SPI HSPI — CS=5, MOSI=12, MISO=47, SCK=13
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include <SD.h>    // exposé ici pour SD.cardSize() dans setup()
#include <Preferences.h>

Preferences prefs;

#include "gps.h"
#include "sensors.h"
#include "buttons.h"
#include "sdcard.h"
#include "map.h"
#include "gpx.h"

// Déclaration extern pour la tare de l'IMU (implémentée dans sensors.cpp / sensors.h)
extern void tareIMU();

TFT_eSPI tft = TFT_eSPI();

// Pages
enum Page { PAGE_MENU = 0, PAGE_GPS, PAGE_ENV, PAGE_IMU, PAGE_COMPASS, PAGE_MAP, PAGE_COUNT };
int currentPage = PAGE_MENU;
int menuIndex = 0;
const int MENU_MAX = 5;  // GPS, ENV, IMU, COMPASS, MAP

// ── Palette couleurs 16-bit RGB565 ──────────────────────────────
#define COL_BG        0x0841   // bleu nuit très foncé (pas tout noir)
#define COL_HEADER    0x024A   // bleu marine profond
#define COL_HEADER2   0x034B   // dégradé header léger
#define COL_LABEL     0x7BEF   // gris argenté
#define COL_VALUE     TFT_WHITE
#define COL_UNIT      0xFD20   // orange vif
#define COL_OK        0x07E0   // vert vif
#define COL_WARN      0xFFE0   // jaune
#define COL_ERR       0xF800   // rouge
#define COL_ACCENT    0x03DF   // cyan/turquoise (accent)
#define COL_DIVIDER   0x2945   // ligne séparatrice subtile

// Dimensions écran ST7789 240x320
#define SCREEN_W 240
#define SCREEN_H 320
#define HEADER_H 32

// Zoom carte
static int mapZoom = MAP_DEFAULT_ZOOM;

// Dernière position GPS affichée sur la carte
static float _lastMapLat = 0;
static float _lastMapLon = 0;
static int   _lastMapZoom = -1;
static bool  _mapNeedsRedraw = true;

// Variables bulle niveau à bulle
static TFT_eSprite imuSprite = TFT_eSprite(&tft);
static bool _imuFirstDraw = true;

// Sprite boussole anti-scintillement
static TFT_eSprite compassSprite = TFT_eSprite(&tft);
static bool _compassFirstDraw = true;

// Flags de premier affichage par page (évite fillScreen au rafraîchissement)
static bool _gpsFirstDraw = true;
static bool _envFirstDraw = true;

// Timers de rafraîchissement
static unsigned long lastUpdate = 0;
static unsigned long lastMenuTime = 0;
static unsigned long lastSDRetry = 0;     // timer auto-reconnexion SD
static unsigned long lastActivity = 0;    // tracking inactivité boutons

// ── Helpers graphiques ───────────────────────────────────────────

// Dessine un header avec dégradé de 2 lignes de hauteur
void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H / 2, COL_HEADER);
  tft.fillRect(0, HEADER_H / 2, SCREEN_W, HEADER_H - HEADER_H / 2, COL_HEADER2);

  tft.setTextColor(COL_ACCENT, COL_HEADER);
  tft.setTextSize(2);
  tft.drawString(title, 8, 8);

  // Indicateur de page en haut à droite
  char pageStr[16];
  snprintf(pageStr, sizeof(pageStr), "%d/%d | SAT:%lu", currentPage, PAGE_COUNT - 1, (unsigned long)getSatellites());
  tft.setTextColor(COL_LABEL, COL_HEADER2);
  tft.drawString(pageStr, SCREEN_W - 90, 8);

  // Ligne de séparation colorée sous le header
  tft.drawFastHLine(0, HEADER_H, SCREEN_W, COL_ACCENT);
}

// Dessine une ligne de données avec label, valeur et unité
// Retourne false si val est NAN (affiche "---")
bool drawRow(int y, const char* label, float val, int decimals, const char* unit) {
  tft.fillRect(0, y, SCREEN_W, 40, COL_BG);

  // Label
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextSize(2);
  tft.drawString(label, 6, y + 4);

  // Ligne de séparation subtile
  tft.drawFastHLine(0, y + 38, SCREEN_W, COL_DIVIDER);

  if (isnan(val)) {
    tft.setTextColor(COL_ERR, COL_BG);
    tft.setTextSize(3);
    tft.drawString("N/A", 6, y + 14);
    return false;
  }

  char buf[16];
  if      (decimals == 0) snprintf(buf, sizeof(buf), "%.0f", val);
  else if (decimals == 1) snprintf(buf, sizeof(buf), "%.1f", val);
  else if (decimals == 2) snprintf(buf, sizeof(buf), "%.2f", val);
  else                    snprintf(buf, sizeof(buf), "%.6f", val);

  tft.setTextColor(COL_VALUE, COL_BG);
  tft.setTextSize(3);
  tft.drawString(buf, 6, y + 14);

  if (unit && unit[0]) {
    tft.setTextColor(COL_UNIT, COL_BG);
    tft.setTextSize(2);
    tft.drawString(unit, 6 + strlen(buf) * 18 + 6, y + 22);
  }
  return true;
}

// ── Petit badge coloré ──────────────────────────────────────────
void drawBadge(int x, int y, const char* text, uint16_t bgColor, uint16_t fgColor) {
  int w = strlen(text) * 12 + 8;
  tft.fillRoundRect(x, y, w, 22, 4, bgColor);
  tft.setTextColor(fgColor, bgColor);
  tft.setTextSize(2);
  tft.drawString(text, x + 4, y + 4);
}

// ── PAGE MENU ───────────────────────────────────────────────────
void drawPageMenu() {
  tft.fillScreen(COL_BG);

  // Header décoratif
  tft.fillRect(0, 0, SCREEN_W, 44, COL_HEADER);
  tft.drawFastHLine(0, 44, SCREEN_W, COL_ACCENT);

  // Logo TrailNav avec accent
  tft.setTextColor(COL_ACCENT, COL_HEADER);
  tft.setTextSize(3);
  tft.drawString("TrailNav", 20, 10);

  // Icônes menu
  const char* items[]  = { " GPS",  " SENSORS", " IMU",     " COMPASS", " MAP" };
  const char* icons[]  = { "\x07",  "\x06",     "\x04",     "\x03",     "\x05" };
  // (les char spéciaux sont remplacés par des préfixes textuels simples)
  const char* labels[] = { "GPS", "SENSORS", "IMU", "COMPASS", "MAP" };

  int y = 60;
  for (int i = 0; i < MENU_MAX; i++) {
    bool selected = (i == menuIndex);

    if (selected) {
      // Fond surligné avec bord accent
      tft.fillRoundRect(8, y - 2, SCREEN_W - 16, 36, 4, COL_ACCENT);
      tft.setTextColor(COL_BG, COL_ACCENT);
    } else {
      tft.setTextColor(COL_LABEL, COL_BG);
    }
    tft.setTextSize(2);

    // Numéro
    char nm[3];
    snprintf(nm, 3, "%d.", i + 1);
    tft.drawString(nm, 20, y + 8);
    tft.drawString(labels[i], 56, y + 8);

    // Indicateur de sélection (flèche à droite)
    if (selected) {
      tft.drawString(">", SCREEN_W - 32, y + 8);
    }

    y += 42;
  }

  // ── Barre de statut en bas ──────────────────────────────────
  tft.drawFastHLine(0, SCREEN_H - 42, SCREEN_W, COL_DIVIDER);

  // SD status badge
  if (sdOK()) {
    drawBadge(4, SCREEN_H - 38, "SD OK", COL_OK, COL_BG);
  } else {
    drawBadge(4, SCREEN_H - 38, "SD OFF", COL_ERR, COL_VALUE);
  }

  // Capteurs status
  if (isAhtOK() && isBmpOK()) {
    drawBadge(88, SCREEN_H - 38, "SENS OK", COL_OK, COL_BG);
  } else {
    drawBadge(88, SCREEN_H - 38, "SENS ERR", COL_ERR, COL_VALUE);
  }

  // ── Heure GPS ──────────────────────────────────────────────
  tft.fillRect(0, SCREEN_H - 18, SCREEN_W, 18, COL_BG);
  tft.setTextSize(2);
  
  bool hasLocFix = (getLatitude() != 0.0f || getLongitude() != 0.0f);
  uint32_t sats = getSatellites();

  // Badge Fix GPS dans la barre des badges
  if (hasLocFix) {
    drawBadge(176, SCREEN_H - 38, "FIX", COL_OK, COL_BG);
  } else {
    drawBadge(176, SCREEN_H - 38, "wait", COL_WARN, COL_BG);
  }

  if (gpsTimeValid()) {
    char timebuf[22];
    snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d  %02d/%02d/%04d",
             getHour(), getMinute(), getSecond(),
             getDay(),  getMonth(),  getYear());
    tft.setTextColor(hasLocFix ? COL_VALUE : COL_WARN, COL_BG);
    tft.drawString(timebuf, 4, SCREEN_H - 16);
  } else {
    // Afficher le nombre de satellites pour diagnostiquer
    char sbuf[24];
    if (sats > 0) {
      snprintf(sbuf, sizeof(sbuf), "GPS: %lu sat, sync...", (unsigned long)sats);
    } else {
      snprintf(sbuf, sizeof(sbuf), "GPS: recherche...");
    }
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString(sbuf, 4, SCREEN_H - 16);
  }
}

// Mise à jour partielle : seulement la ligne d'heure en bas du menu
void updateMenuTime() {
  tft.fillRect(0, SCREEN_H - 18, SCREEN_W, 18, COL_BG);
  
  bool hasLocFix = (getLatitude() != 0.0f || getLongitude() != 0.0f);
  uint32_t sats = getSatellites();

  // Rafraîchir le badge Fix GPS aussi
  if (hasLocFix) {
    drawBadge(176, SCREEN_H - 38, "FIX", COL_OK, COL_BG);
  } else {
    drawBadge(176, SCREEN_H - 38, "wait", COL_WARN, COL_BG);
  }

  tft.setTextSize(2);
  if (gpsTimeValid()) {
    char timebuf[22];
    snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d  %02d/%02d/%04d",
             getHour(), getMinute(), getSecond(),
             getDay(),  getMonth(),  getYear());
    tft.setTextColor(hasLocFix ? COL_VALUE : COL_WARN, COL_BG);
    tft.drawString(timebuf, 4, SCREEN_H - 16);
  } else {
    char sbuf[24];
    if (sats > 0) {
      snprintf(sbuf, sizeof(sbuf), "GPS: %lu sat, sync...", (unsigned long)sats);
    } else {
      snprintf(sbuf, sizeof(sbuf), "GPS: recherche...");
    }
    tft.setTextColor(COL_WARN, COL_BG);
    tft.drawString(sbuf, 4, SCREEN_H - 16);
  }
}

// ── PAGE GPS ─────────────────────────────────────────────────────
void drawPageGPS() {
  if (_gpsFirstDraw) {
    tft.fillScreen(COL_BG);
    _gpsFirstDraw = false;
  }
  drawHeader("GPS INFO");

  float lat     = getLatitude();
  float lon     = getLongitude();
  float spd     = getSpeed();
  float alt_gps = getAltitude();
  float alt_baro = getBaroAltitude();
  uint32_t sats = getSatellites();
  float hdop    = getHDOP();

  // Badge FIX/NO FIX
  bool hasFix = hasGPSFix();
  int bx = SCREEN_W - 74;
  tft.fillRect(bx, 5, 70, 22, COL_HEADER);
  drawBadge(bx, 5, hasFix ? "FIX" : "NO FIX",
            hasFix ? COL_OK : COL_ERR,
            hasFix ? COL_BG : COL_VALUE);

  int y = HEADER_H + 6;
  int step = 38;

  drawRow(y, "LAT",   lat, 6, "deg"); y += step;
  drawRow(y, "LON",   lon, 6, "deg"); y += step;

  // Vitesse + Vmax
  drawRow(y, "SPD",   spd, 1, "km/h");
  char vmaxBuf[16];
  snprintf(vmaxBuf, sizeof(vmaxBuf), "MAX:%.0f", getSpeedMax());
  tft.setTextColor(COL_UNIT, COL_BG);
  tft.setTextSize(1);
  tft.drawString(vmaxBuf, SCREEN_W - 56, y + 6);
  y += step;

  // Altitude GPS ou baro
  if (alt_gps != 0.0f) {
    drawRow(y, "ALT GPS",  alt_gps,  0, "m");
  } else {
    drawRow(y, "ALT BAR", alt_baro, 0, "m");
  }
  y += step;

  // SAT + HDOP
  tft.fillRect(0, y, SCREEN_W, 20, COL_BG);
  tft.setTextSize(2);

  char satbuf[24];
  snprintf(satbuf, sizeof(satbuf), "SAT:%lu", (unsigned long)sats);
  tft.setTextColor(sats >= 4 ? COL_OK : COL_WARN, COL_BG);
  tft.drawString(satbuf, 6, y + 2);

  char hdopbuf[16];
  snprintf(hdopbuf, sizeof(hdopbuf), "HDOP:%.1f", hdop);
  tft.setTextColor(hdop < 2.0f ? COL_OK : (hdop < 5.0f ? COL_WARN : COL_ERR), COL_BG);
  tft.drawString(hdopbuf, 130, y + 2);
  y += 24;

  // Barre graphique satellites (0-12)
  tft.fillRect(6, y, SCREEN_W - 12, 14, 0x1082);
  tft.drawRect(6, y, SCREEN_W - 12, 14, COL_DIVIDER);
  int barW = (SCREEN_W - 16) * min((int)sats, 12) / 12;
  uint16_t barColor = sats < 4 ? COL_ERR : (sats < 8 ? COL_WARN : COL_OK);
  if (barW > 0) tft.fillRect(8, y + 2, barW, 10, barColor);
  y += 20;

  // ── Trip info ──────────────────────────────────────────
  tft.fillRect(0, y, SCREEN_W, 40, COL_BG);
  tft.drawFastHLine(0, y, SCREEN_W, COL_DIVIDER);
  y += 4;

  tft.setTextSize(2);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("TRIP", 6, y);

  // Distance
  float dist = getTotalDistance();
  char distBuf[16];
  if (dist < 1.0f) {
    snprintf(distBuf, sizeof(distBuf), "%.0fm", dist * 1000);
  } else {
    snprintf(distBuf, sizeof(distBuf), "%.1fkm", dist);
  }
  tft.setTextColor(COL_VALUE, COL_BG);
  tft.drawString(distBuf, 66, y);

  // Durée trip
  uint32_t tripSec = getTripSeconds();
  char timeBuf[12];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu",
           (unsigned long)(tripSec / 3600),
           (unsigned long)((tripSec % 3600) / 60),
           (unsigned long)(tripSec % 60));
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(timeBuf, 156, y);
}

// ── PAGE COMPASS (Sprite anti-scintillement) ─────────────────────
void drawPageCompass() {
  float hdg = getCourse();

  if (_compassFirstDraw) {
    tft.fillScreen(COL_BG);
    drawHeader("COMPASS");
    if (!compassSprite.created()) {
      compassSprite.createSprite(SCREEN_W, SCREEN_H - HEADER_H - 2);
    }
    _compassFirstDraw = false;
  }

  int spriteYOff = HEADER_H + 2;
  compassSprite.fillSprite(COL_BG);

  // Coordonnées locales dans le sprite
  int cx = SCREEN_W / 2;
  int cy = 118;   // centre cercle dans le sprite
  int r = 90;

  // Valeur numérique au-dessus du cercle
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f", hdg);
  compassSprite.setTextColor(COL_VALUE, COL_BG);
  compassSprite.setTextSize(4);
  compassSprite.drawCentreString(buf, cx, cy - r - 38, 1);

  // Cercle extérieur double
  compassSprite.drawCircle(cx, cy, r + 2, COL_DIVIDER);
  compassSprite.drawCircle(cx, cy, r,     COL_ACCENT);
  compassSprite.drawCircle(cx, cy, r - 1, COL_HEADER);

  // Graduations tous les 30°
  for (int deg = 0; deg < 360; deg += 30) {
    float rd = (deg - 90) * 0.0174532925f;
    int x1 = cx + (int)((r - 2) * cos(rd));
    int y1 = cy + (int)((r - 2) * sin(rd));
    int x2 = cx + (int)((r - 8) * cos(rd));
    int y2 = cy + (int)((r - 8) * sin(rd));
    compassSprite.drawLine(x1, y1, x2, y2, COL_LABEL);
  }

  // Labels cardinaux
  compassSprite.setTextColor(COL_VALUE, COL_BG);
  compassSprite.setTextSize(2);
  compassSprite.drawCentreString("N", cx,          cy - r - 18, 1);
  compassSprite.drawCentreString("S", cx,          cy + r + 6,  1);
  compassSprite.drawString("W",       cx - r - 18, cy - 8);
  compassSprite.drawString("E",       cx + r + 8,  cy - 8);

  // Diagonales
  compassSprite.setTextColor(COL_LABEL, COL_BG);
  compassSprite.drawString("NW", cx - (int)(r * 0.72f) - 20, cy - (int)(r * 0.72f) - 10);
  compassSprite.drawString("NE", cx + (int)(r * 0.72f) + 6,  cy - (int)(r * 0.72f) - 10);
  compassSprite.drawString("SW", cx - (int)(r * 0.72f) - 20, cy + (int)(r * 0.72f));
  compassSprite.drawString("SE", cx + (int)(r * 0.72f) + 6,  cy + (int)(r * 0.72f));

  // Aiguille avec ombre
  float rd = (hdg - 90.0f) * 0.0174532925f;
  int nx = cx + (int)((r - 12) * cos(rd));
  int ny = cy + (int)((r - 12) * sin(rd));
  int bx_tip = cx - (int)(30 * cos(rd));
  int by_tip = cy - (int)(30 * sin(rd));

  // Ombre
  compassSprite.drawLine(cx+1, cy+1, nx+1, ny+1, 0x2104);
  compassSprite.drawLine(cx+2, cy+2, nx+2, ny+2, 0x2104);

  // Corps aiguille (rouge nord, gris sud)
  compassSprite.drawLine(cx, cy, nx, ny, TFT_RED);
  compassSprite.drawLine(cx+1, cy, nx, ny, TFT_RED);
  compassSprite.drawLine(cx-1, cy, nx, ny, TFT_RED);
  compassSprite.drawLine(cx, cy, bx_tip, by_tip, COL_LABEL);
  compassSprite.drawLine(cx+1, cy, bx_tip, by_tip, COL_LABEL);

  // Centre pivot
  compassSprite.fillCircle(cx, cy, 6, COL_HEADER);
  compassSprite.drawCircle(cx, cy, 6, COL_ACCENT);
  compassSprite.fillCircle(cx, cy, 3, COL_VALUE);

  // Direction textuelle
  compassSprite.setTextSize(2);
  compassSprite.setTextColor(COL_ACCENT, COL_BG);
  int hdgi = (int)hdg;
  const char* dir;
  if      (hdgi <  23 || hdgi >= 338) dir = "Nord";
  else if (hdgi <  68)                 dir = "NordEst";
  else if (hdgi < 113)                 dir = "Est";
  else if (hdgi < 158)                 dir = "SudEst";
  else if (hdgi < 203)                 dir = "Sud";
  else if (hdgi < 248)                 dir = "SudOuest";
  else if (hdgi < 293)                 dir = "Ouest";
  else                                 dir = "NordOuest";
  compassSprite.drawCentreString(dir, cx, cy + r + 28, 1);

  // Push sprite → écran (une seule transaction SPI = 0 scintillement)
  compassSprite.pushSprite(0, spriteYOff);
}

// ── PAGE ENV (Capteurs) ───────────────────────────────────────────
void drawPageEnv() {
  if (_envFirstDraw) {
    tft.fillScreen(COL_BG);
    _envFirstDraw = false;
  }
  drawHeader("ENVIRONNEMENT");

  int y = HEADER_H + 8;
  int step = 42;

  // Indicateurs d'état des capteurs en haut à droite
  tft.setTextSize(2);
  if (!isAhtOK()) {
    drawBadge(SCREEN_W - 84, 6, "AHT KO", COL_ERR, COL_VALUE);
  }
  if (!isBmpOK()) {
    drawBadge(SCREEN_W - 84, HEADER_H + 6, "BMP KO", COL_ERR, COL_VALUE);
  }

  drawRow(y, "TEMP",  getTemperature(), 1, "C");   y += step;
  drawRow(y, "HUM",   getHumidity(),    1, "%");   y += step;
  drawRow(y, "PRES",  getPressure(),    1, "hPa"); y += step;
  drawRow(y, "ALT",   getBaroAltitude(), 0, "m");
}

// ── PAGE IMU (Niveau à bulle avec TFT_eSprite pour fluidité) ───

#define BUBBLE_OUTER_R   80
#define BUBBLE_CENTER_X  120
#define BUBBLE_CENTER_Y  140
#define BUBBLE_BALL_R    14
#define BUBBLE_OK_R      16

void drawPageIMU() {
  float ax, ay, az, gx, gy, gz;
  getAllMotion(ax, ay, az, gx, gy, gz);

  if (_imuFirstDraw) {
    tft.fillScreen(COL_BG);
    drawHeader("NIVEAU");
    
    // Création du Sprite une seule fois pour éviter de fragmenter la RAM
    if (!imuSprite.created()) {
      imuSprite.createSprite(SCREEN_W, BUBBLE_CENTER_Y + BUBBLE_OUTER_R + 20 - HEADER_H); // ≈ 240x210 pixels
    }
    _imuFirstDraw = false;
  }

  // --- Dessin dans le Sprite (mémoire) ---
  int spriteYOffset = HEADER_H + 2; // Le sprite commence juste sous la ligne du header
  imuSprite.fillSprite(COL_BG);
  
  // Centre local au sprite
  int localCX = BUBBLE_CENTER_X;
  int localCY = BUBBLE_CENTER_Y - spriteYOffset;

  // Cercle extérieur avec double trait
  imuSprite.drawCircle(localCX, localCY, BUBBLE_OUTER_R + 2, COL_DIVIDER);
  imuSprite.drawCircle(localCX, localCY, BUBBLE_OUTER_R,     COL_ACCENT);
  imuSprite.drawCircle(localCX, localCY, BUBBLE_OUTER_R - 1, COL_HEADER);

  // Axes centraux
  imuSprite.drawFastHLine(localCX - BUBBLE_OUTER_R + 2, localCY, (BUBBLE_OUTER_R - 2) * 2, COL_DIVIDER);
  imuSprite.drawFastVLine(localCX, localCY - BUBBLE_OUTER_R + 2, (BUBBLE_OUTER_R - 2) * 2, COL_DIVIDER);

  // Zone "plat OK"
  imuSprite.drawCircle(localCX, localCY, BUBBLE_OK_R,     COL_OK);
  imuSprite.drawCircle(localCX, localCY, BUBBLE_OK_R - 1, 0x03C0);

  // Variables calcul position
  float clampAx = max(-1.0f, min(1.0f, ax));
  float clampAy = max(-1.0f, min(1.0f, ay));
  int maxOff = BUBBLE_OUTER_R - BUBBLE_BALL_R - 2;
  int bubX = localCX + (int)(clampAx * maxOff);
  int bubY = localCY + (int)(clampAy * maxOff);

  float dist = sqrt((float)(bubX - localCX) * (bubX - localCX) + (float)(bubY - localCY) * (bubY - localCY));
  if (dist > maxOff) {
    float scale = maxOff / dist;
    bubX = localCX + (int)((bubX - localCX) * scale);
    bubY = localCY + (int)((bubY - localCY) * scale);
  }

  bool inZone = (abs(bubX - localCX) < BUBBLE_OK_R && abs(bubY - localCY) < BUBBLE_OK_R);
  uint16_t bubColor = inZone ? COL_ACCENT : COL_WARN;

  // Bulle avec ombre
  imuSprite.fillCircle(bubX + 1, bubY + 2, BUBBLE_BALL_R, 0x2104);
  imuSprite.fillCircle(bubX, bubY, BUBBLE_BALL_R, bubColor);
  imuSprite.fillCircle(bubX - 3, bubY - 3, 3, TFT_WHITE);  

  // Labels N/S/E/W
  imuSprite.setTextColor(COL_LABEL, COL_BG);
  imuSprite.setTextSize(2);
  imuSprite.drawCentreString("N", localCX, localCY - BUBBLE_OUTER_R - 20, 1);
  imuSprite.drawCentreString("S", localCX, localCY + BUBBLE_OUTER_R + 4,  1);
  imuSprite.drawString("O", localCX - BUBBLE_OUTER_R - 18, localCY - 8);
  imuSprite.drawString("E", localCX + BUBBLE_OUTER_R + 8,  localCY - 8);

  // Envoi du Sprite à l'écran (ultra rapide et sans scintillement)
  imuSprite.pushSprite(0, spriteYOffset);

  // Zone valeurs en bas
  int yVal = BUBBLE_CENTER_Y + BUBBLE_OUTER_R + 24;
  tft.fillRect(0, yVal, SCREEN_W, SCREEN_H - yVal, COL_BG);

  char buf[32];
  tft.setTextSize(2);

  snprintf(buf, sizeof(buf), "AX:%.2f", ax);
  tft.setTextColor(inZone ? COL_OK : COL_WARN, COL_BG);
  tft.drawString(buf, 4, yVal);

  snprintf(buf, sizeof(buf), "AY:%.2f", ay);
  tft.drawString(buf, 130, yVal);

  snprintf(buf, sizeof(buf), "AZ:%.2f", az);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawCentreString(buf, SCREEN_W / 2, yVal + 24, 1);

  snprintf(buf, sizeof(buf), "G:%.1f %.1f %.1f", gx, gy, gz);
  tft.setTextColor(COL_DIVIDER, COL_BG);
  tft.drawCentreString(buf, SCREEN_W / 2, yVal + 48, 1);

  if (inZone) {
    tft.setTextColor(COL_OK, COL_BG);
    tft.drawCentreString("** LEVEL **", SCREEN_W / 2, yVal + 72, 1);
  }
}

// ── PAGE MAP ──────────────────────────────────────────────────────
extern float mapPanLatOffset;
extern float mapPanLonOffset;

void drawPageMap() {
  float lat = getLatitude();
  float lon = getLongitude();

  if (lat == 0.0f && lon == 0.0f) {
    // Si pas de fix, on récupère la dernière position connue depuis la flash NVS
    lat = prefs.getFloat("lat", 43.7102f);
    lon = prefs.getFloat("lon", 7.2620f);
  }

  // Application du décalage panoramique par joystick
  lat += mapPanLatOffset;
  lon += mapPanLonOffset;

  // Clamper la latitude et longitude pour éviter les plantages (division zéro Web Mercator)
  if (lat > 85.05f) { lat = 85.05f; mapPanLatOffset = 85.05f - (getLatitude() != 0.0f ? getLatitude() : 43.7102f); }
  if (lat < -85.05f) { lat = -85.05f; mapPanLatOffset = -85.05f - (getLatitude() != 0.0f ? getLatitude() : 43.7102f); }
  if (lon > 180.0f) { lon -= 360.0f; mapPanLonOffset -= 360.0f; }
  if (lon < -180.0f) { lon += 360.0f; mapPanLonOffset += 360.0f; }

  bool posChanged  = (abs(lat - _lastMapLat) > 0.0001f || abs(lon - _lastMapLon) > 0.0001f);
  bool zoomChanged = (mapZoom != _lastMapZoom);

  if (!_mapNeedsRedraw && !posChanged && !zoomChanged) return;

  _lastMapLat  = lat;
  _lastMapLon  = lon;
  _lastMapZoom = mapZoom;
  _mapNeedsRedraw = false;

  // Header carte
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER);
  tft.drawFastHLine(0, HEADER_H, SCREEN_W, COL_ACCENT);
  tft.setTextColor(COL_ACCENT, COL_HEADER);
  tft.setTextSize(2);

  char hdr[28];
  snprintf(hdr, sizeof(hdr), "MAP  Z:%d  SAT:%d", mapZoom, (int)getSatellites());
  tft.drawString(hdr, 6, 8);

  bool hasFix = (getLatitude() != 0.0f || getLongitude() != 0.0f);
  if (hasFix) {
    if (mapPanLatOffset != 0.0f || mapPanLonOffset != 0.0f) {
       drawBadge(SCREEN_W - 60, 6, "PAN", COL_WARN, COL_BG); // Indique qu'on explore
    } else {
       drawBadge(SCREEN_W - 60, 6, "GPS", COL_OK, COL_BG);
    }
  } else {
    drawBadge(SCREEN_W - 60, 6, "SIM", COL_WARN, COL_BG);
  }

  if (!sdOK()) {
    tft.fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, COL_BG);
    tft.setTextColor(COL_ERR, COL_BG);
    tft.setTextSize(2);
    tft.drawCentreString("SD non dispo", SCREEN_W/2, SCREEN_H/2, 1);
    return;
  }

  drawMap(lat, lon, mapZoom);
  drawGPXOverlay(&tft, lat, lon, mapZoom);

  // Infos coordonnées en bas
  tft.fillRect(0, SCREEN_H - 24, SCREEN_W, 24, COL_BG);
  tft.drawFastHLine(0, SCREEN_H - 24, SCREEN_W, COL_DIVIDER);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextSize(2);
  char coord[32];
  snprintf(coord, sizeof(coord), "%.4f,%.4f", lat, lon);
  tft.drawString(coord, 6, SCREEN_H - 20);
}

// ── SETUP ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== BOOT ===");

  prefs.begin("garmine", false); // Initialisation de la mémoire flash NVS

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(COL_BG);

  // Splash screen amélioré
  tft.fillRect(0, 0, SCREEN_W, 60, COL_HEADER);
  tft.drawFastHLine(0, 60, SCREEN_W, COL_ACCENT);
  tft.setTextColor(COL_ACCENT, COL_HEADER);
  tft.setTextSize(4);
  tft.drawString("TrailNav", 20, 14);

  tft.setTextSize(2);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.drawString("GPS Device v2.2", 28, 68);
  tft.drawString("ESP32-S3 N16R8", 40, 88);

  int yLine = 120;
  auto tftLog = [&](const char* msg, uint16_t color = TFT_WHITE) {
    tft.fillRect(0, yLine, SCREEN_W, 20, COL_BG);
    tft.setTextColor(color, COL_BG);
    tft.setTextSize(2);
    tft.drawString(msg, 8, yLine);
    Serial.println(msg);
    yLine += 24;
    if (yLine > 280) yLine = 120;
  };

  Wire.begin(21, 20);
  initButtons();
  initGPS();
  tftLog("I2C + GPS OK", COL_OK);

  tftLog("Init capteurs...", COL_LABEL);
  initSensors();
  if (isBmpOK() && isAhtOK()) {
    tftLog("Capteurs OK", COL_OK);
  } else {
    char capBuf[28];
    snprintf(capBuf, sizeof(capBuf), "BMP:%s AHT:%s",
             isBmpOK() ? "OK" : "ERR",
             isAhtOK() ? "OK" : "ERR");
    tftLog(capBuf, isBmpOK() || isAhtOK() ? COL_WARN : COL_ERR);
  }
  if (!isMpuOK()) tftLog("MPU: non detecte", COL_WARN);

  tftLog("Test SD...", COL_LABEL);

  if (initSD()) {
    tftLog("SD: OK !", COL_OK);

    char szBuf[24];
    snprintf(szBuf, sizeof(szBuf), "Size: %lluMB",
             SD.cardSize() / (1024ULL * 1024ULL));
    tftLog(szBuf, COL_OK);

    initMap(&tft);

    tftLog("GPX...", COL_LABEL);
    int pts = loadGPX("/track.gpx");
    if (pts > 0) {
      char buf[24];
      snprintf(buf, sizeof(buf), "GPX: %d pts", pts);
      tftLog(buf, COL_OK);
    } else {
      tftLog("GPX: aucun", COL_WARN);
    }
  } else {
    tftLog("SD: ECHEC!", COL_ERR);
  }

  delay(2500);
  drawPageMenu();
}

// Variables panoramique carte (Globales, utilisées dans drawPageMap et modifiées dans loop)
float mapPanLatOffset = 0.0f;
float mapPanLonOffset = 0.0f;

void loop() {
  updateGPS();

  // Auto-calibrer le baromètre avec le GPS (si fix très précis)
  // Et sauvegarder la géoloc en NVS toutes les 3 minutes
  static unsigned long lastCalib = 0;
  if (millis() - lastCalib > 30000) { // toutes les 30 secondes
    if (getSatellites() >= 6 && getHDOP() < 2.0f && getAltitude() != 0.0f) {
      calibrateBaro(getAltitude());
    }
    
    // Sauvegarde NVS de la dernière bonne position toutes les ~30s au lieu de 3min pour éviter la perte
    if (getLatitude() != 0.0f && getLongitude() != 0.0f) {
      prefs.putFloat("lat", getLatitude());
      prefs.putFloat("lon", getLongitude());
    }
    
    lastCalib = millis();
  }

  // Auto-reconnexion SD toutes les 10s si elle est déconnectée
  if (!sdOK() && millis() - lastSDRetry > 10000) {
    lastSDRetry = millis();
    Serial.println("[SD] Tentative auto-reconnexion...");
    if (initSD()) {
      Serial.println("[SD] Reconnectée !");
      initMap(&tft);
    }
  }

  int btn = handleButtons();

  if (currentPage == PAGE_MENU) {
    if (millis() - lastMenuTime > 1000) {
      lastMenuTime = millis();
      updateMenuTime();
    }

    if (btn == BTN_DOWN_PRESSED) {
      menuIndex = (menuIndex + 1) % MENU_MAX;
      drawPageMenu();
    }
    else if (btn == BTN_UP_PRESSED) {
      menuIndex = (menuIndex - 1 + MENU_MAX) % MENU_MAX;
      drawPageMenu();
    }
    else if (btn == BTN_RIGHT_PRESSED || btn == BTN_SET_PRESSED || btn == BTN_MID_PRESSED) {
      if      (menuIndex == 0) {
        currentPage = PAGE_GPS;
        _gpsFirstDraw = true;
      }
      else if (menuIndex == 1) {
        currentPage = PAGE_ENV;
        _envFirstDraw = true;
      }
      else if (menuIndex == 2) {
        currentPage = PAGE_IMU;
        _imuFirstDraw = true;
      }
      else if (menuIndex == 3) {
        currentPage = PAGE_COMPASS;
        _compassFirstDraw = true;
      }
      else if (menuIndex == 4) {
        currentPage = PAGE_MAP;
        mapPanLatOffset = 0;
        mapPanLonOffset = 0;
      }

      lastUpdate = millis();
      _mapNeedsRedraw = true;
    }
  } else {
    // Boutons sur les autres pages
    if (currentPage == PAGE_MAP) {
      float panStep = 0.01f / pow(2.0f, mapZoom - 13); // Panoramique dynamique selon zoom

      if (btn == BTN_UP_PRESSED && mapZoom < MAP_ZOOM_MAX) {
        mapZoom++; _mapNeedsRedraw = true;
      } 
      else if (btn == BTN_DOWN_PRESSED && mapZoom > MAP_ZOOM_MIN) {
        mapZoom--; _mapNeedsRedraw = true;
      }
      else if (btn == BTN_MID_PRESSED) { // Nord
        mapPanLatOffset += panStep; _mapNeedsRedraw = true;
      }
      else if (btn == BTN_SET_PRESSED) { // Sud
        mapPanLatOffset -= panStep; _mapNeedsRedraw = true;
      }
      else if (btn == BTN_RIGHT_PRESSED) { // Est
        mapPanLonOffset += panStep; _mapNeedsRedraw = true;
      }
      else if (btn == BTN_LEFT_PRESSED) { // Ouest
        mapPanLonOffset -= panStep; _mapNeedsRedraw = true;
      }
      else if (btn == BTN_RST_PRESSED) { // Retour menu ET recentre
        currentPage = PAGE_MENU;
        lastMenuTime = 0;
        drawPageMenu();
      }
    } 
    else if (currentPage == PAGE_IMU) {
      if (btn == BTN_SET_PRESSED) {
        tareIMU(); // Fixer le zéro
      }
      // LEFT/RST retournent
      else if (btn == BTN_LEFT_PRESSED || btn == BTN_RST_PRESSED) {
        currentPage = PAGE_MENU;
        lastMenuTime = 0;
        drawPageMenu();
      }
    }
    else {
      // Autres pages (GPS, COMPASS, ENV)
      if (btn == BTN_LEFT_PRESSED || btn == BTN_RST_PRESSED) {
        currentPage = PAGE_MENU;
        lastMenuTime = 0;
        drawPageMenu();
      }
    }
  }

  // Redraw régulier selon la page
  unsigned long interval;
  if (currentPage == PAGE_MAP) {
    interval = _mapNeedsRedraw ? 50 : 3000;
  } else if (currentPage == PAGE_IMU) {
    interval = 50;   // bulle hyper fluide avec joystick snap-action
  } else if (currentPage == PAGE_MENU) {
    return;  // le menu est géré par updateMenuTime()
  } else {
    interval = 500;
  }

  if (millis() - lastUpdate > interval || _mapNeedsRedraw) {
    lastUpdate = millis();

    switch (currentPage) {
      case PAGE_GPS:     drawPageGPS();     break;
      case PAGE_ENV:     drawPageEnv();     break;
      case PAGE_IMU:     drawPageIMU();     break;
      case PAGE_COMPASS: drawPageCompass(); break;
      case PAGE_MAP:     drawPageMap();     break;
      default: break;
    }
  }
}