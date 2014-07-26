#include <Wire.h>
#include "rgb_lcd.h"
#include <Keypad.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "Debug.h"
#include <WiFly.h>
#include "WiflyHTTPClient.h"
#include <EEPROM.h>

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
char    CORRECT_PIN[] = {'1','2','3','4'};

const char    BT_END_CHAR   = '\n';
const int     BT_MAX_BYTES  = 8;

const int     HTTP_MAX_BYTES = 32;

const int     MAX_USERS = 8;

/*
 * Begin program
 */

char pin[4];              // The digits of the PIN the user is currently typing
int pinChar = 0;          // The digit of the PIN that the user is up to

char bt_buffer[BT_MAX_BYTES]; // The buffer used to store a received message from a phone
char http_buffer[HTTP_MAX_BYTES]; // The buffer used to store a received message over HTTP

unsigned long timeSinceLastPoll = 0;

void setup() {
    Serial.begin(9600);
    wifly_uart.begin(9600);
  
    pinMode(DOOR_STRIKE_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    
    lcd.begin(16, 2);     // set up the LCD's number of columns and rows:
    lcd.setColorAll();    // Turn backlight off
    
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
  pinChar = 0;
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

boolean checkPin(){
  char user_pin[4];
  for(int i=0; i<MAX_USERS; i++){
    if(!userExists(i)) continue;
    for(int j=0; j<PIN_LENGTH; j++){
      user_pin[j] = userRead(i, 3 + j);
    }
    if(checkPin(pin, user_pin)) return true;
  }
  return checkPin(pin, CORRECT_PIN);
}

void unlockDoor(){
  digitalWrite(DOOR_STRIKE_PIN, HIGH);
  delay(2000);
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
        if(checkPin()){
          lcd.setRGB(0,255,0);
          clearAndPrint("Welcome, friend :-)");
          unlockDoor();
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

int findFreeUserAddress() {
  for(int i=0; i<MAX_USERS; i++){
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

void checkForRemoteMessage() {
    
    Serial.println("Polling");
    timeSinceLastPoll = millis();
    
    while (http.get(HTTP_GET_URL, 2000) < 0) { // wait 
    }
    
    char c;
    
    // Read response code
    int responseCode = getResponseCode(wifly);
    if(responseCode == 204){
      Serial.println();
      return;
    }
    else if(responseCode != 200){
      Serial.println();
      return;
    }
    else {
      Serial.println(" OK"); 
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
      unlockDoor();
    }
    else if(command.equals("USR")){
      String payload = response.substring(5);
      String userId = payload.substring(0, 2);
      int slot;
      if(slot = findUser(userId) >= 0){
        Serial.println("Updating user");
        // Copy to EEPROM
        for(int i=0; i<payload.length(); i++){
          Serial.write(payload.charAt(i));
          EEPROM.write(slot * 32 + i + 1, payload.charAt(i));
        }
        EEPROM.write(slot * 32, '1');
      }
      else{
        Serial.println("Adding user");
        
        if(slot = findFreeUserAddress() >= 0){
          // Copy to EEPROM
          Serial.print("Slot ");
          Serial.println(slot, DEC);
          for(int i=0; i<payload.length(); i++){
            Serial.write(payload.charAt(i));
            EEPROM.write(slot * 32 + i + 1, payload.charAt(i));
          }
          EEPROM.write(slot * 32, '1');
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

void loop() {
    checkForKey();
    if(millis() - timeSinceLastPoll > 5000) checkForRemoteMessage();
    
}

