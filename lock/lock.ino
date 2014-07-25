#include <Wire.h>
#include "rgb_lcd.h"
#include <Keypad.h>

/*
 * Set up static data
 */

const byte ROWS = 4; //four rows
const byte COLS = 3; //three columns
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {12, 11, 10, 9}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {8, 7, 6}; //connect to the column pinouts of the keypad

const int PIN_LENGTH = 4;
const char RESET_KEY = '*'; // The key that is pressed to reset the PIN entry

/*
 * Set up libraries
 */
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );
rgb_lcd lcd;

/*
 * Begin program
 */

char pin[4];              // The digits of the PIN the user is currently typing
int pinChar = 0;          // The digit of the PIN that the user is up to

void enterPinEntryMode() {
  lcd.setColorWhite();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Enter PIN: ");
  pinChar = 0;
}

void setup() {
    Serial.begin(9600);
  
    // set up the LCD's number of columns and rows:
    lcd.begin(16, 2);
    
    // Turn backlight off
    lcd.setColorAll();
    
    delay(1000);
    // lcd.setRGB(r, g, b);
    
    enterPinEntryMode();
}

void loop() {
    char key = keypad.getKey();
  
    if (key){
      if(key == RESET_KEY) return enterPinEntryMode();
      
      lcd.print('*');
      pin[pinChar++] = key;
      if(pinChar == PIN_LENGTH){
        // Compare PIN with correct PINs
        enterPinEntryMode(); 
      }
      
    }
}

