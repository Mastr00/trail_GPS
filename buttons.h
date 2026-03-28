#ifndef BUTTONS_H
#define BUTTONS_H

// Codes originaux de l'enum
#define BTN_NONE          0
#define BTN_UP_PRESSED    1
#define BTN_DOWN_PRESSED  2
#define BTN_LEFT_PRESSED  3
#define BTN_RIGHT_PRESSED 4
#define BTN_MID_PRESSED   5
#define BTN_SET_PRESSED   6
#define BTN_RST_PRESSED   7

void initButtons();

// Renvoie BTN_NONE s'il n'y a pas eu d'appui, 
// sinon renvoie le code du bouton relâché (ou pressé)
int handleButtons();

#endif