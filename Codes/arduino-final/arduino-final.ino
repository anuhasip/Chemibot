#include <Stepper.h>
#include <Servo.h>
#include <Arduino_JSON.h>
#include <SPI.h>
#include <RFID.h>

//pins
#define LAFP A8
#define LABP A9
#define ARB2 A10
#define ARB1 A11
#define LAIN1 22
#define LAIN2 23
#define grabberPin 12
#define StepX1 2
#define DirX1 5
#define StepX2 3
#define DirX2 6
#define StepY 4
#define DirY 7
#define SDA_DIO 53
#define RESET_DIO 49

#define all_chems_size 15
#define stack_chems_size 5

struct chem {
  String rfid;
  byte row;
  byte col;
};

struct ChemIRSensor {
  byte pin;
  bool present;
};

// stack for store ordered chemicals
struct chem stack_chems[stack_chems_size];
byte chem_count = 0;


// array to store all chemicals
struct chem allChems[all_chems_size];

// Define a 3x5 array of ChemSensor structs for the main ir sensors
ChemIRSensor chems_present_grid[3][5] = {
  { { 28, true }, { 29, true }, { 30, true }, { 31, true }, { 32, true } },
  { { 33, true }, { 34, true }, { 35, true }, { 36, true }, { 37, true } },
  { { 38, true }, { 39, true }, { 40, true }, { 41, true }, { 42, true } }
};

// Define a 1x5 array of ChemSensor structs for the takeout ir sensors
ChemIRSensor takeout_present_row[5] = {
  { 43, true }, { 44, true }, { 45, true }, { 46, true }, { 47, true }
};

//int count = 0;
byte XYpos[2] = { 2, 1 };

RFID RC522(SDA_DIO, RESET_DIO);

Stepper arm_rotator(2048, 24, 26, 25, 27);

Servo grabber;

void setup() {
  arm_rotator.setSpeed(10);
  grabber.attach(grabberPin);
  grabber.write(5);

  SPI.begin();
  RC522.init();

  pinMode(ARB1, INPUT);
  pinMode(ARB2, INPUT);

  pinMode(LAFP, INPUT);
  pinMode(LABP, INPUT);
  pinMode(LAIN1, OUTPUT);
  pinMode(LAIN2, OUTPUT);

  pinMode(StepX1, OUTPUT);
  pinMode(DirX1, OUTPUT);
  pinMode(StepY, OUTPUT);
  pinMode(DirY, OUTPUT);
  pinMode(StepX2, OUTPUT);
  pinMode(DirX2, OUTPUT);

  for (byte i = 0; i < 3; i++) {
    for (byte j = 0; j < 5; j++) {
      pinMode(chems_present_grid[i][j].pin, INPUT);
    }
  }

  for (byte i = 0; i < 5; i++) {
    pinMode(takeout_present_row[i].pin, INPUT);
  }

  Serial.begin(9600);
  Serial1.begin(9600);  // Serial communication with ESP32
  Serial.println("get all chems :");
  GoToXYPos(2, 1);
  delay(500);
  //rotatorClockwise();
  
  getAllChems();
  delay(300);
  delay(100);
  Serial.println("begin");
}

void loop() {

  if (chemsAvailable()) {

    printChemStack();

    struct chem selected_chem = popFromChemStack();

    if (selected_chem.rfid == "putchem") {
      // put to chem slots
      PutToChemSlots();
      return;
    }

    if (selected_chem.rfid == "E3") {
      // chemical is not present in the chem grid
      Serial.println("E3");
      return;
    }

    while (checkTakeoutPresentRowAll()) {
      // all takeout slots unavailable
      Serial1.println("E1");
    }

    GoToXYPos(selected_chem.col, selected_chem.row);
    delay(100);

    linearForward();
    delay(100);

    String CardId = "";

    for (byte i = 0; i < 100; i++) {
      CardId = readRFID();
      if (CardId.length() >= 7) {
        break;
      }
      delay(5);
    }

    Serial.println("Selected Chem RFID: " + selected_chem.rfid);
    Serial.println("Slot RFID: " + CardId);

    if (CardId == selected_chem.rfid) {
      grabberGrab();
      delay(100);

      GoToStepYmin(true);
      linearBackward();

      GoToStepYmin(false);
      delay(100);

      if (XYpos[1] != 1) {
        GoToXYPos(XYpos[0], 1);
        delay(100);
      }

      if (XYpos[0] > 2) {
        GoToXYPos(2, XYpos[1]);
        delay(100);
      }

      rotatorAntiClockwise();
      delay(100);

      byte nearesttakeoutcol = nearestTakeoutCol();
      GoToXYPos(nearesttakeoutcol, 1);
      delay(100);

      GoToXYPos(nearesttakeoutcol, 0);
      delay(100);

      grabberRelease();
      delay(100);

      GoToXYPos(nearesttakeoutcol, 1);
      delay(100);

      if (XYpos[0] > 2) {
        GoToXYPos(2, XYpos[1]);
        delay(100);
      }

      rotatorClockwise();
      delay(100);

    } else {
      linearBackward();
    }

    // Serial.print("\n rfid " + selected_chem.rfid);
    // Serial.print("  row ");
    // Serial.print(selected_chem.row);
    // Serial.print("  col ");
    // Serial.print(selected_chem.col);
  } else {
    if (XYpos[0] != 2 || XYpos[1] != 1) {
      GoToXYPos(2, 1);
    }
    getChems();
  }
}

void PutToChemSlots() {
  while (checkTakeoutPresentRowSome() == true) {

    if (XYpos[1] != 1) {
      GoToXYPos(XYpos[0], 1);
      delay(100);
    }

    if (XYpos[0] > 2) {
      GoToXYPos(2, XYpos[1]);
      delay(100);
    }

    byte nearest_put_col = nearestPutCol();

    rotatorAntiClockwise();
    delay(100);

    GoToXYPos(nearest_put_col, 1);
    delay(100);

    GoToXYPos(nearest_put_col, 0);
    delay(100);

    struct chem choosen_chem;
    choosen_chem.rfid = "";

    for (byte i = 0; i < 100; i++) {
      choosen_chem.rfid = readRFID();
      if (choosen_chem.rfid.length() >= 7) {
        break;
      }
      delay(5);
    }

    Serial.println("Chem RFID: " + choosen_chem.rfid);

    bool available = true;
    for (byte i = 0; i < all_chems_size; i++) { // check rfid and get the desired chemgrid position
      if (allChems[i].rfid == choosen_chem.rfid) {
        choosen_chem.row = allChems[i].row;
        choosen_chem.col = allChems[i].col;
        available = true;
        break;
      }
      available = false;
    }

    // if chemival bottle not present in the desired takeout slot or if there is a chemical stored in desired chemgrid position
    if (available == false || checkChemPresentGrid(choosen_chem.col, choosen_chem.row)) {
      GoToXYPos(nearest_put_col, 1);
      delay(100);
      if (XYpos[0] > 2) {
        GoToXYPos(2, XYpos[1]);
        delay(100);
      }
      rotatorClockwise();
      delay(100);
      return ;
    }

    Serial1.println(choosen_chem.rfid); // send request to esp32 to update chemical availability
    delay(100);

    grabberGrab();

    GoToXYPos(nearest_put_col, 1);
    delay(100);

    if (XYpos[0] > 2) {
      GoToXYPos(2, XYpos[1]);
      delay(100);
    }

    rotatorClockwise();
    delay(100);

    GoToXYPos(choosen_chem.col, choosen_chem.row);
    delay(100);

    GoToStepYmin(true);
    linearForward();
    GoToStepYmin(false);
    delay(100);
    grabberRelease();
    linearBackward();
    delay(100);
    if (XYpos[1] == 0) {
      GoToXYPos(XYpos[0], 1);
      delay(100);
    }
  }
  delay(100);
}

//linear arm controlling

void linearForward() {
  while (analogRead(LAFP) <= 200) {
    digitalWrite(LAIN1, LOW);
    digitalWrite(LAIN2, HIGH);
    delay(100);
  }
  linearStop();
}

void linearBackward() {
  while (analogRead(LABP) <= 200) {
    digitalWrite(LAIN1, HIGH);
    digitalWrite(LAIN2, LOW);
    delay(100);
  }
  linearStop();
}

void linearStop() {
  digitalWrite(LAIN1, LOW);
  digitalWrite(LAIN2, LOW);
}

//grabber controlling

void grabberGrab() {
  for (byte i = 5; i <= 60; i++) {  // goes from 120 degrees to 180 degrees
    grabber.write(i);
    delay(10);
  }
}

void grabberRelease() {
  for (byte i = 60; i >= 5; i--) {  // goes from 180 degrees to 120 degrees
    grabber.write(i);
    delay(10);
  }
}

//grabber arm controlling

void rotatorClockwise() {
  for (int i = 0; i < 2048; i++) {
    arm_rotator.step(1);  // Step one step
    //delay(5);             // Adjust delay for desired speed
    if (analogRead(ARB1) >= 200) {
      break;
    }
  }
}

void rotatorAntiClockwise() {
  for (int i = 0; i < 2048; i++) {
    arm_rotator.step(-1);  // Step one step
    //delay(5);              // Adjust delay for desired speed
    if (analogRead(ARB2) >= 200) {
      break;
    }
  }
}

//move in X and Y axis

void GoToXYPos(byte pX, byte pY) {
  digitalWrite(DirX1, (XYpos[0] < pX));  //true->right false->left
  digitalWrite(DirX2, !(XYpos[0] < pX));
  digitalWrite(DirY, (XYpos[1] < pY));  //true->up false->down
  for (byte i = 0; i < abs(pX - XYpos[0]); i++) {
    GoToStepX();
  }
  for (byte i = 0; i < abs(pY - XYpos[1]); i++) {
    GoToStepY();
    //delay(100);
  }
  XYpos[0] = pX;
  XYpos[1] = pY;
}

void GoToStepX() {
  for (int x = 0; x < 280; x++) {  // loop for 280 steps
    digitalWrite(StepX1, HIGH);
    digitalWrite(StepX2, HIGH);
    delayMicroseconds(1500);
    digitalWrite(StepX1, LOW);
    digitalWrite(StepX2, LOW);
    delayMicroseconds(1500);
  }
}

void GoToStepY() {
  for (int x = 0; x < 4500; x++) {  // loop for 4500 steps
    digitalWrite(StepY, HIGH);
    delayMicroseconds(1500);
    digitalWrite(StepY, LOW);
    delayMicroseconds(1500);
  }
}

void GoToStepYmin(bool direction) { // true for up, false for down
  digitalWrite(DirY, direction);
  for (int x = 0; x < 300; x++) {  // loop for 300 steps
    digitalWrite(StepY, HIGH);
    delayMicroseconds(1500);
    digitalWrite(StepY, LOW);
    delayMicroseconds(1500);
  }
}

//get chem data from ESP32

void getAllChems() {
  byte chemCount = 0;
  while (chemCount < all_chems_size) {
    delay(200);
    if (Serial1.available()) {
      String r = Serial1.readStringUntil('\n');

      JSONVar res = JSON.parse(r);

      if (JSON.typeof(res) == "undefined") {
        Serial.println("Parsing input failed!");
      } else {
        struct chem temp;
        String rfidString = JSON.stringify(res["rfid"]);
        rfidString = rfidString.substring(1, rfidString.length() - 1);
        temp.rfid = rfidString;
        temp.row = byte(res["row"]) - 1;
        temp.col = byte(res["col"]) - 1;
        if (byte(res["row"]) < 0 || byte(res["col"]) < 0) {
          Serial.println("Invalid chem pos!");
          continue;
        }
        allChems[chemCount] = temp;
        chemCount++;
        Serial.print("\nChem Received: ");
        Serial.println(r);
      }
    }
  }
  for (byte i = 0; i < all_chems_size; i++) {
    Serial.print("rfid: ");
    Serial.print(allChems[i].rfid);
    Serial.print("  row: ");
    Serial.print(allChems[i].row);
    Serial.print("  col: ");
    Serial.println(allChems[i].col);
  }
  Serial.println("__________________\n");
}

bool chemsAvailable() {
  return (chem_count > 0);
}

void getChems() {
  bool available = getChem();
  if (available) {
    for (int i = 0; i < 10; i++) {
      getChem();
    }
  }
  //Serial.println("try to get chems");
}

bool getChem() {
  delay(200);
  if (Serial1.available()) {
    String r = Serial1.readStringUntil('\n');
    pushToChemStack(r);
    Serial.print("\nMessage Received: ");
    Serial.println(r);
    return true;
  }
  return false;
}

void pushToChemStack(String x) {
  if (chem_count >= stack_chems_size) {
    Serial.println("There is no space available in the stack!");
    return;
  }
  JSONVar res = JSON.parse(x);

  if (JSON.typeof(res) == "undefined") {
    Serial.println("Parsing input failed!");
    return;
  } else {
    struct chem temp;
    String rfidString = JSON.stringify(res["rfid"]);
    rfidString = rfidString.substring(1, rfidString.length() - 1); // remove unnecessary double quotes 
    temp.rfid = rfidString;
    temp.row = byte(res["row"]) - 1;
    temp.col = byte(res["col"]) - 1;
    if (byte(res["row"]) < 0 || byte(res["col"]) < 0) {
      Serial.println("Invalid chem pos!");
      return;
    }
    stack_chems[chem_count].rfid = temp.rfid;
    stack_chems[chem_count].row = temp.row;
    stack_chems[chem_count].col = temp.col;
    chem_count++;
  }
}

struct chem popFromChemStack() {
  struct chem x;
  if (chem_count <= 0) {
    Serial.println("Nothing to take from stack! : ");
    x = { "E2", 1, 2 };
    return x;
  }
  chem_count--;
  x = stack_chems[chem_count];
  if (x.rfid == "putchem") {
    return x;
  }
  if (checkChemPresentGrid(x.col, x.row) == false) {
    x = { "E3", 1, 2 }; // chemical not present in desired grid position
    return x;
  }
  return x;
}

void printChemStack() {
  for (int i = 0; i < chem_count; i++) {
    Serial.print("rfid: ");
    Serial.print(stack_chems[i].rfid);
    Serial.print("  row: ");
    Serial.print(stack_chems[i].row);
    Serial.print("  col: ");
    Serial.println(stack_chems[i].col);
    delay(5);
  }
}

// RFID reader

String readRFID() {
  /* Has a card been detected? */
  String cardID = "";
  if (RC522.isCard()) {
    /* If so then get its serial number */
    RC522.readCardSerial();
    Serial.print("Card detected:");
    for (int i = 0; i < 5; i++) {
      cardID += String(RC522.serNum[i], HEX);
    }
    Serial.println(cardID);
  }
  return cardID;
}

// IR

bool checkChemPresentGrid(byte col, byte row) {
  chems_present_grid[row][col].present = !digitalRead(chems_present_grid[row][col].pin);
  return chems_present_grid[row][col].present;
}

bool checkTakeoutPresentRow(byte col) {
  takeout_present_row[col].present = !digitalRead(takeout_present_row[col].pin);
  return takeout_present_row[col].present;  // true - present, false - not present
}

bool checkTakeoutPresentRowAll() {
  for (byte i = 0; i < 5; i++) {
    if (checkTakeoutPresentRow(i) == false) {
      return false;
    }
  }
  return true;
}

bool checkTakeoutPresentRowSome() {
  for (byte i = 0; i < 5; i++) {
    if (checkTakeoutPresentRow(i) == true) {
      return true;
    }
  }
  return false;
}

// Intelligent features

byte nearestTakeoutCol() {
  if (checkTakeoutPresentRow(XYpos[0]) == false) {
    return XYpos[0];
  }
  byte distanceToNearestCol = 4;
  byte nearestCol = 0;
  for (byte i = 0; i < 5; i++) {
    if (checkTakeoutPresentRow(i) == false && (abs(i - XYpos[0]) < distanceToNearestCol)) {
      nearestCol = i;
      distanceToNearestCol = abs(i - XYpos[0]);
    }
  }
  return nearestCol;
}

byte nearestPutCol() {
  if (checkTakeoutPresentRow(XYpos[0]) == true) {
    return XYpos[0];
  }
  byte distanceToNearestCol = 4;
  byte nearestCol = 0;
  for (byte i = 0; i < 5; i++) {
    if (checkTakeoutPresentRow(i) == true && (abs(i - XYpos[0]) < distanceToNearestCol)) {
      nearestCol = i;
      distanceToNearestCol = abs(i - XYpos[0]);
    }
  }
  return nearestCol;
}

void nearestChemSlot() {
  struct chem temp;
  for (byte i = 0; i < chem_count - 1; i++) {
    if (stack_chems[i].row == 1) {
      if (stack_chems[chem_count - 1].row == 1) {
        if (abs(stack_chems[i].col - XYpos[0]) < abs(stack_chems[chem_count - 1].col - XYpos[0])) {
          temp = stack_chems[i];
          stack_chems[chem_count - 1] = stack_chems[i];
          stack_chems[i] = temp;
        }
      } else {
        temp = stack_chems[i];
        stack_chems[chem_count - 1] = stack_chems[i];
        stack_chems[i] = temp;
      }
    } else {
      if (stack_chems[chem_count - 1].row != 1) {
        if (abs(stack_chems[i].col - XYpos[0]) < abs(stack_chems[chem_count - 1].col - XYpos[0])) {
          temp = stack_chems[i];
          stack_chems[chem_count - 1] = stack_chems[i];
          stack_chems[i] = temp;
        }
      }
    }
  }
}