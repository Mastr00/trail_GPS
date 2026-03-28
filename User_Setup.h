/*
 * User_Setup.h pour TFT_eSPI - GARMINE_LIKE
 *
 * Ce fichier est une COPIE de référence.
 * Le vrai fichier est déjà dans :
 *    C:\Users\mehdi\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
 *
 * Configuré pour : Écran 1.8" ST7735S 128x160 SPI 4 fils
 */

// ============================================================
// DRIVER
// ============================================================
// ============================================================
// DRIVER
// ============================================================
#define ST7789_DRIVER // Remplacé ST7735_DRIVER par ST7789_DRIVER

// ============================================================
// DIMENSIONS
// ============================================================
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Offset d'écran — nécessaire sur certains modules ST7735S
// Décommentez si l'image est décalée de quelques pixels :
// #define TFT_XOFFSET 2
// #define TFT_YOFFSET 1

// ============================================================
// PINS SPI
// Adaptez selon votre câblage ESP32 :
//   SCK  → GPIO 18
//   SDA  → GPIO 23  (= MOSI)
//   RES  → GPIO 4   (= RST)
//   DC   → GPIO 2   (= A0/RS)
//   CS   → GPIO 15
//   BLK  → 3.3V ou un GPIO pour contrôle rétroéclairage
// ============================================================
#define TFT_MISO  -1   // Non utilisé
#define TFT_MOSI  11   // SDA / DIN
#define TFT_SCLK  18   // SCK / CLK
#define TFT_CS    10   // CS  (Chip Select)
#define TFT_DC     9   // DC  / A0 / RS (Data/Command)
#define TFT_RST    8   // RES (Reset)
// #define TFT_BL  32  // Optionnel : pin PWM rétroéclairage

// ============================================================
// POLICES À CHARGER
// ============================================================
#define LOAD_GLCD    // 5x7 (toujours utile)
#define LOAD_FONT2   // Petits chiffres
#define LOAD_FONT4   // Chiffres moyens
#define LOAD_FONT6   // Grands chiffres
#define LOAD_FONT7   // Style 7-segment
#define LOAD_FONT8   // Très grands
#define LOAD_GFXFF   // Polices FreeFonts
#define SMOOTH_FONT

// ============================================================
// PERFORMANCE SPI
// ST7789 supporte jusqu'à 40-80 MHz
// ============================================================
#define SPI_FREQUENCY      40000000
#define SPI_READ_FREQUENCY 20000000
