#ifndef MAP_H
#define MAP_H

#include <TFT_eSPI.h>

// Zoom par défaut pour l'affichage carte (utilise les tuiles zoom 13, 14 et 15)
#define MAP_DEFAULT_ZOOM 14
#define MAP_ZOOM_MIN     13
#define MAP_ZOOM_MAX     15

// Initialise le module map (appeler après initSD)
void initMap(TFT_eSPI* tft);

// Affiche la carte centrée sur lat/lon au niveau de zoom donné
// Charge la tuile depuis /tiles/<zoom>/<x>/<y>.bmp sur la SD
void drawMap(float lat, float lon, int zoom = MAP_DEFAULT_ZOOM);

// Convertit lat/lon en numéro de tuile XYZ
void latLonToTile(float lat, float lon, int zoom, int &tileX, int &tileY);

// Retourne le décalage en pixels dans la tuile
void latLonToPixelOffset(float lat, float lon, int zoom,
                          int tileX, int tileY,
                          int &px, int &py);

// Projette un point lat/lon sur l'écran (par rapport au centre)
// Retourne false si le point est hors écran
bool latLonToScreen(float lat, float lon,
                    float centerLat, float centerLon,
                    int zoom, int &sx, int &sy);

#endif
