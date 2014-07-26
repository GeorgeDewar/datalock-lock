#include <Wire.h>
#include "rgb_lcd.h"
#include <Keypad.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <WiFly.h>
#include "WiflyHTTPClient.h"
#include <EEPROM.h>
#include "Seeed_QTouch.h"

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
const int WIFLY_RX_PIN      = 4;
const int WIFLY_TX_PIN      = 5;
const int INDOOR_BUTTON     = 14;

const int TOUCH_OUTDOOR_PIN = 3;

#define SSID      "DATACOMP"
#define KEY       "DataC0mp2014!"
#define AUTH      WIFLY_AUTH_WPA2_PSK

#define HTTP_GET_URL "http://172.26.75.139:3000/messages/poll"
#define HTTP_POST_URL "http://172.26.75.139:3000/messages/confirm"

/*
 * Set up libraries
 */
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );
rgb_lcd lcd;
SoftwareSerial wifly_uart(WIFLY_RX_PIN, WIFLY_TX_PIN);
WiFly wifly(wifly_uart);
HTTPClient http;

/*
 * Static data
 */
const int     PIN_LENGTH    = 4;
const char    RESET_KEY     = '*'; // The key that is pressed to reset the PIN entry
char          CORRECT_PIN[] = {'1','2','3','4'};
const char    MAX_PIN_TRIES = 3;
const int     LOCKOUT_TIME  = 10000;

const char    HTTP_MAX_BYTES = 32;
const char    MAX_USERS      = 8;
const int     UNLOCK_TIME    = 10000;
const int     POLL_FREQUENCY = 5000;

const int     MODE_PIN       = 0;
const int     MODE_OPEN      = 1;

/*
 * Begin program
 */

char pin[4];              // The digits of the PIN the user is currently typing
char pinChar = 0;         // The digit of the PIN that the user is up to
char pinTries = 0;        // The number of attempts the user has made to enter the correct PIN

char http_buffer[HTTP_MAX_BYTES]; // The buffer used to store a received message over HTTP

unsigned long timeSinceLastPoll = 0;
unsigned long unlocked_at       = 0;

char cursorX = 0;
char cursorY = 0;

char mode = MODE_PIN;

void setup() {
    Serial.begin(9600);
    wifly_uart.begin(9600);
  
    pinMode(DOOR_STRIKE_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    pinMode(INDOOR_BUTTON, INPUT);
    
    lcd.begin(16, 2);     // set up the LCD's number of columns and rows:
    lcd.setColorAll();    // Turn backlight off
    
    QTouch.calibrate();
    
    joinNetwork();   
    enterPinEntryMode();
    
    if(EEPROM.read(511) != '1'){
      // Initialize EEPROM
      for(int i=0; i<511; i++){
        EEPROM.write(i, 0); 
      }
      EEPROM.write(511, '1');
    }
}

void joinNetwork(){
  if (!wifly.isAssociated(SSID)) {
    Serial.println("WiFi Connecting");
    while (!wifly.join(SSID, KEY, AUTH)) {
      Serial.println("Failed");
      delay(100);
    }
    Serial.println("OK");
    
    wifly.save();    // save configuration
  }
}

void leaveNetwork(){
  wifly.leave(); 
}

void clearAndPrint(String text){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(text);
}

void enterPinEntryMode() {
  lcd.setColorWhite();
  clearAndPrint("Enter PIN: ");
  setCursor(0,1);
  pinChar = 0;
  mode = MODE_PIN;
}

boolean checkPin(char supplied_pin[], char correct_pin[]){
  Serial.print("Entered PIN: ");
  for(int i=0; i<PIN_LENGTH; i++){
    Serial.print(supplied_pin[i]);
  }
  Serial.print(" ");
  for(int i=0; i<PIN_LENGTH; i++){
    Serial.print(correct_pin[i]);
  }
  Serial.println();
  
  for(int i=0; i<PIN_LENGTH; i++){
    if(supplied_pin[i] != correct_pin[i]) return false;
  }
}

int checkPin(){
  char user_pin[4];
  for(int i=0; i<MAX_USERS; i++){
    if(!userExists(i)) continue;
    for(int j=0; j<PIN_LENGTH; j++){
      user_pin[j] = userRead(i, 3 + j);
    }
    if(checkPin(pin, user_pin)) return i;
  }
  if(checkPin(pin, CORRECT_PIN)) return -1; // Admin PIN
  return -2; // No match
}

void unlockDoor(String message){
  lcd.setRGB(0,255,0);
  clearAndPrint(message);
  mode = MODE_OPEN;
  unlocked_at = millis();
}

void relockDoor() {
  Serial.println("Relock");
  enterPinEntryMode(); 
  digitalWrite(DOOR_STRIKE_PIN, LOW);
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
        int slot = checkPin();
        Serial.print(slot, DEC);
        if(slot >= -1){
          lcd.setRGB(0,255,0);
          String user;
          if(slot == -1) {
            Serial.println("Admin");
            user = "Admin";
          }
          else{
            user = userName(slot); 
          }
          return unlockDoor("Welcome, " + user + "!");
        }
        else{
          lcd.setRGB(255,0,0);
          clearAndPrint("Incorrect PIN!");
          delay(2000);
          pinTries++;
          
          if(pinTries == MAX_PIN_TRIES){
             clearAndPrint(" GET AWAY THIEF");
             setCursor(0,1);
             lcd.print(" OWNER NOTIFIED");
             delay(LOCKOUT_TIME);
             pinTries = 0;
          }
          
          return enterPinEntryMode();
        }
      }
    }
}

int getResponseCode(WiFly wifly){
  int index = 0;
  char c;
  char code[3];
  while (wifly.receive((uint8_t *)&c, 1, 1000) == 1) {
    if(index++ < 9) continue;
    code[index-10] = c;
    Serial.print(c);
    if(index > 11) break;
  }
  return (100 * (code[0] - '0') + 10 * (code[1] - '0') + 1 * (code[2] - '0'));
}

void getPastHeaders(WiFly wifly){
  int newLineCount = 0;
  char c;
  while (wifly.receive((uint8_t *)&c, 1, 1000) == 1) {
    if(c == '\n' || c == '\r') {
      newLineCount++;
    }
    else {
      newLineCount = 0;
    }
    if(newLineCount == 4) break;
  }
}

char userRead(int slot, int index){
  return EEPROM.read(slot * 32 + index);
}

boolean userExists(int slot){
  return userRead(slot, 0) == '1';
}

String userName(int slot){
  char str[16];
  int i = 0;
  for(i=0; i<16; i++){
    char c = userRead(slot, i + 7);
    Serial.print(c);
    if(c == '#') break;
    str[i] = c;
  }
  str[i] = 0;
  return String(str);
}

int findFreeUserAddress() {
  for(int i=0; i<MAX_USERS; i++){
    Serial.print(i, DEC);
    Serial.println(userExists(i), DEC);
    if(!userExists(i)) return i;
  }
  return -1;
}

int findUser(String id){
  Serial.println("Looking for user" + id);
  for(int i=0; i<MAX_USERS; i++){
    Serial.print(i, DEC);
    Serial.print(userExists(i), DEC);
    if(userExists(i)){
      Serial.print((char) EEPROM.read(i * 32 + 1));
      Serial.print((char) EEPROM.read(i * 32 + 2));
      if(EEPROM.read(i * 32 + 1) == id.charAt(0) && EEPROM.read(i * 32 + 2) == id.charAt(1)) return i;
    }
    
  }
  return -1;
}

void writeUserData(int slot, String payload){
  Serial.print("Slot ");
  Serial.println(slot, DEC);
  for(int i=0; i<payload.length(); i++){
    Serial.write(payload.charAt(i));
    EEPROM.write(slot * 32 + i + 1, payload.charAt(i));
  }
  EEPROM.write(slot * 32, '1');
}

void debugChar(char chr){
  lcd.setCursor(15,1); 
  lcd.write(chr);
  lcd.setCursor(cursorX, cursorY);
}

void setCursor(char x, char y){
  lcd.setCursor(x, y);
  cursorX = x;
  cursorY = y;
}

void checkForRemoteMessage() {
    
    Serial.println("Polling");
    debugChar('P');
    timeSinceLastPoll = millis();
    
    while (http.get(HTTP_GET_URL, 2000) < 0) { // wait 
    }
    
    char c;
    
    // Read response code
    int responseCode = getResponseCode(wifly);
    if(responseCode == 204){
      Serial.println();
      debugChar(' ');
      return;
    }
    else if(responseCode != 200){
      Serial.println();
      debugChar(' ');
      return;
    }
    else {
      Serial.println(" OK"); 
      debugChar(' ');
    }
    
    // Get past headers
    getPastHeaders(wifly);
    
    // Read actual content
    int index = 0;
    while (wifly.receive((uint8_t *)&c, 1, 1000) == 1) {
      //Serial.print(c);
      http_buffer[index++] = c;
    }
  
    http_buffer[index] = 0;
    String response = String(http_buffer);
    Serial.println(response);

    String id = response.substring(0, 2);
    String command = response.substring(2, 5);
    Serial.println(id + " " + command);
    Serial.println(command.length(), DEC);
    
    if(command.equals("UNL")){
      unlockDoor("Remote Unlock");
    }
    else if(command.equals("USR")){
      String payload = response.substring(5);
      String userId = payload.substring(0, 2);
      int slot = findUser(userId);
      if(slot >= 0){
        Serial.println("Updating user");
        // Copy to EEPROM
        writeUserData(slot, payload);
      }
      else{
        Serial.println("Adding user");
        slot = findFreeUserAddress();
        if(slot >= 0){
          // Copy to EEPROM
          writeUserData(slot, payload);
        }
        else{
          Serial.println("No space");
          return;
        }
      }
    }
    else{
      Serial.println("Invalid CMD: " + command);
      return;
    }
    
    Serial.println("ACK " + id);
    while (http.post(HTTP_POST_URL, new char[2] {id.charAt(0), id.charAt(1)}, 10000) < 0) {
    }
  

}

void checkForButton(){
  if(digitalRead(INDOOR_BUTTON)) unlockDoor("Inside"); 
}

void checkForTouch(){
   digitalWrite(DOOR_STRIKE_PIN, QTouch.isTouch(TOUCH_OUTDOOR_PIN)); 
}

void loop() {
    if(mode == MODE_OPEN){
      checkForTouch(); 
      if(millis() - unlocked_at > UNLOCK_TIME) relockDoor();
    }
    else{
      checkForKey();
      checkForButton();
      if((millis() - timeSinceLastPoll > POLL_FREQUENCY) && (pinChar == 0)) checkForRemoteMessage(); // only if not typing in PIN
    
    }
}

