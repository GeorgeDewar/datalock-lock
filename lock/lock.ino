#include <Wire.h>
#include "rgb_lcd.h"
#include <Keypad.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include "Debug.h"
#include <WiFly.h>
#include "WiflyHTTPClient.h"

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

#define SSID      "Door-A-Com"
#define KEY       "qwerty0987"
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
const char    CORRECT_PIN[] = {'1','2','3','4'};

const char    BT_END_CHAR   = '\n';
const int     BT_MAX_BYTES  = 32;

const int     HTTP_MAX_BYTES = 32;

/*
 * Begin program
 */

char pin[4];              // The digits of the PIN the user is currently typing
int pinChar = 0;          // The digit of the PIN that the user is up to

char bt_buffer[BT_MAX_BYTES]; // The buffer used to store a received message from a phone
char http_buffer[HTTP_MAX_BYTES]; // The buffer used to store a received message over HTTP

void setup() {
    Serial.begin(9600);
    wifly_uart.begin(9600);
  
    pinMode(DOOR_STRIKE_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    
    lcd.begin(16, 2);     // set up the LCD's number of columns and rows:
    lcd.setColorAll();    // Turn backlight off
    
    joinNetwork();   
    enterPinEntryMode();
    
}

void joinNetwork(){
  if (!wifly.isAssociated(SSID)) {
    Serial.println("Connecting");
    while (!wifly.join(SSID, KEY, AUTH)) {
      Serial.println("Failed to join " SSID);
      Serial.println("Wait 0.1 second and try again...");
      delay(100);
    }
    Serial.println("WiFi Connected");
    
    wifly.save();    // save configuration, 
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

void checkForRemoteMessage() {
    
    Serial.println("\r\nTry to get url - " HTTP_GET_URL);
    Serial.println("------------------------------");
    
    while (http.get(HTTP_GET_URL, 2000) < 0) { // wait 
    }
    
    char c;
    
    // Read response code
    int responseCode = getResponseCode(wifly);
    if(responseCode == 204){
      Serial.println(" No content");
      return;
    }
    else if(responseCode != 200){
      Serial.println(" Error" + responseCode);
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
  
    //Serial.println("\r\n\r\nTry to post data to url - " HTTP_POST_URL);
    //Serial.println("-------------------------------");
    //while (http.post(HTTP_POST_URL, new char[3] {'a','b','c'}, 10000) < 0) {
    //}
    //while (wifly.receive((uint8_t *)&c, 1, 1000) == 1) {
      //Serial.print(get);
    //}
}

void loop() {
    checkForKey();
    checkForRemoteMessage();
    
}

