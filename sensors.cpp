#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <MPU6050.h>

#include "sensors.h"

Adafruit_BMP280 bmp;
Adafruit_AHTX0  aht;
MPU6050         mpu;

// Flags d'état des capteurs
static bool _bmpOK = false;
static bool _ahtOK = false;
static bool _mpuOK = false;

// Cache des dernières valeurs IMU
static int16_t _ax = 0, _ay = 0, _az = 0;
static int16_t _gx = 0, _gy = 0, _gz = 0;

// Offsets de calibration gyro (mesurés au démarrage à plat)
static float _gx_off = 0, _gy_off = 0, _gz_off = 0;

// Sensibilités MPU6050 par défaut
// Accel ±2g  → 16384 LSB/g
// Gyro  ±250°/s → 131 LSB/(°/s)
#define ACCEL_SCALE  16384.0f
#define GYRO_SCALE     131.0f

// Filtre passe-bas simple (lissage exponentiel)
#define ALPHA  0.15f   // 0=figé, 1=pas de filtrage — 0.15 = bon compromis

static float _ax_f = 0, _ay_f = 0, _az_f = 0;
static float _gx_f = 0, _gy_f = 0, _gz_f = 0;

// Cache AHTx0 — lecture unique par cycle pour temperature ET humidite
static float _cachedTemp = 0;
static float _cachedHum  = 0;
static unsigned long _ahtLastRead = 0;
#define AHT_CACHE_MS 500   // rafraîchi au maximum toutes les 500 ms

// -------------------------------------------------------
// Calibration gyro RAPIDE — 50 échantillons (≈150 ms)
// IMPORTANT: réduit le blocage au boot qui faisait perdre
// des trames NMEA GPS (anciennement 200 × 3ms = 600 ms)
static void calibrateGyro() {
  long sx = 0, sy = 0, sz = 0;
  const int N = 50;
  int16_t ax, ay, az, gx, gy, gz;

  for (int i = 0; i < N; i++) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    sx += gx; sy += gy; sz += gz;
    delay(3);
  }

  _gx_off = (float)sx / N;
  _gy_off = (float)sy / N;
  _gz_off = (float)sz / N;
}

// -------------------------------------------------------
static void refreshAHT() {
  if (!_ahtOK) return;
  unsigned long now = millis();
  if (now - _ahtLastRead < AHT_CACHE_MS) return;
  _ahtLastRead = now;

  sensors_event_t humEvt, tempEvt;
  aht.getEvent(&humEvt, &tempEvt);
  _cachedTemp = tempEvt.temperature;
  _cachedHum  = humEvt.relative_humidity;
}

// -------------------------------------------------------
void initSensors() {
  _bmpOK = bmp.begin(0x76);
  if (!_bmpOK) _bmpOK = bmp.begin(0x77);

  if (_bmpOK) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
  }

  _ahtOK = aht.begin();

  mpu.initialize();
  
  // Détection réelle : testConnection() d'abord, puis fallback pour les clones
  _mpuOK = mpu.testConnection();
  if (!_mpuOK) {
    // Certains clones (MPU6500 vendu comme 6050) ont un WHO_AM_I différent
    // On vérifie en lisant l'accéléromètre : si on reçoit des données, c'est OK
    int16_t tax, tay, taz;
    mpu.getAcceleration(&tax, &tay, &taz);
    _mpuOK = (tax != 0 || tay != 0 || taz != 0);
    if (_mpuOK) Serial.println("[MPU] Clone detecte via lecture accel");
  }
  
  if (_mpuOK) {
    // Sensibilité maximale : ±250°/s gyro, ±2g accel
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    calibrateGyro();
  }
}

// -------------------------------------------------------
bool isBmpOK()  { return _bmpOK; }
bool isAhtOK()  { return _ahtOK; }
bool isMpuOK()  { return _mpuOK; }

// -------------------------------------------------------
float getTemperature() {
  if (!_ahtOK) return NAN;
  refreshAHT();
  return _cachedTemp;
}

float getHumidity() {
  if (!_ahtOK) return NAN;
  refreshAHT();
  return _cachedHum;
}

float getPressure() {
  if (!_bmpOK) return NAN;
  return bmp.readPressure() / 100.0f;
}

static float _seaLevelPressure = 1013.25f;

void calibrateBaro(float realAltitude) {
  if (!_bmpOK) return;
  float p = bmp.readPressure() / 100.0f;
  // Formule inverse pour retrouver la pression au niveau de la mer
  if (p > 500.0f && p < 1200.0f) { 
    _seaLevelPressure = p / pow(1.0f - realAltitude / 44330.0f, 5.255f);
  }
}

float getBaroAltitude() {
  if (!_bmpOK) return NAN;
  return bmp.readAltitude(_seaLevelPressure);
}

// Offsets de tare (définis par l'utilisateur via le bouton SET)
static float _tare_ax = 0;
static float _tare_ay = 0;

void tareIMU() {
  if (!_mpuOK) return;
  _tare_ax = _ax_f;
  _tare_ay = _ay_f;
}

// -------------------------------------------------------
void getAllMotion(float &ax, float &ay, float &az,
                 float &gx, float &gy, float &gz) {
  if (!_mpuOK) {
    ax = ay = az = gx = gy = gz = 0;
    return;
  }

  mpu.getMotion6(&_ax, &_ay, &_az, &_gx, &_gy, &_gz);

  // Conversion en unités physiques
  float raw_ax = _ax / ACCEL_SCALE;
  float raw_ay = _ay / ACCEL_SCALE;
  float raw_az = _az / ACCEL_SCALE;
  float raw_gx = (_gx - _gx_off) / GYRO_SCALE;
  float raw_gy = (_gy - _gy_off) / GYRO_SCALE;
  float raw_gz = (_gz - _gz_off) / GYRO_SCALE;

  // Filtre passe-bas exponentiel
  _ax_f = ALPHA * raw_ax + (1 - ALPHA) * _ax_f;
  _ay_f = ALPHA * raw_ay + (1 - ALPHA) * _ay_f;
  _az_f = ALPHA * raw_az + (1 - ALPHA) * _az_f;
  _gx_f = ALPHA * raw_gx + (1 - ALPHA) * _gx_f;
  _gy_f = ALPHA * raw_gy + (1 - ALPHA) * _gy_f;
  _gz_f = ALPHA * raw_gz + (1 - ALPHA) * _gz_f;

  ax = _ax_f - _tare_ax; 
  ay = _ay_f - _tare_ay; 
  az = _az_f;
  gx = _gx_f; gy = _gy_f; gz = _gz_f;
}

float getAccelX() { return _ax_f - _tare_ax; }
float getAccelY() { return _ay_f - _tare_ay; }
float getAccelZ() { return _az_f; }
float getGyroX()  { return _gx_f; }
float getGyroY()  { return _gy_f; }
float getGyroZ()  { return _gz_f; }