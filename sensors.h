#ifndef SENSORS_H
#define SENSORS_H

#include <math.h>  // pour NAN

void initSensors();

// État des capteurs (true = détecté et initialisé)
bool isBmpOK();
bool isAhtOK();
bool isMpuOK();

// Retournent NAN si le capteur n'est pas disponible
float getTemperature();
float getHumidity();
float getPressure();
float getBaroAltitude();

// Calibre l'altitude barométrique à partir d'une altitude connue (ex: GPS)
void calibrateBaro(float realAltitude);

// Lecture unique des 6 axes (évite d'appeler getMotion6 plusieurs fois)
void getAllMotion(float &ax, float &ay, float &az,
                 float &gx, float &gy, float &gz);

// Accesseurs individuels (utilisent la dernière lecture en cache)
float getAccelX();
float getAccelY();
float getAccelZ();

float getGyroX();
float getGyroY();
float getGyroZ();

#endif