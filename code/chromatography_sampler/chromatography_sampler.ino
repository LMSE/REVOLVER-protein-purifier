/*
Arduino code for running the sample collection from a chromatography process.
This version works with an Aduino UNO
*/

/* Include libraries */
#include <Stepper.h>
#include <Servo.h>

/* Define pins */
const byte tubeSensor = 2; // pin for level sensor in tubes - this triggers the servo to retract, and then the motor to rotate
const byte servoPin = 3; // pin (PWM) for servo
const byte wasteSensor = 4; // pin for detecting liquid in the waste container
// Pins for stepper motor
const byte in1 = 8;
const byte in2 = 9;
const byte in3 = 10;
const byte in4 = 11;

/* Define variables */
// Constant properties of the harware
#define STEPS  32   // Number of steps per revolution of internal shaft of a BYJ48 stepper
const double angleTubes = 23.8; // angular separation between tubes in degrees
const int nTubes = 12; // Number of tubes in the sampler

// Other variables
byte sensorValue; // Value of the level sensor attached to the servo
byte wasteValue = HIGH; // Value of the liquid sensor in waste collector - default HIGH = not triggered

int nSteps;  // 2048 = 1 Revolution
byte tubeIdx = 1; // counter for tube position

bool washDone; // Logical value indicating whether the wash solution has completely left the column
unsigned long timeOff = 0; // time that the waste collector is empty
unsigned long currentMillis; // current time in milliseconds
const unsigned long timeEmpty = 10000; // time in milliseconds the waste container must be empty before accepting wash is done 

/* Objects */
// Setup of proper sequencing for Motor Driver Pins
// In1, In2, In3, In4 in the sequence 1-3-2-4
Stepper plateStepper(STEPS, in1, in3, in2, in4);
Servo   levelServo;

/* ================= Setup function =========================== */

// For testing we won't use the loop function. This code should execute a protocol once
// the loop function will come in handy when the whole protocol is repeated

void setup(){

  // ========== Setup pins and monitor ========================================
  // Start serial monitor
  Serial.begin(9600);
  Serial.println("Serial collection of chromatography fractions");

  // Define pin modes
  pinMode(tubeSensor, INPUT_PULLUP);
  pinMode(wasteSensor, INPUT_PULLUP);

  // Turn off motor - here we could have a homing routine, maybe using a small
  // magnet and a hall effect sensor to home the disc
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);

  // Attach servo and raise for homing
  levelServo.attach(servoPin);
  levelServo.write(90);


  // ========== Home rotating plate - align for waste collection ===============
  /* Rotate until the homing sensor is triggered
    fow now, the homing sensor is the same tube sensor but will be added as an
    additional input */
  homePlate();
  delay(1000); // to be removed if necessary
  
  // ========== Wash and collect waste =========================================
  // addWashBuffer(); or get the user to dump the sample and simply wait until it flow through
  washDone = false;
  collectWaste();
  delay(1000);


  // ========== Fill tubes =====================================================
  // Move to position and lower servo
  //to do - move to position
  levelServo.write(0);
  fillTubes();

}

/* ================= Loop function =========================== */
void loop(){


}/* --end main loop -- */


/* Additional auxilliary functions */

// Homing routine
void homePlate(){
  Serial.println("Homing...");
  // Rotate plate by a couple of steps until the sensor is triggered
  while (digitalRead(tubeSensor) == HIGH){
    plateStepper.setSpeed(300); // rotate a bit slower
    plateStepper.step(10);
    delay(10);
  }
  Serial.println("Homed!");
}


// Collect waste after washes
void collectWaste(){
  Serial.println("Collecting waste...");
  // Wait until the sensor is triggered the first time before we start counting
  while (wasteValue == HIGH){
    wasteValue = digitalRead(wasteSensor);
  }
  Serial.println("First drop detected...waiting");
  // Get current time 
  currentMillis = millis();
  while (!washDone){
    wasteValue = digitalRead(wasteSensor);
    if (wasteValue == LOW){
      // Sensor triggered, there is liquid in waste collector. Reset timer 
      currentMillis = millis();
      timeOff = 0;
    }
    else{
      // No liquid detected, increase time counter
      timeOff = millis() - currentMillis;
    }
    // Has the collector been empty for longer than the threshold time?
    washDone = (timeOff >= timeEmpty);
  }
  Serial.println("Wash done!");
}


// Fill tubes: Fill the eppis and rotate
void fillTubes(){
  Serial.println("Filling tubes");
  while (tubeIdx <= nTubes){
    // Read level sensor
    sensorValue = digitalRead(tubeSensor);
    if (sensorValue == LOW){
      // Sensor triggered, rotate
      Serial.print("Done with tube #");
      Serial.println(tubeIdx);
      // Lift servo - small delay to give time for it to lift
      levelServo.write(90); // 90 degrees turn
      delay(250);
      // Rotate stepper
      plateStepper.setSpeed(500); //Max seems to be 500
      nSteps = round(2048*angleTubes/360); // convert angle to steps, knowing that a rotation is 2048 steps
      plateStepper.step(nSteps);
      delay(2*nSteps); // 2 ms delay per step should be enough
      // Lower servo
      levelServo.write(0); // 90 degrees turn
      delay(250);
      // Increase position counter
      tubeIdx++;
    }
  }
  Serial.println("Done filling!");
}
