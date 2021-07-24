/*
Arduino code for running the sample collection from a chromatography process with a single collector.
Instructions set via serial connection
*/

// Include libraries
#include <Stepper.h> // For driving stepper motor (28byj-48)
#include <Servo.h>  // For driving the microservo (SG90)
//# include <Wire.h> // For I2C communication - not needed for single device

// Define pins - these are meant to work with an Arduino Nano, but should also work with an Arduino Uno
const byte tubeSensor = 2; // Level sensor in eppi tubes
const byte servoPin = 3; // Servo 
const byte wasteSensor = 4; // Liquid sensor for waste funnel
const byte pump1 = 5; // Pump#1
const byte pump2 = 6; // Pump #2
const byte dockingSensor = 7; // For docking the distributor to the device - not needed for single device
const byte homeSensor = 8; // For homing the revolving plate (hall effect sensor A3144)
const byte motPins[] = {9, 10, 11, 12}; // Pins for stepper motor

// Declare variables 

/* Constant properties of the harware */
#define STEPS  32   // Number of steps per revolution of internal shaft of a 28byj-48 stepper
const float angleTubes = 23.8; // angular separation between tubes in degrees
const float angleWaste = 43.3; // angle between the center of the waste collector and the center of the first tube
volatile byte nTubes = 5; // Number of tubes in the sampler - default 5, bu can be modified
// Other variables
byte sensorValue; // Value of the level sensor attached to the servo
byte wasteValue = HIGH; // Value of the liquid sensor in waste collector - default HIGH = not triggered
int numWashes = 1; //Number of washes

int nSteps;  // 2048 = 1 Revolution

bool washDone; // Logical value indicating whether the wash solution has completely left the column
unsigned long timeOff = 0; // time that the waste collector is empty
unsigned long currentMillis; // current time in milliseconds
const unsigned long timeEmpty = 20000; // time in milliseconds the waste container must be empty before accepting wash is done
// TO DO: Add variables given by serial port as volatile. Another version of the firmware called "stand alone" might be needed that has pre-set arguments to not use the PC

// Variables for parsing serial communication
const byte numChars = 32; // this value could be smaller or larger, as long as it fits the largest expected message
char receivedChars[numChars];
char tempChars[numChars];        // temporary array for use when parsing
// variables to hold the parsed data
char messageFromPC[numChars] = {0};
int ledInstruction = 0;
boolean newData = false;
int args[4] = {0}; // arguments for executing the functions .. gotta get the actual maximum number

// Define objects
// Setup of proper sequencing for Motor Driver Pins: In1, In2, In3, In4 in the sequence 1-3-2-4
Stepper plateStepper(STEPS, motPins[0], motPins[2], motPins[1], motPins[3]);
Servo   levelServo;

/* Run this program once*/
void setup(){

  // ========== Setup pins and monitor ========================================
  // Start serial monitor
  Serial.begin(9600);
  //Serial.println("Serial collection of chromatography fractions");

  // Define pin modes
  pinMode(tubeSensor, INPUT_PULLUP);
  pinMode(wasteSensor, INPUT_PULLUP);
  pinMode(pump1, OUTPUT);
  pinMode(pump2, OUTPUT);
  pinMode(dockingSensor, INPUT_PULLUP);
  pinMode(homeSensor, INPUT_PULLUP);

  // Turn off motor coils
  for (int i; i<4; i++){ digitalWrite(motPins[i], LOW);}
  plateStepper.setSpeed(700);
  // Attach servo and raise for homing
  levelServo.attach(servoPin);
  levelServo.write(90);

  // Run protocol

  //Serial.println("Filling tubes");

  Serial.println("ready");
  //homePlate();
  //fillTubes();
}

/* Don't need no loops */
void loop(){

  // Monitor the serial port - the loop will scan the characters as they come, even if the full
  // message doesn't arrive during a single iteration of the loop
  recvWithStartEndMarkers();
    if (newData == true) {
        newData = false;
        // This temporary copy is necessary to protect the original data
        // because strtok() used in parseData() replaces the commas with \0
        strcpy(tempChars, receivedChars);
        // Parse the message: Split into its components     
        parseCommand();
        // Execute the data: Called the necessary function
        executeCommand();  

        
    }
  
  
}

// ====================================================
// Functions for serial communication

void recvWithStartEndMarkers() {
    static boolean recvInProgress = false;
    static byte idx = 0;
    char startMarker = '<';
    char endMarker = '>';
    char rc;

    while (Serial.available() > 0 && newData == false) {
        rc = Serial.read();

        if (recvInProgress == true) {
            if (rc != endMarker) {
                receivedChars[idx] = rc;
                idx++;
                if (idx >= numChars) { // this if clause makes sure we don't exceed the max size of message, and starts overwriting the last character
                    idx = numChars - 1;
                }
            }
            else {
                receivedChars[idx] = '\0'; // terminate the string
                recvInProgress = false;
                idx = 0;
                newData = true;
            }
        }

        else if (rc == startMarker) {
            recvInProgress = true;
        }
    }
}

//============

void parseCommand() {      // split the command into its parts
    char * strtokIndx; // this is used by strtok() as an index

    strtokIndx = strtok(tempChars,",");      // get the first part
    strcpy(messageFromPC, strtokIndx); // copy it to ledAddress as a string
    Serial.println(messageFromPC);

    // Scan the other arguments - TO DO: fix, we assume for now that all are integers and up to 4
    for (int i = 0; i < 4; i++){
      strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
      args[i] = atoi(strtokIndx);     // convert this part to an integer
      Serial.println(args[i]);
    }
    
    
}

void executeCommand(){
  // Call the required command 
  // (pegassu firmware https://github.com/pachterlab/pegasus/blob/master/firmware/motor_serial_com/motor_serial_com.ino does something different)

  // I am using aquick hcak to use the switch since it needs a char, so I am getting the first letter of the message. This works but maybe not ellegant
  // and we'll see if all commands have different letters
    char firstLetter = messageFromPC[0];
    switch (firstLetter){
    case 'H':
      homePlate();
      break;
    case 'F':
      // override number of tubes if argument was given
      if (args[0] > 0){
      nTubes = args[0];
      }
      Serial.print("Filling tubes:");
      Serial.println(nTubes);
      fillTubes();
      break;
    
  }

  
}

// ====================================================
// Functions for running the protocol

// Home the plate
void homePlate(){
  Serial.println("Homing...");
  // Step counter for finding homing position
  int posSteps = 0;
  // Lift servo
  levelServo.write(90);
  delay(500);
  // Rotate back a bit (approx 30 degrees) just in case we have started on top of the sensor
  plateStepper.step(-180);
  // Start rotating plate until sensor is triggered
  while (digitalRead(homeSensor) == HIGH){
    plateStepper.step(2); // 2 steps at a time, could be tuned though
  }
  Serial.println("Hall effect triggered!");
  // Rotate more until the sensor is no longer triggered
  while (digitalRead(homeSensor) == LOW){
    plateStepper.step(2);
    posSteps = posSteps + 2;
  }
  Serial.println("Hall effect reset!");
  // Now we found the positions (step indices) where the sensor is triggered.
  // we take the half point, so we go back half the steps between the limits
  plateStepper.step(-round(posSteps/2));
  Serial.println("Homed!");
}

// Function for pumping buffers - TO DO: Get rid of delay and use a timer so that the arduino doesn't stay in standby while pumping
void pumpSolution(float pumpTime, int pumpID){
  if (pumpID == 1){
    digitalWrite(pump1, HIGH);
    delay(pumpTime*1000);
    digitalWrite(pump1, LOW);
  }
  else if (pumpID == 2){
    digitalWrite(pump2, HIGH);
    delay(pumpTime*1000);
    digitalWrite(pump2, LOW);
  }
}

void collectWaste(){
  int washDone = false;
  Serial.println("Washing has begun.");
  // Wait until the sensor is triggered the first time before we start counting
  while (wasteValue == HIGH){
    wasteValue = digitalRead(wasteSensor);
    Serial.println("Waiting for first drop");
    delay(10);
  }
  Serial.println("First drop detected...waiting");
  // Get current time
  currentMillis = millis();
  while (!washDone){
    wasteValue = digitalRead(wasteSensor);
    if (wasteValue == LOW){
      // Reset timer: Sensor triggered, there is liquid in waste collector. 
      currentMillis = millis();
      timeOff = 0;
      Serial.println("Drops detected");
    }
    else{
      // Increase time counter: No liquid detected.
      timeOff = millis() - currentMillis;
      Serial.println("Drops not detected");
    }
    // Has the collector been empty for longer than the threshold time?
    //Return FALSE if timeOff does not exceed threshold seconds.
    washDone = (timeOff >= timeEmpty);
    delay(10);
  }
  Serial.println("Wash done!");
  delay(2000);
}

// Fill tubes ( collect fractions)
void fillTubes(){
  Serial.println("Filling tubes");

  // Local variables
  unsigned long currentMillis = millis(); 
  byte tubeIdx = 1; // counter for tube position
  // Lift servo and move to first tube
  levelServo.write(90);
  delay(500);
  plateStepper.setSpeed(750); //Max seems to be 750
  nSteps = round(2048*angleWaste/360); // convert angle to steps, knowing that a rotation is 2048 steps
  plateStepper.step(nSteps);
  // Lower servo and add elution buffer to column
  levelServo.write(0);
  delay(500);
  pumpSolution(0.24, 1); // Use pump #1

  // Fill all tubes - wait until each tube is filled before moving to the next one
  nSteps = round(2048*angleTubes/360); // convert angle to steps, knowing that a rotation is 2048 steps
  while (tubeIdx <= nTubes){
    // Read level sensor
    sensorValue = digitalRead(tubeSensor);
    // The minimum time before triggering (to avoid spurious triggers) is 5 seconds (TO DO - add as variable)
    if ((sensorValue == LOW) && (millis() - currentMillis > 5000)){
      // Reset timer
      currentMillis = millis();
      // Sensor triggered, rotate
      Serial.print("Done with tube #");
      Serial.println(tubeIdx);
      // Lift servo - small delay to give time for it to lift
      levelServo.write(90); // 90 degrees turn
      delay(500);
      // Increase position counter
      tubeIdx++;
      if (tubeIdx <= nTubes){
        // Rotate stepper
        plateStepper.step(nSteps);
        // Lower servo for next tube
        levelServo.write(0); // 90 degrees turn
        delay(500);
        pumpSolution(0.24, 1); // Use pump #1
        
      }
    }
  }
  Serial.println("Waiting for the last drops...");
  delay(10000);
  Serial.println("Done filling!");
  
  // Move to zero position
  nSteps = round(2048*(360 - (nTubes-1)*angleTubes - angleWaste)/360);
  plateStepper.step(nSteps);
}
