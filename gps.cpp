#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <math.h>
#include "gps.h"

static TinyGPSPlus gps;
static HardwareSerial GPSserial(1);

// Variables GPS privées au fichier (bug #1 corrigé : ajout static)
static float lat    = 0;
static float lon    = 0;
static float spd    = 0;
static float alt    = 0;
static float course = 0;
static bool  _hasFix = false;

// Trip data
static float _speedMax = 0;
static double _totalDist = 0;  // mètres (double pour précision cumulative)
static unsigned long _tripStart = 0;
static bool _tripStarted = false;
static float _prevLat = 0, _prevLon = 0;
static bool _hasPrev = false;

// Fix perdu si pas de mise à jour depuis 10s
#define FIX_TIMEOUT_MS 10000

// ── Haversine : distance en mètres entre 2 points GPS ────
static float haversine(float lat1, float lon1, float lat2, float lon2) {
  float dLat = (lat2 - lat1) * 0.0174533f;
  float dLon = (lon2 - lon1) * 0.0174533f;
  float a = sin(dLat / 2) * sin(dLat / 2) +
            cos(lat1 * 0.0174533f) * cos(lat2 * 0.0174533f) *
            sin(dLon / 2) * sin(dLon / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return 6371000.0f * c;
}

// ── Timezone auto France (CET/CEST) ──────────────────────
// Passage heure été : dernier dimanche de mars à 01:00 UTC
// Passage heure hiver : dernier dimanche d'octobre à 01:00 UTC

static int dayOfWeek(int y, int m, int d) {
  // Sakamoto : 0=dimanche, 1=lundi, ..., 6=samedi
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int getTZOffset() {
  if (!gps.date.isValid() || gps.date.year() < 2021) return 1; // CET par défaut

  int year  = gps.date.year();
  int month = gps.date.month();
  int day   = gps.date.day();
  int hUTC  = gps.time.hour();

  if (month < 3 || month > 10) return 1;   // Nov-Fév → CET (UTC+1)
  if (month > 3 && month < 10) return 2;   // Avr-Sep → CEST (UTC+2)

  // Mars : CEST commence le dernier dimanche à 01:00 UTC
  if (month == 3) {
    int lastSun = 31 - dayOfWeek(year, 3, 31);
    if (day > lastSun || (day == lastSun && hUTC >= 1)) return 2;
    return 1;
  }

  // Octobre : CET reprend le dernier dimanche à 01:00 UTC
  int lastSun = 31 - dayOfWeek(year, 10, 31);
  if (day < lastSun || (day == lastSun && hUTC < 1)) return 2;
  return 1;
}

// ──────────────────────────────────────────────────────────
void initGPS() {
  GPSserial.setRxBufferSize(1024);
  GPSserial.begin(9600, SERIAL_8N1, 16, 17);
}

void updateGPS() {
  while (GPSserial.available()) {
    gps.encode(GPSserial.read());
  }

  // Vérifier si le fix est valide et récent
  if (gps.location.isValid() && gps.location.age() < FIX_TIMEOUT_MS) {
    float newLat = gps.location.lat();
    float newLon = gps.location.lng();
    _hasFix = true;

    // Accumulation de la distance (seulement si vitesse > 2 km/h)
    if (_hasPrev && spd > 2.0f) {
      float d = haversine(_prevLat, _prevLon, newLat, newLon);
      if (d > 1.0f && d < 500.0f) {  // filtre : entre 1m et 500m
        _totalDist += d;
      }
    }

    _prevLat = newLat;
    _prevLon = newLon;
    _hasPrev = true;
    lat = newLat;
    lon = newLon;

    // Démarrer le chrono trip au premier fix
    if (!_tripStarted) {
      _tripStart = millis();
      _tripStarted = true;
    }
  } else if (gps.location.age() >= FIX_TIMEOUT_MS) {
    // Fix perdu → remettre la vitesse à 0 (bug #2 corrigé)
    _hasFix = false;
    spd = 0;
  }

  if (gps.speed.isValid() && _hasFix) {
    spd = gps.speed.kmph();
    if (spd > _speedMax) _speedMax = spd;
  }

  if (gps.altitude.isValid()) alt    = gps.altitude.meters();
  if (gps.course.isValid())   course = gps.course.deg();
}

// ── Accesseurs ───────────────────────────────────────────
float    getLatitude()  { return lat; }
float    getLongitude() { return lon; }
float    getSpeed()     { return spd; }
float    getAltitude()  { return alt; }
float    getCourse()    { return course; }
bool     hasGPSFix()    { return _hasFix; }

uint32_t getSatellites() {
  return gps.satellites.isValid() ? gps.satellites.value() : 0;
}

float getHDOP() {
  return gps.hdop.isValid() ? gps.hdop.hdop() : 99.9f;
}

// ── Trip data ────────────────────────────────────────────
float    getSpeedMax()     { return _speedMax; }
float    getTotalDistance() { return (float)(_totalDist / 1000.0); } // km
uint32_t getTripSeconds()  {
  if (!_tripStarted) return 0;
  return (millis() - _tripStart) / 1000;
}

void resetTrip() {
  _speedMax = 0;
  _totalDist = 0;
  _tripStart = millis();
  _tripStarted = _hasFix;
  _hasPrev = false;
}

// ── Heure / Date GPS (timezone auto CET/CEST) ───────────
bool gpsTimeValid() {
  return gps.time.isValid()
      && gps.date.isValid()
      && gps.date.year() > 2020
      && gps.time.age() < 2000;
}

uint8_t getHour() {
  int tz = getTZOffset();
  int h = (int)gps.time.hour() + tz;
  if (h >= 24) h -= 24;
  if (h < 0)   h += 24;
  return (uint8_t)h;
}

uint8_t  getMinute() { return gps.time.minute(); }
uint8_t  getSecond() { return gps.time.second(); }
uint8_t  getDay()    { return gps.date.day(); }
uint8_t  getMonth()  { return gps.date.month(); }
uint16_t getYear()   { return gps.date.year(); }