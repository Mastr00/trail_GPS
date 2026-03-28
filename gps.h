#ifndef GPS_H
#define GPS_H

#include <stdint.h>

void initGPS();
void updateGPS();

float    getLatitude();
float    getLongitude();
float    getSpeed();
float    getAltitude();
float    getCourse();

// Qualité du fix GPS
uint32_t getSatellites();
float    getHDOP();
bool     hasGPSFix();        // true si fix valide et récent (< 10s)

// Trip data (accumulés depuis le boot ou resetTrip)
float    getSpeedMax();       // km/h max atteint
float    getTotalDistance();   // distance totale en km
uint32_t getTripSeconds();    // durée depuis premier fix
void     resetTrip();         // remet tout à zéro

// Heure GPS (UTC + timezone auto France CET/CEST)
bool     gpsTimeValid();
uint8_t  getHour();
uint8_t  getMinute();
uint8_t  getSecond();
uint8_t  getDay();
uint8_t  getMonth();
uint16_t getYear();

#endif