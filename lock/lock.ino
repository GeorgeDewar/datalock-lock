#include <Wire.h>
#include "rgb_lcd.h"
#include <Keypad.h>

/*
 * Hardware configuration
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

const int DOOR_STRIKE_PIN   = 2;
const int PIR_PIN           = 3;

/*
 * Set up libraries
 */
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );
rgb_lcd lcd;

/*
 * Static data
 */
const int     PIN_LENGTH    = 4;
const char    RESET_KEY     = '*'; // The key that is pressed to reset the PIN entry
const char    CORRECT_PIN[] = {'1','2','3','4'};

const char    BT_END_CHAR   = '\n';
const int     BT_MAX_BYTES  = 32;

/*
 * Begin program
 */

char pin[4];              // The digits of the PIN the user is currently typing
int pinChar = 0;          // The digit of the PIN that the user is up to

char bt_buffer[BT_MAX_BYTES]; // The buffer used to store a received message

void setup() {
    Serial.begin(57600);
  
    pinMode(DOOR_STRIKE_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    
    lcd.begin(16, 2);     // set up the LCD's number of columns and rows:
    lcd.setColorAll();    // Turn backlight off
       
    enterPinEntryMode();
}

void clearAndPrint(String text){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(text);
}

void enterPinEntryMode() {
  lcd.setColorWhite();
  clearAndPrint("Enter PIN: ");
  pinChar = 0;
}

boolean checkPin(){
  for(int i=0; i<PIN_LENGTH; i++){
    if(pin[i] != CORRECT_PIN[i]) return false;
  }
  return true;
}

/* See if a key has been pressed, and deal with it if so */
void checkForKey(){
    char key = keypad.getKey();
    if (key){
      if(key == RESET_KEY) return enterPinEntryMode();
      
      lcd.print('*');
      pin[pinChar++] = key;
      if(pinChar == PIN_LENGTH){
        // Compare PIN with correct PINs
        if(checkPin()){
          lcd.setRGB(0,255,0);
          clearAndPrint("Welcome, friend :-)");
          digitalWrite(DOOR_STRIKE_PIN, HIGH);
          delay(1000);
          digitalWrite(DOOR_STRIKE_PIN, LOW);
          return enterPinEntryMode();
        }
        else{
          lcd.setRGB(255,0,0);
          clearAndPrint("Incorrect PIN!");
          delay(500);
          return enterPinEntryMode();
        }
      }
    }
}

void loop() {
    checkForKey();
    if(Serial.available() > 0){
      // We have a message
      int len = Serial.readBytesUntil(BT_END_CHAR, bt_buffer, BT_MAX_BYTES);
      // Get ID, message
      char id[2] = {bt_buffer[0], bt_buffer[1]};
      
    }
}

