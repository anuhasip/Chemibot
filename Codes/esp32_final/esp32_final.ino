#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "esp_system.h"

//#include <Arduino.h>

#define RXp2 19
#define TXp2 18

#define LED 2

#define solenoidPin 15

const byte ROWS = 4;  // Four rows
const byte COLS = 4;  // Four columns

char keys[ROWS][COLS] = {
  { '1', '2', '3', '0' },
  { '4', '5', '6', '*' },
  { '7', '8', '9', '#' },
  { '*', '0', '#', 'D' }
};

byte rowPins[ROWS] = { 13, 12, 14, 27 };
byte colPins[COLS] = { 26, 25, 33, 32 };

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

LiquidCrystal_I2C lcd(0x27, 16, 2);

const String correctPassword = "4564";
String inputPassword = "";
bool hasAccess = false;
byte accessTime = 150;

const String ssid = "Wicky";
const String ssid_password = "wicky21816wicky";
const String serverAddress = "https://chemibot-render.onrender.com";
byte wifi_error = 5;

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXp2, TXp2);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  pinMode(solenoidPin, OUTPUT);
  digitalWrite(solenoidPin, HIGH);

  lcd.init();       // Initialize the LCD
  lcd.backlight();  // Turn on the backlight

  // Enable internal pull-up resistors for column pins
  for (byte i = 0; i < COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }

  // Enable internal pull-down resistors for row pins
  for (byte i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], INPUT_PULLDOWN);
  }

  // Set debounce time
  keypad.setDebounceTime(200);  // Adjust debounce time (milliseconds)


  WiFi.begin(ssid, ssid_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    displayMessage("Connecting to", "WiFi...");
  }

  Serial.println("WiFi connected");
  displayMessage("WiFi connected", "");
  delay(1000);

  displayMessage("Please Wait...", "");


  while (true) {
    bool chems_received = getAllChemicals();
    if (chems_received) {
      break;
    }
  }

  displayMessage("Enter Password:", "");
  digitalWrite(LED, LOW);
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    Serial.println(key);
    handleKeypadInput(key);
  }

  if (hasAccess) {
    checkSerial();
    checkChemicalOrder();
    accessTime--;
    if (accessTime <= 0) {
      hasAccess = false;
      accessTime = 150;
      displayMessage("Enter Password:", inputPassword);
    }
  }
}


//check serial from arduino mega
void checkSerial() {
  if (Serial2.available()) {
    String r = Serial2.readStringUntil('\n');
    Serial.println(r);
    r.trim();  // Remove any leading/trailing whitespace, including newline characters
    if (r == "E1") {
      displayMessage("All takeout slots", "Unavailable!");
    } else if(r == "E3") {
      displayMessage("Chemical ", "Unavailable!");
    } else {
      Serial.println("send putchem");
      r = "\"" + r + "\"";
      r = httpPOSTRequest("/putchem", r);
      Serial.println(r);
    }
  }
}

// get all chemicals rfids, rows, cols from the database
bool getAllChemicals() {
  delay(1000);

  String response = httpPOSTRequest("/allchems", "[]");
  Serial.println(response);
  JSONVar myObject = JSON.parse(response);

  if (JSON.typeof(myObject) == "undefined") {
    Serial.println("Parsing input failed!");
    displayMessage("Error", "Check Wifi");
    delay(500);
    wifi_error--;
    if (wifi_error <= 0) {
      esp_restart(); // restart the esp32 when wifi error occured
    }
  } else {
    Serial.print("JSON object = ");
    Serial.println(myObject);
    for (int i = 0; i < myObject.length(); i++) {
      Serial2.println(myObject[i]); // pass all chemical data to arduino mega
      delay(300);
    }
    return true;
    //Serial2.println(myObject);
  }
  return false;
}

// check chemical order is available from the database
void checkChemicalOrder() {
  delay(1000);
  checkSerial();

  String response = httpPOSTRequest("/order", "[]");

  JSONVar myObject = JSON.parse(response);

  if (JSON.typeof(myObject) == "undefined") {
    Serial.println("Parsing input failed!");
    displayMessage("Error", "Check Wifi");
    delay(500);
    wifi_error--;
    if (wifi_error <= 0) {
      esp_restart();
    }
  } else {
    Serial.print("JSON object = ");
    Serial.println(myObject);
    if (myObject.length() > 0) {
      httpPOSTRequest("/order", "[0]");
      String rfidString = JSON.stringify(myObject[0]["rfid"]);
      rfidString = rfidString.substring(1, rfidString.length() - 1); // remove unnecessary double quotes from rfid string
      if (rfidString == "putchem") {
        displayMessage("Putting back", "Chemicals");
        delay(500);
      } else {
        displayMessage("Chemical Order", "Received");
        delay(500);
      }
    }
    for (int i = 0; i < myObject.length(); i++) {
      Serial2.println(myObject[i]); // pass chemical order or putchem to arduino mega
      delay(300);
    }
    //Serial2.println(myObject);
    //displayMessage("Hold # to Unlock", "");
  }
}

// post request to the web server
String httpPOSTRequest(String path, String data) {

  HTTPClient http;
  http.begin(serverAddress + String(path));
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"data\": " + data + "}";
  Serial.println(payload);

  int httpResponseCode = http.POST(payload);
  payload = "[]";

  if (httpResponseCode == 200) {
    payload = http.getString();
    //Serial.println("HTTP Response Code: " + String(httpResponseCode));
    //Serial.println(response);
  } else {
    Serial.println("Error sending data to server");
  }

  http.end();

  return payload;
}

// keyboard 
void handleKeypadInput(char key) {
  if (key == '#') {
    // Confirm password
    if (inputPassword == correctPassword && hasAccess == false) {
      displayMessage("Access Granted", "");
      hasAccess = true;
      unlockSolenoid();
      displayMessage("Hold # to Unlock", "");
    } else if (hasAccess) {
      displayMessage("Access Granted", "");
      unlockSolenoid();
      displayMessage("Hold # to Unlock", "");
    } else {
      displayMessage("Access Denied", "");
      delay(2000);
      displayMessage("Enter Password:", "");
    }

    inputPassword = "";  // Clear input password
  } else if (key == '*') {
    // Backspace
    if (inputPassword.length() > 0) {
      inputPassword.remove(inputPassword.length() - 1);
      displayMessage("Enter Password:", inputPassword);
    }
  } else {
    // Append key to inputPassword
    if (hasAccess == false) {
      inputPassword += key;
      displayMessage("Enter Password:", inputPassword);
    } 
  }
}

// LCD display
void displayMessage(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// Solenoid lock
void unlockSolenoid() {
  Serial.println("Unlocked!");
  digitalWrite(solenoidPin, LOW);   // Activate the solenoid
  delay(5000);                      // Keep it unlocked for 5 seconds
  digitalWrite(solenoidPin, HIGH);  // Lock the solenoid again
  Serial.println("Locked!");
}