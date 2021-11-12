/*
Arduino code for running the sample collection from a chromatography process with a single collector.
Instructions set via serial connection and sent to distributors
----------------
Master device
*/

// V1 - I2C scanner based on code from https://playground.arduino.cc/Main/I2cScanner/

#include <Wire.h>
#include <Stepper.h>

// Define pins - distributor only has a stepper motor and pumps
const byte pump1 = 6; // Pump#1
const byte pump2 = 5; // Pump #2
const byte motPins[] = {9, 10, 11, 12}; // Pins for stepper motor

/* Constant properties of the hardware */
#define STEPS  32   // Number of steps per revolution of internal shaft of a 28byj-48 stepper
const int steps2take = 2048; // number of steps of motor - 1 revolution (motor is geared)

// Variables for parsing serial communication
const byte numChars = 32; // this value could be smaller or larger, as long as it fits the largest expected message
char receivedChars[numChars];
char tempChars[numChars];        // temporary array for use when parsing
// variables to hold the parsed data
char messageFromPC[numChars] = {0};
int ledInstruction = 0;
boolean newData = false;

// Variables for I2C scanner and I2C communication
byte error, address; // Variables for I2C scanner
int nI2C; // number of connected devices via I2C
const byte nI2CMax = 10; // Maximum number of I2C devices we will allow to be connected
const byte nTaskMax = 20; // Maximum number of tasks to be stored - TODO: This could be tuned
const byte nArgs = 3; // Maximum number of arguments to be used in functions
byte listI2C[nI2CMax]; // array that holds the addresses of the I2C devices connected to the bus (first column) and their angular position (second)
int locationsI2C[nI2CMax]; // array that contains the angular locations (in n steps) of the connected I2C devices
int angularPos = 0; // angular position of the motor when we start...arbitrary location
byte taskIdx[nI2CMax]; // array for storing the task being currently executed by each revolver
char taskName[nTaskMax]; // array for storing the tasks to be executed by each revolver (e.g. "H", "P", "F")
unsigned int taskArgs[nTaskMax][nArgs]; // array for storing arguments (up to three) for each of the tasks
unsigned int args[nArgs]; // arguments for executing the functions that are called manually, or for ease of reading
boolean allStored = false; // variable indicating whether the full protocol has been parsed and stored
byte nTask = 0; // Number of tasks received. Works as a index. Byte should allow for 255 instructions which is plenty
byte messageFromRevolver = 0; // Temporary storage for message from revolvers
boolean allDone; // variable that stores whether all devices are finished. We can use this to allow more instructions without reseting the devices
byte requestedAddress; // variable to store an I2C address if there is a manual request (e.g. rotate a plate)
boolean storeTask = false; // Flag for storing upcoming instructions in an array

// Define objects
// Setup of proper sequencing for Motor Driver Pins: In1, In2, In3, In4 in the sequence 1-3-2-4
Stepper mainStepper(STEPS, motPins[0], motPins[2], motPins[1], motPins[3]);

/* Run this program once*/
void setup() {
  // ========== Setup pins and monitor ========================================
  // Start serial monitor
  Serial.begin(9600);
  // Join I2C bus as master (no address)
  Wire.begin();
  Serial.println("MULTIVOLVER: Parallel collection of chromatography fractions");

  // Define pin modes
  pinMode(pump1, OUTPUT);
  pinMode(pump2, OUTPUT);

  // Turn off motor coils
  for (int i; i < 4; i++){digitalWrite(motPins[i], LOW);}
  mainStepper.setSpeed(700);

  // Step 1: Only execute once - find number and addresses of connected I2C devices
  scanI2C();

  // Step 2: Rotate to get the location of the devices
  //locateI2C();
  Serial.println("Setup done...please locate devices");
}

/* Loop function - listen to serial monitor */
void loop() {

  // Listen to serial monitor for instructions as long as they haven't been all stored
  // This structure allows the whole process to be repeated without rebooting the arduino
  if (!allStored){
    // Monitor the serial port - the loop will scan the characters as they come, even if the full
    // message doesn't arrive during a single iteration of the loop
    recvWithStartEndMarkers();
      if (newData == true) {
          newData = false;
          // This temporary copy is necessary to protect the original data
          // because strtok() used in parseData() replaces the commas with \0
          strcpy(tempChars, receivedChars);
          // Parse the message: Split into its components and store for I2C
          parseCommand();
          // Execute the data: Called the necessary function - only when NOT recording instructions
          // since these are manual functions assumed to be used only for setup
          if (!storeTask && !allStored){
            executeCommand();
          }
          // Announce to port that we are done so that next instruction can come in
          //Serial.println("done");
      }
  }

  else {
    // All instructions have been stored, we now start passsing them one by one to each connected
    // REVOLVER.

    // Reset boolean to check if all REVOLVERs are done with all tasks(any false will make it false, so it starts as true)
    allDone = true;
    // Loop through all revolvers, read their status and pass instructions if idle
    for (int idx = 0; idx < nI2C; idx++){
      //delay(3000); // temporary delay for debugging - it seems that if we call I2C while the stepper is running, it interrupts
      // Request status update of revolver
      Wire.requestFrom(listI2C[idx], 1); // request 1 byte
      // Read message - ask the REVOLVER if it's idle or busy (0)
      messageFromRevolver = Wire.read();
      // Check if all devices have finished all tasks (by checking if each device
      // has finished the last task and is currently free). We compare > nTask because
      // the following if statement increases the counter by one (i.e. the last task is
      // to display stuff to the serial monitor)
      allDone = allDone && (taskIdx[idx] > nTask && messageFromRevolver != 0);

      // If we finished the final task and the device is idle, display something
      if (messageFromRevolver != 0 && taskIdx[idx] == nTask){
        Serial.print("REVOLVER #");
        Serial.print(listI2C[idx]);
        Serial.println(" finished!");
        taskIdx[idx]++; // make index > nTask so nothing happens until it gets reset to 0
      }

      // If we haven't finished all tasks, but the REVOLVER is idle, we transmit a new instruction.
      // By default all REVOLVERs start as "done" and wait for the first instruction,
      // which is why we update the index after passing the instruction
      if (messageFromRevolver != 0 && taskIdx[idx] < nTask){

        // If the next task is to pump, we handle that with the distributor by visiting the REVOLVER and pumping.
        // If not, we request the command via I2C
        if (taskName[taskIdx[idx]] == 'P'){
          Serial.print("Distributing buffer to REVOLVER I2C #");
          Serial.println(listI2C[idx]);
          // Visit device
          visitRevolver(listI2C[idx]);
          // Pump the required buffer by passing the arguments of the function
          delay(1000);
          pumpSolution(taskArgs[taskIdx[idx]][0], taskArgs[taskIdx[idx]][1]);
          // Update task idx for next iteration
          taskIdx[idx]++;
          // If the following task is to fill tubes, we don't want to wait for the master to loop
          // through the other REVOLVERs and waste time before assigning the fill task, so we do it here.
          // This is only critical after a "pump" operation since the column will start dripping
          if (taskName[taskIdx[idx]] == 'F'){
            sendTaskI2C(idx); // Task index updated internally
          }
        }
        else {
          sendTaskI2C(idx);
        }

      }

    }



    // If all devices are done, reset and start listenting for more
    if (allDone){
      Serial.println("All devices finished the requested tasks! Input more");
      delay(1000);
      // Reset counters
      allStored = false;
      nTask = 0;
      for (int i = 0; i < nI2CMax; i++){
        taskIdx[i] = 0;
      }
    }

  }


} // end of loop

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

    // Scan the other arguments - all assumed to be integers
    for (int i = 0; i < nArgs; i++){
      strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
      args[i] = atoi(strtokIndx);     // convert this part to an integer
      //Serial.println(args[i]);
    }

    // If we are storing instructions, we save the current instruction to the required arrays
    // As a precaution, do NOT store a "ROTATE" command since that will cause all REVOLVERs
    // to rotate and mess up the collection
    if (storeTask && messageFromPC[0] != 'R' && messageFromPC[0] != 'X'){
      taskName[nTask] = messageFromPC[0];
      for (int i = 0; i < nArgs; i++){
        taskArgs[nTask][i] = args[i];
      }
      // Display stuff
      Serial.print("Task '");
      Serial.print(taskName[nTask]);
      Serial.println("' stored");
      // Increase task counter
      nTask++;
    }

    // If the task is to rotate, we need to modify the arguments a bit because
    // the number of steps requested might be larger than 255 and won't fit in a single byte for I2C.
    // To solve this, we don't pass the number of steps as the first argument, but we pass mod(n,255)
    // and as a third argument we pass the whole part of n/255. That way we tell the slaves how many time to
    // rotate 255 steps, plus a bit more
    if (messageFromPC[0] == 'R'){
      // This task should only be called before recording the protocol since it's only used
      // for homing each REVOLVER manually

      // Extract target I2C address and number of steps
      unsigned int nSteps = args[0];
      requestedAddress = args[2];
      // Convert number of steps by mod 255 and change the arguments. args[1] is the direction and doesn't change
      args[0] = nSteps % 255;
      args[2] = nSteps/255;
    }

    // Check whether we'll start storing instructios in the next command:
    // The special command 'X' defines when tasks are stored and when they are not.
    // i.e. the first 'X' indicates to start storing the protocol and the last 'X'
    // stops recording and executes the protocol.
    // Any manual command needs to be therefore before the first 'X'

    if (messageFromPC[0] == 'X' && !storeTask){ // Start storing
      storeTask = true;
      Serial.println("Receiving and storing tasks...");
    }
    else if (messageFromPC[0] == 'X' && storeTask){ // Stop storing; indicate all steps are stored
      storeTask = false;
      allStored = true;
      Serial.println("Done receiving and storing tasks...Protocol begins...");
    }

}

// Function for executing commands.
// These are commands that are executed directly after calling them via serial and they
// are executed by the distributor (with the exception of manually rotating the REVOLVERs).
// These are only executed when NOT recording the protocol, which is why we use args and not taskArgs

void executeCommand(){ // TO DO - clean execute function and parse commands
  char firstLetter = messageFromPC[0];
  switch (firstLetter){
    case 'R': // Rotate manually
      if (requestedAddress == 0){
        // Rotate distributor with the arguments parsed - convert back to total steps as done
        // by the distributors
        rotatePlate(args[0] + 255*args[2], args[1]);
      }
      else {
        // Pass arguments to slave via I2C
        Wire.beginTransmission(requestedAddress);
        // Write the name of the command to be executed
        Wire.write('R');
        // Write the arguments
        for (int i = 0; i < nArgs; i++){
          Wire.write(args[i]);
        }
        // End transmission
        Wire.endTransmission();
      }
      break;
    case 'S': // Store location of I2C device

      Serial.print("Location for device with I2C #");
      Serial.print(args[0]);
      Serial.print(": ");
      Serial.println(angularPos);
      // This code could be simpler if we guarantee that the I2C addresses we use are in order 1,2,...,6 such that the indices are also ordered.
      // but determining the index is fast anyways, so we keep this version where we can use ANY I2C addresses (not necessarily in order)
      locationsI2C[findIdx(args[0])] = angularPos;
      break;
    case 'V': // Visit a device - ONLY for debugging. We don't want to have manual access to this, not necessary
      visitRevolver(args[0]);
      break;
    case 'P': // Pump. Useful for purging lines
      pumpSolution(args[0], args[1]); // args 1 is the pump time in milliseconds, args 0 is the pump ID
      break;
    case 'L':
      locateI2C();
      break; // Auto locate revolvers
    default:
      Serial.println("Requested command cannot be executed by distributor!");
      break;
  }
}

// ====================================================
// Functions for I2C

// Scan I2C bus to see what's connected and what addresses they have
void scanI2C() {
  Serial.println("Scanning I2C bus...");
  nI2C = 0;
  for(address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission(); // see https://www.arduino.cc/en/Reference/WireEndTransmission

    if (error == 0) // success
    {
      Serial.print("I2C device found at address: ");
      Serial.println(address);
      // Store address
      listI2C[nI2C] = address;
      // Increase count
      nI2C++;
    }
   /* else if (error==4) // other error
    {
      Serial.print("Unknown error at address: ");
      Serial.println(address);
    }    */

    // Make sure we don't exceed the number of devices we can store
    if (nI2C > nI2CMax) {nI2C = nI2CMax;}
  }
  Serial.print("Found ");
  Serial.print(nI2C);
  Serial.println(" devices");

}

// Rotate stepper motor to get locations of I2C devices
// Optional: Requires additional hall effect sensors in each device

void locateI2C(){

  Serial.println("Finding locations of devices...");
  byte docked = 0; // not docked by default
  angularPos = 0;
 // mainStepper.step(-steps2take/2);
  // Rotate the stepper a bit at a time and scan the I2C addresses to see if we docked a device
  while (angularPos < steps2take){ // complete a single rotation
    mainStepper.step(10); // arbitrary rotation, but should be small - can change
    angularPos = angularPos + 10;
    //Serial.println("Searching...");

    // Loop for the I2C devices
    for (int idx = 0; idx < nI2C; idx++){
      // Ask each device if their sensor has been triggered (i.e. docking happened)
      Wire.requestFrom(listI2C[idx], 1); // request 1 byte
      //Serial.print("Status for sensor ");
      //Serial.print(listI2C[idx]);
      //Serial.print(" is: ");
      while (Wire.available()){
        docked = Wire.read(); // The reolver will return a '2' if we are docked. If not it will return a 1 or a 0 depending on taskDone
        //Serial.println(docked);
      }
      //Serial.println(docked);
      if (docked == 2){

        // Hall effect sensor triggered - store the initial position where the sensor was triggered
        locationsI2C[idx] = angularPos;
        // Advance more until the sensor is reset, indicating we passed the docking position
        while (docked == 2){
          // Advance a bit
          mainStepper.step(4);
          angularPos = angularPos + 4;
          // Interrogate slave
          Wire.requestFrom(listI2C[idx], 1); // request 1 byte
          docked = Wire.read();
          //Serial.println("meep");
        }
        // Sensor was reset. The location of the I2C device is the average of the initial
        // position and the current position
        locationsI2C[idx] = round((locationsI2C[idx] + angularPos)/2);
        //locationsI2C[idx] = locationsI2C[idx] + steps2take/2;
        //locationsI2C[idx] = locationsI2C[idx] % steps2take;
        Serial.print("Location of I2C #");
        Serial.print(listI2C[idx]);
        Serial.print(" found at n = ");
        Serial.println(locationsI2C[idx]);

      }
    }
  }


}

// Send I2C command - aux function for keeping main code clean
void sendTaskI2C(int idx){
  Wire.beginTransmission(listI2C[idx]);
  // Write the name of the command to be executed
  Wire.write(taskName[taskIdx[idx]]);
  // Write the arguments
  for (int i = 0; i < nArgs; i++){
    Wire.write(taskArgs[taskIdx[idx]][i]);
  }
  // End transmission
  Wire.endTransmission();
  // Display stuff
  Serial.print("Requesting task ");
  Serial.print(taskName[taskIdx[idx]]);
  Serial.print(" from device #");
  Serial.print(listI2C[idx]);
  Serial.println("...");

  // If the task is "C" (collect) and the argument is 1, this is a step where
  // the user adds the buffer, so we display something to let the user know about this
  // and make the buzzer beep (TO ADD)
  if (taskName[taskIdx[idx]] == 'C' && taskArgs[taskIdx[idx]][0] == 1){
    Serial.print("Please add lysate to device #");
    Serial.println(listI2C[idx]);
  }

  // Update task idx for next iteration
  taskIdx[idx]++;
}

// ==============================================
// Functions for running the protocol

void rotatePlate(int steps, int dir){ // mainStepper has a different name from plateStepper in the revolver. Unify functions?
  // d must be either 0 or 1.
  dir = dir*2 - 1; // this converts it to -1 or 1
  mainStepper.step(dir*steps);
  // Update position - calculate the mod to make sure abs(position) < steps2take
  angularPos = angularPos + dir*steps;
  angularPos = angularPos % steps2take;
  // If position is negative, add steps2take to make it positive
  if (angularPos < 0 ){
    angularPos = angularPos + steps2take;
  }
  //Serial.println(angularPos);
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

// Function for visiting a given REVOLVER. We might take the longest path, but
// we want the distriutor to never complete more than a full rotation to avoid
// tangling the tubing
void visitRevolver(int targetDevice){
  // If both the current position and the stored position are in the same half of the rotation, we rotate normally.
  // Else, we rotate the other direction to avoid getting tangled in the center

  // Target angular position - corresponding to the index of the target device
  int targetPos = locationsI2C[findIdx(targetDevice)];

  // Check if both start and end position are on the same half of the rotation
  if ((targetPos>steps2take/2 && angularPos>steps2take/2) || (targetPos<steps2take/2 && angularPos<steps2take/2)){
    // Starting and end position in the same half of the rotation
    mainStepper.step(targetPos - angularPos);
  }
  else{
    // Go all the way around to not cross the mid point (to avoid tangles)
    // Direction will depend on position of start and end point
    if (angularPos > targetPos){
      // Go counter clockwise
      mainStepper.step(targetPos - angularPos + steps2take);
    }
    else {
      // Go clockwise
      mainStepper.step(targetPos - angularPos - steps2take);
    }
  }
  // Update position
  angularPos = targetPos;
}

// ==============================================
// Additional useful functions

// Scans the array of I2C addresses for a given value val and finds the index where its found.
// This function does not handle errors because it is only used internally
byte findIdx(byte val){
  byte idx = 0;
  for (int i = 0; i < nI2C; i++){
    if (listI2C[i] == val){ // Match found, index is the current i
      idx = i;
      break;
    }
  }
  return idx;
}
