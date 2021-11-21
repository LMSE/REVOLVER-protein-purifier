/*
Arduino code for running the sample collection from a chromatography process with a single collector.
Instructions set via serial connection
*/

// Include libraries
#include <Stepper.h> // For driving stepper motor (28byj-48)
#include <Servo.h>  // For driving the microservo (SG90)
//# include <Wire.h> // For I2C communication - not needed for single device

// Define the type of collector (1 for normal, 2 for siphon version)
byte collectorType = 1;

// Define pins - these are meant to work with an Arduino Nano, but should also work with an Arduino Uno
const byte tubeSensor = 2; // Level sensor in eppi tubes
const byte servoPin = 3; // Servo
const byte wasteSensor = 4; // Liquid sensor for waste funnel
const byte pump1 = 6; // Pump#1
const byte pump2 = 5; // Pump #2
const byte dockingSensor = 7; // For docking the distributor to the device - not needed for single device
const byte homeSensor = 8; // For homing the revolving plate (hall effect sensor A3144)
const byte motPins[] = {9, 10, 11, 12}; // Pins for stepper motor

// Declare variables

/* Constant properties of the hardware */
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

// Variables for parsing serial communication
const byte numChars = 32; // this value could be smaller or larger, as long as it fits the largest expected message
char receivedChars[numChars];
char tempChars[numChars];        // temporary array for use when parsing

// Variables to hold the parsed data
char messageFromPC[numChars] = {0};
int ledInstruction = 0;
boolean newData = false;
const byte nArgs = 3; // Maximum number of arguments to be used in functions
unsigned int args[nArgs]; // arguments for executing the functions. Declared as unsigned to be able to store larger numbers if needed

// Define objects
// Setup of proper sequencing for Motor Driver Pins: In1, In2, In3, In4 in the sequence 1-3-2-4
Stepper plateStepper(STEPS, motPins[0], motPins[2], motPins[1], motPins[3]);
Servo   levelServo;

/* Run this program once*/
void setup(){

  // ========== Setup pins and monitor ========================================
  // Start serial monitor
  Serial.begin(9600);
  Serial.println("REVOLVER: Serial collection of chromatography fractions");

  // Define pin modes
  pinMode(tubeSensor, INPUT_PULLUP);
  pinMode(wasteSensor, INPUT_PULLUP);
  pinMode(pump1, OUTPUT);
  pinMode(pump2, OUTPUT);
  pinMode(dockingSensor, INPUT_PULLUP);
  pinMode(homeSensor, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  // Turn off motor coils
  for (int i = 0; i < 4; i++){digitalWrite(motPins[i], LOW);}
  plateStepper.setSpeed(700);
  // Attach servo and raise for homing
  levelServo.attach(servoPin);
  levelServo.write(90);
}

/* Loop function - listen to serial monitor */
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
        // Announce to port that we are done so that next instruction can come in
        //Serial.println("Task done"); Only needed for GUI to have a standard message
    }

    // For debugging homing sensor
    //if(digitalRead(homeSensor) == LOW){digitalWrite(LED_BUILTIN, HIGH);}
    //else{digitalWrite(LED_BUILTIN, LOW);}
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
    //Serial.println(messageFromPC);

    // Scan the other arguments - all assumed to be integers
    for (int i = 0; i < nArgs; i++){
      strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
      args[i] = atoi(strtokIndx);     // convert this part to an integer
      //Serial.println(args[i]);
    }
}

void executeCommand(){
  // Call the required command
  // All commands have a different letter so we only
  // need their first character to identify them
  char firstLetter = messageFromPC[0];
  switch (firstLetter){
    case 'H': // Auto home
      homePlate();
      break;
    case 'F': // Fill tubes
      // override number of tubes if argument was given
      if (args[0] > 0){
        nTubes = args[0];
      }
      fillTubes();
      break;
    case 'P': // Pump
      pumpSolution(args[0], args[1]); // args 1 is the pump time in milliseconds, args 0 is the pump ID
      break;
    case 'C': // Collect waste
      collectWaste(); // No arguments, but could take an optional argument from args that is the wait time
      break;
    case 'R': // Rotate manually
      rotatePlate();
      break;
    // OPTIONAL TO DO - add a case for "settings" where we modify waiting times and things like that
  }
}

// ====================================================
// Functions for running the protocol

// Home the plate
void homePlate(){
  Serial.println("Homing REVOLVER...");
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
    posSteps = posSteps + 2; // update position
    // If we go around once without finding the sensor, we stop and throw a notification
    // this prevents infinite loops if the sensor is not installed
    if (posSteps > 2100){
      Serial.println("Sensor not found...stopping");
      return;
    }
  }
  // Reset step counter
  posSteps = 0;
  Serial.print("Sensor triggered!...");
  // Rotate more until the sensor is no longer triggered
  while (digitalRead(homeSensor) == LOW){
    plateStepper.step(2);
    posSteps = posSteps + 2;
  }
  Serial.print("Sensor reset!...");
  // Now we found the positions (step indices) where the sensor is triggered.
  // we take the half point, so we go back half the steps between the limits
  plateStepper.step(-round(posSteps/2));
  Serial.println("Homed!");
}

// Function for pumping buffers
void pumpSolution(int pumpVolume, int pumpID){
  if (pumpID == 1){
    if (pumpVolume <= 1){
       int pumpTime = pumpVolume*(100/0.74);
       digitalWrite(pump1, HIGH);
       delay(pumpTime);
       digitalWrite(pump1, LOW);
    }
    else{
       int pumpTime = pumpVolume*(100/0.707);
       digitalWrite(pump1, HIGH);
       delay(pumpTime);
       digitalWrite(pump1, LOW);
    }
  }
  else if (pumpID == 2){
    if (pumpVolume <= 1){
       int pumpTime = pumpVolume*(100/0.74);
       digitalWrite(pump2, HIGH);
       delay(pumpTime);
       digitalWrite(pump2, LOW);
    }
    else{
       int pumpTime = pumpVolume*(100/0.42);
       digitalWrite(pump2, HIGH);
       delay(pumpTime);
       digitalWrite(pump2, LOW);
    }
  }
}

// Function for collecting waste after washes
void collectWaste(){
  int washDone = false;
  Serial.println("Washing has begun...");
  Serial.println("Waiting for first drop...");

  // Wait until the sensor is triggered the first time before we start counting.
  // If it's not triggered within a certain time, it might be that the user forgot to add lysate,
  // so we display a reminder
  currentMillis = millis();
  while (wasteValue == HIGH){
    wasteValue = digitalRead(wasteSensor);
    delay(10);
    if (millis() - currentMillis >= timeEmpty){
      // Display reminder
      Serial.println("No liquid detected so far. Did you forget to add the lysate or input a prior command to pump wash solution?");
      // Reset timer
      currentMillis = millis();
    }
  }
  // Loop exits if a drop was detected
  Serial.println("First drop detected...waiting");

  // Handle the waste collection depending on the type of collector
  switch (collectorType){
    // Version 1 of collector - exit criterion is that the colelctor remains empty
    case 1:
      // Get current time
      currentMillis = millis();
      while (!washDone){
        wasteValue = digitalRead(wasteSensor);
        if (wasteValue == LOW){
          // Reset timer: Sensor triggered, there is liquid in waste collector.
          currentMillis = millis();
          timeOff = 0;
          Serial.println("Drops detected"); //Should we remove this prompt? it clutters the monitor
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
      break;
    // Version 2 of collector (siphon) - exit criterion is that the collector signal
    // stops changing (i.e. stays full OR empty)
    case 2:
    // Get current time and read initial state of sensor
    currentMillis = millis();
    byte wasteValueOld = digitalRead(wasteSensor);
    while (!washDone){
      // Read sensor
      wasteValue = digitalRead(wasteSensor);
      if (wasteValue != wasteValueOld){
        // Reset timer: Liquid content in the siphon changed
        currentMillis = millis();
        timeOff = 0;
        Serial.println("Siphon level changed");
      }
      else{
        // Increase time counter: Siphon state is unchanged
        timeOff = millis() - currentMillis;
      }
      // Update old state
      wasteValueOld = wasteValue;
      // Has the collector been empty for longer than the threshold time?
      //Return FALSE if timeOff does not exceed threshold seconds.
      washDone = (timeOff >= timeEmpty);
      delay(10);
    }
    break;
  }

  Serial.println("Wash done!");
  delay(2000);
}

// Fill tubes (collect fractions)
void fillTubes(){
  Serial.println("Filling tubes");
  // Lift servo and move to first tube
  levelServo.write(90);
  delay(500);
  plateStepper.setSpeed(750); //Max seems to be 750
  nSteps = round(2048*angleWaste/360); // convert angle to steps, knowing that a rotation is 2048 steps
  plateStepper.step(nSteps+30); // add 30 steps so that the servo sensor dips close to the center of the tube
  // Local variables - timer and tube counter
  unsigned long currentMillis = millis();
  byte tubeIdx = 1; // counter for tube position
  // Move a couple more steps to guarantee the sensor goes in and doesn't bump
  plateStepper.step(10);
  // Lower servo
  levelServo.write(0);
  delay(500);
  // Go back those extra steps
  plateStepper.step(-10);


  // Fill all tubes - wait until each tube is filled before moving to the next one
  nSteps = round(2048*angleTubes/360); // convert angle to steps, knowing that a rotation is 2048 steps
  while (tubeIdx <= nTubes){
    // Read level sensor
    sensorValue = digitalRead(tubeSensor);

    // The minimum time before triggering (to avoid spurious triggers) is 5 seconds (TO DO - add as variable)
    if ((sensorValue == LOW) && (millis() - currentMillis > 5000)){
      // Sensor triggered, display stuff and rotate
      Serial.print("Done with tube #");
      Serial.print(tubeIdx);
      Serial.print("...time ellapsed (ms): ");
      Serial.println(millis() - currentMillis);
      // Lift servo - small delay to give time for it to lift
      levelServo.write(90); // 90 degrees turn
      delay(500);
      // Increase position counter
      tubeIdx++;
      if (tubeIdx <= nTubes){
        // Rotate stepper
        plateStepper.step(nSteps);
        // Reset timer
        currentMillis = millis();
        // Move extra steps so the sensor goes in always
        plateStepper.step(10);
        // Lower servo for next tube
        levelServo.write(0); // 90 degrees turn
        delay(500);
        // Move back
        plateStepper.step(-10);
      }
    }
  }
  Serial.print("Waiting for the last drops...");
  delay(500);
  Serial.println("Done filling tubes!");

  // Move to zero position
  // TO DO - maybe make this an option. If we want to add
  // buffer between collections, we need to NOT reset the plate
  // and only do the movement from waste to tube 1 ONCE (when tube idx == 0)
  nSteps = round(2048*(360 - (nTubes-1)*angleTubes - angleWaste)/360);
  plateStepper.step(nSteps-30); // Remove the 30 steps we had added
}

// Additional function: Rotate plate manually for manual homing if needed
void rotatePlate(){
  // Use the args array, with args[0] being taken as the number of steps, and args[1] as the direction.
  // args[1] must be either 0 or 1.
  int dir = args[1]*2 - 1; // this converts it to -1 or 1
  plateStepper.step(dir*args[0]);
}
