#ifndef GPX_H
#define GPX_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// Nouvelle limite massive : 10 000 points (prend ~80 Ko de PSRAM sur les 8000 dispos)
#define GPX_MAX_PTS 10000

// Charge le fichier GPX depuis la SD avec un buffer de 4 Ko (streaming rapide)
// Nettoie la mémoire auto si un ancien fichier était chargé
// Retourne le nombre de points chargés, ou -1 si erreur
int loadGPX(const char* path);

// Libère la mémoire PSRAM allouée pour le GPX
void freeGPX();

// Accesseurs
int     getGPXPointCount();
float   getGPXLat(int i);
float   getGPXLon(int i);

// Dessine les points GPX sur l'écran en projection autour du centre
void drawGPXOverlay(TFT_eSPI* tft, float centerLat, float centerLon, int zoom);

#endif
