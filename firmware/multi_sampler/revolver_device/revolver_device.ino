/*
Arduino code for running the sample collection from a chromatography process with a single collector.
Instructions set via serial connection and sent to distributors
----------------
Slave device (i.e. Revolver)
*/

// Include libraries
#include <Stepper.h> // For driving stepper motor (28byj-48)
#include <Servo.h>  // For driving the microservo (SG90)
#include <Wire.h> // For I2C communication - not needed for single device

// I2C address: Make sure each board gets a unique address and write it down on the revolver
const byte I2CAddress = 2; // this should be an integer between 1 and 127

// Define pins - these are meant to work with an Arduino Nano, but should also work with an Arduino Uno
const byte tubeSensor = 2; // Level sensor in eppi tubes
const byte servoPin = 3; // Servo
const byte wasteSensor = 4; // Liquid sensor for waste funnel
const byte pump1 = 5; // Pump#1 - pumps not needed when running in multiplexed device
const byte pump2 = 6; // Pump #2 - pumps not needed when running in multiplexed device
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
// TO DO: Add variables given by serial port as volatile. Another version of the firmware called "stand alone" might be needed that has pre-set arguments to not use the PC

// Variables for parsing I2C communication
char messageFromMaster; // Character describing the instruction to execute
const byte nArgs = 3; // Maximum number of arguments to be used in functions
unsigned int args[nArgs]; // arguments for executing the functions. Declared as unsigned to be able to store larger numbers if needed
boolean taskDone = true; // variable to indicate if a given task is done. Starts as true to receive first instruction
boolean newTask = false;

// Define objects
// Setup of proper sequencing for Motor Driver Pins: In1, In2, In3, In4 in the sequence 1-3-2-4
Stepper plateStepper(STEPS, motPins[0], motPins[2], motPins[1], motPins[3]);
Servo   levelServo;

/* Run this program once*/
void setup(){

  // ========== Setup pins and monitor ========================================
  // Start serial monitor
  //Serial.begin(9600);
  // Join I2C bus
  Wire.begin(I2CAddress);
  // Register events
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  //Serial.println("Serial collection of chromatography fractions");

  // Define pin modes
  pinMode(tubeSensor, INPUT_PULLUP);
  pinMode(wasteSensor, INPUT_PULLUP);
  pinMode(pump1, OUTPUT);
  pinMode(pump2, OUTPUT);
  pinMode(dockingSensor, INPUT_PULLUP);
  pinMode(homeSensor, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Turn off motor coils
  for (int i; i<4; i++){ digitalWrite(motPins[i], LOW);}
  plateStepper.setSpeed(700);
  // Attach servo and raise for homing
  levelServo.attach(servoPin);
  levelServo.write(90);

}

/* Loop function - listen to I2C bus */
void loop(){

  // Execute commands given by serial to the master and sent via I2C to revolver
  if (newTask){
    digitalWrite(LED_BUILTIN, HIGH);
    newTask = false;
    executeCommand();
    // Once task is completed
    taskDone = true;
    digitalWrite(LED_BUILTIN, LOW);
  }
}

// ====================================================
// Functions for I2C

// Once a task is done, we inform the master, who sends a new command
void receiveEvent(int howMany){
  // Read data in I2C bus - the master will send 4 bytes:
  // a character with the instruction, and three ints as arguments
  messageFromMaster = Wire.read(); // receive byte 1 as a character. When we read, we evacuate one byte
  for (int i = 0; i < nArgs; i++){
    args[i] = Wire.read();
  }
  // Reset task
  taskDone = false;
  // Indicate new task is received
  newTask = true;
}

// Executed when there is a request. For now only indicate if task is done (1) or not (0)
// this can be extended to request pumps
void requestEvent(){
  if (taskDone){
    Wire.write(1);
  }
  else {
    Wire.write(0);
  }
}

void executeCommand(){
  // Call the required command
  // (pegassu firmware https://github.com/pachterlab/pegasus/blob/master/firmware/motor_serial_com/motor_serial_com.ino does something different)

  // I am using a quick hack to use the switch since it needs a char, so I am getting the first letter of the message. This works but maybe not ellegant
  // and we'll see if all commands have different letters
    char firstLetter = messageFromMaster; // in the single version, this is messageFromPC[0]. We need to reconcile these and add them as an argument
    switch (firstLetter){
    case 'H': // Home
      homePlate();
      break;
    case 'F': // Fill tubes
      // override number of tubes if argument was given
      if (args[0] > 0){
      nTubes = args[0];
      }
      //Serial.print("Filling tubes:");
      //Serial.println(nTubes);
      fillTubes();
      break;
    case 'P': // Pump
      pumpSolution(args[1], args[0]); // args 1 is the pump time in milliseconds, args 0 is the pump ID
      break;
    case 'C': // Collect waste
      collectWaste(); // No arguments, but could take an optional argument from args that is the wait time
      break;
      // TO DO - add a case for "settings" where we modify waiting times and things like that
    case 'R': // Rotate manually
      rotatePlate();
      break;
  }


}

// ====================================================
// Functions for running the protocol

// Home the plate
void homePlate(){
  //Serial.println("Homing...");
  // Step counter for finding homing position
  int posSteps = 0;
  // Lift servo
  levelServo.write(90);
  delay(500);
  // Rotate back a bit (approx 30 degrees) just in case we have started on top of the sensor
  plateStepper.step(-180);

  //plateStepper.step(1000); // for testing multiple arduino
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
  //Serial.println("Homed!");
}

// Function for pumping buffers - TO DO: Get rid of delay and use a timer so that the arduino doesn't stay in standby while pumping
// Change so that the input is milliseconds and it's calculated by R based on the callibration
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
  //Serial.println("Wash done!");
  delay(2000);
}

// Fill tubes ( collect fractions)
void fillTubes(){
  //Serial.println("Filling tubes");

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

void rotatePlate(){
  // Use the args array. args[1] is the direction (either 0 or 1). The number of steps is encoded
  // as a multiple of 255 (arg[2]) plus the remainder (arg[0]) to able to pass a large number of steps 
  // as bytes
  int dir = args[1]*2 - 1; // this converts it to -1 or 1
  int n = args[0] + 255*args[2];
  plateStepper.step(dir*n);
}
