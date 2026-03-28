#include <Arduino.h>
#include "buttons.h"

#define BTN_UP    4
#define BTN_DOWN  3
#define BTN_LEFT  14
#define BTN_RIGHT 15
#define BTN_MID   46
#define BTN_SET   7
#define BTN_RST   6

// SNAP ACTION avec Edge-Triggering (Déclenchement unique par appui)
#define DEBOUNCE_MS 20

static int lastRawState = BTN_NONE;
static int validatedState = BTN_NONE;
static unsigned long debounceTimer = 0;

void initButtons() {
  pinMode(BTN_UP,    INPUT_PULLUP);
  pinMode(BTN_DOWN,  INPUT_PULLUP);
  pinMode(BTN_LEFT,  INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_MID,   INPUT_PULLUP);
  pinMode(BTN_SET,   INPUT_PULLUP);
  pinMode(BTN_RST,   INPUT_PULLUP);
}

int handleButtons() {
  int raw = BTN_NONE;
  if      (digitalRead(BTN_UP)    == LOW) { raw = BTN_UP_PRESSED;    }
  else if (digitalRead(BTN_DOWN)  == LOW) { raw = BTN_DOWN_PRESSED;  }
  else if (digitalRead(BTN_LEFT)  == LOW) { raw = BTN_LEFT_PRESSED;  }
  else if (digitalRead(BTN_RIGHT) == LOW) { raw = BTN_RIGHT_PRESSED; }
  else if (digitalRead(BTN_MID)   == LOW) { raw = BTN_MID_PRESSED;   }
  else if (digitalRead(BTN_SET)   == LOW) { raw = BTN_SET_PRESSED;   }
  else if (digitalRead(BTN_RST)   == LOW) { raw = BTN_RST_PRESSED;   }

  // Si le bouton physique change d'état
  if (raw != lastRawState) {
    lastRawState = raw;
    debounceTimer = millis(); // Reset de l'horloge d'anti-rebond
  }

  // Si l'état physique est stable pendant au moins DEBOUNCE_MS (20ms)
  if ((millis() - debounceTimer) > DEBOUNCE_MS) {
    if (raw != validatedState) {
      validatedState = raw; // Nouvel état stable confirmé

      // On agit uniquement lors de l'appui (passage de AUCUN bouton à UN bouton)
      if (validatedState != BTN_NONE) {
        Serial.printf("BOUTON PRESSE: %d\n", validatedState);
        return validatedState; // Déclenchement unique
      }
    }
  }

  return BTN_NONE; // Rien de nouveau ou aucune pression maintenue ne déclenche
}