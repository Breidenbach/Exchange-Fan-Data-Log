/**************************************************************************/
/*

This program controls operation of the air exchange fan, and logs data 
from four temperature sensors as well as the switches on the sliding
deck door and windows in the bedroom and office.  An additional input
to controlling the fan which is also logged is the furnace control.

The harware uses the Adafruit MCP9808 temperature sensors (4), the
Adafruit DS1307 real time clock, and relay module.

Pins SCL & SDA are used for addressing the temp sensors as well as the Real Time Clock
Pins 8, 11, 12, and 13 are used for communicating between the Arduino
and Andee.

The computer is an Arduino Uno with an Annikken Andee board to write 
to a micro SD card and communicate with an IOS product.

Data is logged to the SD card in csv format which can be read by Excel.
Logging is done at intervals set by the constant interval (in seconds).
The format of the data is:
  Date number (number of days since 1/2/1904, the base used by Mac Excel)
  Time of day (number of seconds since midnight, which must be converted 
    in Excel to a fraction of the day by dividing by 86400)
  Temperature from sensor 1 (Outside air)
  Temperature from sensor 2 (Outside air after exchange)
  Temperature from sensor 3 (Inside air)
  Temperature from sensor 4 (Inside air after exchange)
  % of time movingAvg
      desired %
      actual %
      adjustment enabled
      run mode state
  Status of Bedroom windows (0 = open, 1 = closed)
  Status of Office windows
  Status of Sliding Deck Door
  Status of Furnace signal
  Status of Air Exchange Fan (1 = running, 0 = not running)

The exchange fan is set to running if requested by the furnace and
all windows and the door are closed, except that the door signal is 
delayed to avoid turning the fan on and off excessively when the door
is used for a quick entry or exit from the house.  The delay is set by
constant doorDelay (in seconds).

The time running percent may be adjusted by the computer, and there are
buttons for setting the requested percent, reporting the current
percent, toggling enablement of adjustment.  The percentage calculated
time running per on/off cycle.

Update log:
1.1  8/29/2016
Correct output string length
Correct clock setting to use iPad values
Add F macros to test print strings.
Correct printing ratios for debug
Remove temperature adjustments

*/
/**************************************************************************/

//#define outputSerial  //  to produce serial port output for debugging

#include <SPI.h>
#include <Wire.h>
#include "RTClib.h"  // apparently needs to preceed
#include "Adafruit_MCP9808.h"
#include <Andee.h>


//  Calibration values
const unsigned long interval = 300; //  5 min logging interval
const unsigned long doorDelay = 120;   //  temp 12 sec delay
const int maxN = 64;
const int defaultPercent = 30;

// Pin definitions
const byte      bedroomSensor = 2;
const byte      officeSensor = 3;
const byte      kitchenSensor = 4;
const byte      furnaceSensor = 5;
const byte      bedroomIndicator = 6;
const byte      officeIndicator = 7;
const byte      kitchenIndicator = 9;
const byte      furnaceIndicator = 10;
const byte      relayControlPin = A0;  // relay for fan control

//  Display definitions
AndeeHelper tempDisplayF[4]; // Temp in deg F
AndeeHelper clockDisplay;  // RTC display
AndeeHelper logStats;   //  logging count
AndeeHelper btnToggleLog; // turn logging on and off
AndeeHelper btnUploadLog; // request upload log
AndeeHelper switchDisplay; // sensors and running
AndeeHelper btnSetClock;  // set real time clock
AndeeHelper sliderAdjustPercent; // adjust desired percent
AndeeHelper blowerPercent;  // current actual 
AndeeHelper btnEnableAdjustment;  // enable/disable percent adjustment 

// Create the MCP9808 temperature sensor object
Adafruit_MCP9808 tempsensor[4] = Adafruit_MCP9808();
const int sensor_addr = 0x18;  // I2C address, app uses 0x18 through 0x1B

// Create RealTimeClock object
RTC_DS1307 rtc;  // I2C address is 0x68

// and data format area
char     timeStamp[12] = "not set";
char     todayDate[12];
char     logStatus[35];
char     bedroomDisplayStatus[7];
char     officeDisplayStatus[7];
char     kitchenDisplayStatus[7];
char     furnaceDisplayStatus[7];
char     operatingDisplayStatus[8];

unsigned long logCount = 0;  // number of log entries written

bool      logData = false;
bool      AndeeFlag = false;
bool      firstTime = true;
bool      adjustmentEnabled = false;

// time and date variables
unsigned long currentTime = 0;
unsigned long loggedTime = 0;
unsigned long currentDay;
unsigned long doorComplete = 0;  // time when door delay will complete

 // Switch status
bool      SSbedroom = false; 
bool      SSoffice = false;
bool      SSkitchen = false;
bool      SSfurnace = false;
bool      SSrunning = false;
int       bedroomSensorInput = LOW;
int       officeSensorInput = LOW;
int       kitchenSensorInput = LOW;
int       furnaceSensorInput = LOW;

// Variables necessary for SD card read/write
char filename[20];
word  filenumber = 0;
unsigned long  offset = 0;
int logOffset;
char errorMsgBuffer[30];
char outputString[40];

// To store the temperature reading
float degF[4];
char strTemp0[12];
byte ndx;

// Variables for percent operation of fan, always stored as ratio but 
//     displayed as percent
float   desiredRatio;
float   actualRatio = 0;
float   movingAverage = 0;
int     averageCount = 0;
int     desiredPercent;
#define notRunning 0
#define Running 1
#define RunningAdjusting 2
#define notRunningAdjusting 3
int     runState;
unsigned long onPeriodStart;
unsigned long totPeriodStart;


void  setRTC();
void  savePercent(int pct );
int   retrievePercent( void );
void  saveLogStatus( bool stat );
bool  retrieveLogStatus( void );
float calcPeriodRatio ( unsigned long currentT, unsigned long onT, unsigned long periodT );
float calcMovingAverage (float mA, float Xi, int & count);
void  writeHeadersToFile( void );

void setup() {
  #ifdef outputSerial
  Serial.begin(9600);
  Serial.println(F ("starting sketch"));
  #endif
  
  Andee.begin(); // Sets up the communication link between Arduino and the Annikken Andee

  pinMode(bedroomSensor, INPUT);  // declare bedroomSensor Pin as input
  pinMode(bedroomIndicator, OUTPUT);  // declare bedroomIndicator pin as output
  pinMode(officeSensor, INPUT);   // declare officeSensor Pin as input
  pinMode(kitchenSensor, INPUT);  // declare kitchenSensor Pin as input
  pinMode(furnaceSensor, INPUT);  // declare furnaceSensor Pin as input
  pinMode(relayControlPin, OUTPUT); // Configures relayControlPin for output.
  
  rtc.begin(); // connect to RealTimeClock
  
  void initializeAndeeDisplays();
  
  // check clock
  if ( ! rtc.isrunning()) {
    clockDisplay.setData((char*)"no clock");
    clockDisplay.update();
    #ifdef outputSerial
    Serial.println(F ("no clock"));
    #endif
  }
  if (Andee.isConnected() == 1) AndeeFlag = 1;
  initializeAndeeDisplays();
  
  desiredPercent = retrievePercent();
  desiredRatio =  desiredPercent / 100.0;
  logData = retrieveLogStatus( );
  if (logData) {
    writeHeadersToFile();
  }
  
  // set initial run state
  if ((digitalRead(bedroomSensor) == LOW) &&
       (digitalRead(officeSensor) == LOW) && 
  	 	 (digitalRead(kitchenSensor) == LOW) &&
  		  (digitalRead(furnaceSensor) == LOW))  {
    #ifdef outputSerial
    Serial.println(F("setting intial to RUN"));
    #endif
  	runState = Running;
    SSrunning = true;
    sprintf(operatingDisplayStatus, "%s", "Running");
    digitalWrite(relayControlPin, LOW);
  } else {
    runState = notRunning;
    SSrunning = false;
    movingAverage = 0;
    actualRatio = 0;
    sprintf(operatingDisplayStatus, "%s", "  Off  ");
    digitalWrite(relayControlPin, HIGH);
  }
}  // end of setup()

// This is the function meant to define the types and the apperance of
// all the objects on your smartphone
void initializeAndeeDisplays() {
  #ifdef outputSerial
  Serial.println(F ("     initializeAndeeDisplays"));
  #endif
  
  Andee.clear(); // Clear the screen of any previous displays
  
  btnEnableAdjustment.setId(0);
  btnEnableAdjustment.setType(BUTTON_IN);
  btnEnableAdjustment.setLocation(3, 1, ONE_QUART);
  btnEnableAdjustment.setTitle((char*)"Enable Percent Adjustment");
  btnEnableAdjustment.setColor(GREEN);
  btnEnableAdjustment.update();
  
  blowerPercent.setId(1);
  blowerPercent.setType(DATA_OUT);
  blowerPercent.setLocation(3, 2, ONE_QUART);
  blowerPercent.setTitle((char*)"Blower Percent");
  blowerPercent.setData((char*)"");
  sprintf (outputString, "Moving Average, N=%d", maxN);
  blowerPercent.setUnit((char*)outputString);
  blowerPercent.update();

  sliderAdjustPercent.setId(2);
  sliderAdjustPercent.setType(SLIDER_IN);
  sliderAdjustPercent.setLocation(3, 0, HALF);
  sliderAdjustPercent.setTitle((char*)"Requested Percent");
  sliderAdjustPercent.setColor(GREEN);
  sliderAdjustPercent.setSliderMinMax(0, 100, 0);
  sliderAdjustPercent.setSliderInitialValue(defaultPercent);
  sliderAdjustPercent.setSliderNumIntervals(100);
  sliderAdjustPercent.update();
  
  logStats.setId(3);
  logStats.setType(DATA_OUT);
  logStats.setLocation(1, 1, ONE_QUART);
  logStats.setTitle((char*)"Logging Status");
  logStats.setData((char*)"Waiting to Start");
  logStats.setColor(RED);
  logStats.setUnit((char*)"");
  logStats.update();
  
  btnToggleLog.setId(4);
  btnToggleLog.setType(BUTTON_IN);
  btnToggleLog.setLocation(1, 2, ONE_QUART);
  btnToggleLog.setTitle((char*)"Toggle logging");
  btnToggleLog.setColor(RED);
  btnToggleLog.update();
  
  clockDisplay.setId(5);
  clockDisplay.setType(DATA_OUT);
  clockDisplay.setLocation(0, 0, HALF);
  clockDisplay.setTitle((char*)"Time Stamp");
  clockDisplay.setData((char*)"");
  clockDisplay.setUnit((char*)"");
  clockDisplay.update();
  
  btnSetClock.setId(6);
  btnSetClock.setType(BUTTON_IN);
  btnSetClock.setLocation(0, 1, ONE_QUART);
  btnSetClock.setTitle((char*)"Set Clock to iPad (ignores DST)");
  btnSetClock.setColor(BLUE);
  btnSetClock.update();
     
  switchDisplay.setId(7);
  switchDisplay.setType(DATA_OUT);
  switchDisplay.setLocation(1, 0, HALF);
  switchDisplay.setTitle((char*)"Sensor Data");
  switchDisplay.setData((char*)"");
  switchDisplay.setUnit((char*)"Bedroom Office  Kitchen Furnace Running");
  switchDisplay.update();
  
  for (ndx = 0; ndx < 4; ndx++) {
    tempDisplayF[ndx].setId(8+ndx);  // Each object must have a unique ID number
    tempDisplayF[ndx].setType(DATA_OUT);  // This defines your object as a display box
    tempDisplayF[ndx].setLocation(2, ndx, ONE_QUART); // Sets the location and size of your object
    tempDisplayF[ndx].setTitle((char*)"Temp");
    tempDisplayF[ndx].setData((char*)""); // We'll update it with new analog data later.
    tempDisplayF[ndx].setUnit((char*)"deg F");  
    tempDisplayF[ndx].update();  
    tempsensor[ndx].begin(sensor_addr + ndx); // required to get clock working
        // function of I2C communication?
  }
}

void loop() {
  if (Andee.isConnected()) {
    if (! AndeeFlag) {
      initializeAndeeDisplays();
      AndeeFlag = 1; //this flag is to signal that Andee has already connected and the function setup() has run once.
      #ifdef outputSerial
      Serial.println(F ("Andee connected"));
      #endif
    }
  } else {
    if (! Andee.isConnected()) {
      if (AndeeFlag) {
         AndeeFlag = 0;
         #ifdef outputSerial
         Serial.println(F ("connect flag set to 0"));
         #endif    
      }
    }
  }
  // check for log request
  if (btnToggleLog.isPressed()) {
    btnToggleLog.ack();
    if (logData) {
      logData = false;
	  saveLogStatus( logData );
    //  	btnToggleLog.setColor(GREEN);
    }
    else {
      logData = true;
      writeHeadersToFile();
      btnToggleLog.setColor(RED);
    }
	saveLogStatus( logData );
    btnToggleLog.update();
    delay(250);
  }
  
  //  check for request to set the clock
  if (btnSetClock.isPressed()) {
      btnSetClock.ack();
      setRTC();
  }
  
  if ( desiredPercent != sliderAdjustPercent.getSliderValue(INT) ) {
    desiredPercent = sliderAdjustPercent.getSliderValue(INT);
    savePercent (desiredPercent);
    desiredRatio = desiredPercent / 100.0;
  }
  
  if (btnEnableAdjustment.isPressed()) {
    btnEnableAdjustment.ack();
    if (adjustmentEnabled) {
      adjustmentEnabled = false;
      btnEnableAdjustment.setTitle((char*)"Enable Percent Adjustment");
      btnEnableAdjustment.setColor(GREEN);
    }
    else {
      adjustmentEnabled = true;
      btnEnableAdjustment.setTitle((char*)"Disable Percent Adjustment");
      btnEnableAdjustment.setColor(RED);
    }
    btnEnableAdjustment.update();
    delay(250);
 }

  // format time stamp
  DateTime now = rtc.now();
  currentTime = now.unixtime();
  currentDay = currentTime/86400;
  if (onPeriodStart == 0 && runState == Running) {
    // should only happen at startup
    onPeriodStart = currentTime;
  }

  sprintf(timeStamp, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  clockDisplay.setData(timeStamp);
  sprintf(todayDate, "%02d/%02d/%02d", now.month(), now.day(), now.year()-2000);
  clockDisplay.setUnit(todayDate);
  clockDisplay.update();
  
  for (ndx = 0; ndx < 4; ndx++) {
    if (!tempsensor[ndx].begin(sensor_addr + ndx)) {
      degF[ndx] = -40;
      tempDisplayF[ndx].setData(degF[ndx]); 
      tempDisplayF[ndx].update(); 
    }
    // Read and print out the temperature, then convert to *F
    tempsensor[ndx].shutdown_wake(0); // try wake up here
    degF[ndx] = tempsensor[ndx].readTempC()*9/5 + 32;
    tempDisplayF[ndx].setData(degF[ndx]); 
    tempDisplayF[ndx].update(); 

    tempsensor[ndx].shutdown_wake(1); // shutdown MSP9808 - power consumption ~0.1 mikro Ampere
  }

    // check values of Sensors - what is open, what isn't
  bedroomSensorInput = digitalRead(bedroomSensor);  // read bedroomSensor Pin
  if (bedroomSensorInput == LOW) {   // If Door_Sensor N.0. (nc with magnet) -> HIGH : Door is closed 
                          // LOW : Door is open [This will be our case]
    sprintf(bedroomDisplayStatus, "%s", "Closed");
    SSbedroom = false;   //  bedroom window not open
    digitalWrite(bedroomIndicator,LOW);
  } else {
    sprintf(bedroomDisplayStatus, "%s", " Open ");
    SSbedroom = true;    //  bedroom window is open
    digitalWrite(bedroomIndicator,HIGH);
            }
  officeSensorInput = digitalRead(officeSensor);  // read officeSensor Pin
  if (officeSensorInput == LOW) {
    sprintf(officeDisplayStatus, "%s", "Closed");
    SSoffice = false;
    digitalWrite(officeIndicator,LOW);
  } else {
    sprintf(officeDisplayStatus, "%s", " Open ");
    SSoffice = true;
    digitalWrite(officeIndicator,HIGH);
  }
  kitchenSensorInput = digitalRead(kitchenSensor);  // read kitchenSensor Pin
  if (kitchenSensorInput == LOW) {
    sprintf(kitchenDisplayStatus, "%s", "Closed");
    digitalWrite(kitchenIndicator,LOW);
    if (SSkitchen) {
     doorComplete = currentTime + doorDelay;
   }
    SSkitchen = false;
  } else {
    sprintf(kitchenDisplayStatus, "%s", " Open ");
    digitalWrite(kitchenIndicator,HIGH);
    if (! SSkitchen) {
     doorComplete = currentTime + doorDelay;
   }
    SSkitchen = true;
  }
  furnaceSensorInput = digitalRead(furnaceSensor);  // read furnaceSensor Pin
  if (furnaceSensorInput == LOW) {
    sprintf(furnaceDisplayStatus, "%s", "Closed");
    digitalWrite(furnaceIndicator,LOW);
    SSfurnace = false;
  } else {
    sprintf(furnaceDisplayStatus, "%s", " Open ");
    digitalWrite(furnaceIndicator,HIGH);
    SSfurnace = true;
  }
  if ( totPeriodStart == 0 ) totPeriodStart = currentTime;
  // turn on or off the fan
  switch (runState) {
  case Running:
    actualRatio = calcPeriodRatio ( currentTime, onPeriodStart, totPeriodStart );
    #ifdef outputSerial
    Serial.print(F("actual ratio in case running: "));
    Serial.print(F("rslt, curr, on, base: "));
    dtostrf(actualRatio, 5, 3, strTemp0);
    Serial.print(strTemp0);
    Serial.print(F("  "));
    Serial.print(currentTime);
    Serial.print(F("  "));
    Serial.print(onPeriodStart);
    Serial.print(F("  "));
    Serial.println(totPeriodStart);
    #endif
    
    if ( adjustmentEnabled && ( actualRatio > desiredRatio ) ) {
      runState = notRunningAdjusting;
    }
    if ( SSbedroom || SSoffice || SSfurnace || SSkitchen &&
           ( currentTime > doorComplete ))  {
       if ( adjustmentEnabled && ( actualRatio < desiredRatio )) {
              runState = RunningAdjusting;
       } else {
          runState = notRunning;
          SSrunning = false;
          movingAverage = calcMovingAverage ( movingAverage, actualRatio, averageCount);
          actualRatio = 0;
          totPeriodStart = currentTime;
          sprintf(operatingDisplayStatus, "%s", "  Off  ");
          digitalWrite(relayControlPin, HIGH);
       }
     }
  break;
  case notRunning:
    if (! SSbedroom && ! SSoffice && ! SSfurnace &&
           ( ! SSkitchen && (currentTime > doorComplete))) {
        runState = Running;
        SSrunning = true;
        onPeriodStart = currentTime;
        sprintf(operatingDisplayStatus, "%s", "Running");
        digitalWrite(relayControlPin, LOW);
      }
  break;
  case RunningAdjusting:  // off transition but ratio not reached
    actualRatio = calcPeriodRatio ( currentTime, onPeriodStart, totPeriodStart );
    if ((actualRatio > desiredRatio ) || (! adjustmentEnabled )){
        movingAverage = calcMovingAverage ( movingAverage, actualRatio, averageCount);
        actualRatio = 0;
        totPeriodStart = currentTime;
        sprintf(operatingDisplayStatus, "%s", "  Off  ");
        digitalWrite(relayControlPin, HIGH);
        runState = notRunning;
        SSrunning = false;
    }
  break;
  case notRunningAdjusting:
    sprintf(operatingDisplayStatus, "%s", "  Off  ");
    digitalWrite(relayControlPin, HIGH);
    SSrunning = false;
    if ( SSbedroom || SSoffice || SSfurnace || SSkitchen &&
           ( currentTime > doorComplete ))  {
        movingAverage = calcMovingAverage ( movingAverage, actualRatio, averageCount);
        actualRatio = 0;
        totPeriodStart = currentTime;
        runState = notRunning;
     }
  break;
  default:
  break;
}

  dtostrf(100.0 * movingAverage, 5, 2, strTemp0);
  blowerPercent.setData(strTemp0);
  blowerPercent.update();
  delay(250);
  #ifdef outputSerial
  Serial.print (F ("mov avg: "));
  Serial.print (strTemp0);
  sprintf(outputString, "  dsrd: %d  act:  ", desiredPercent);
  Serial.print (outputString);
  dtostrf(100.0 * actualRatio, 5, 3, strTemp0);
  Serial.print (strTemp0);
  Serial.print (F ("  "));
  sprintf (outputString, "furn = %d ", SSfurnace);
  Serial.println(outputString);
  #endif
    
  sprintf (outputString, "%s %s %s %s %s", bedroomDisplayStatus, officeDisplayStatus,
          kitchenDisplayStatus, furnaceDisplayStatus, operatingDisplayStatus);
  switchDisplay.setData(outputString);
  switchDisplay.update();
  delay(250);
    
  if (((currentTime >= loggedTime + interval) || firstTime ) && logData && (offset != -1))  {
  
    loggedTime = currentTime;
    if (! firstTime) {
      #ifdef outputSerial
      sprintf(outputString, "%d  ", offset);
      Serial.print(outputString);
      sprintf(outputString, "%s, %s, ", todayDate, timeStamp);
      Serial.print(outputString);
      sprintf(outputString, " %u, %u, %u, %u, %u\n", SSbedroom,
          SSoffice, SSkitchen, SSfurnace, SSrunning);
      Serial.print(outputString);
      #endif
      sprintf(outputString, "%lu,%s,", currentDay + 24107,  timeStamp);
      offset = Andee.appendSD(filename, outputString, errorMsgBuffer);
          // note that day is exact for Excel, 24107 is delta to make date relative to 1/2/1904
          // time is presented as text to avoid dividing by 86400 in Excel to display correctly
      // append remainder of log - it appears that writing all in one action injects extra characters
      // at the head of the log entry.
      for (ndx = 0; ndx < 4; ndx++) {
        dtostrf(degF[ndx], 5, 3, strTemp0);
        if (offset != -1) { // only do second append if no error from first
          sprintf(outputString, "%s,", strTemp0);
          offset = Andee.appendSD(filename, outputString, errorMsgBuffer);
        }
      }        
      if (offset != -1) {
        dtostrf(100 * movingAverage, 5, 2, strTemp0);
        sprintf(outputString, "%s, %d,", strTemp0, averageCount);  
        offset = Andee.appendSD(filename, outputString, errorMsgBuffer);
      }
      if (offset != -1) {
        dtostrf(100 * desiredRatio, 5, 3, strTemp0);
        sprintf(outputString, "%s,", strTemp0);
        offset = Andee.appendSD(filename, outputString, errorMsgBuffer);
      }
      if (offset != -1) {
        dtostrf(100 * actualRatio, 5, 3, strTemp0);
        sprintf(outputString, "%s,%d,%d,", strTemp0, adjustmentEnabled, runState );
        offset = Andee.appendSD(filename, outputString, errorMsgBuffer);
      }
      if (offset != -1) {
        sprintf(outputString, "%u,%u,%u,%u,%u\n", SSbedroom, SSoffice, SSkitchen, SSfurnace, SSrunning);
        offset = Andee.appendSD(filename, outputString, errorMsgBuffer);        
      }
      if (offset != -1 ) {
        logCount++;
        sprintf(logStatus, "%d", logCount);
      }
    } else {
      firstTime = 0;
    }
  }
  if(offset == -1) {
     sprintf(logStatus, "%s", errorMsgBuffer);
     logData = false;
     offset = 0;
  }
   logStats.setData(logStatus);
 
  if (logData) {
    logStats.setUnit("Logging");
    logStats.setColor(GREEN);
  } else {
    logStats.setUnit((char*)"Logging OFF");
    logStats.setColor(RED);
  }
  logStats.update();
}    
void setRTC() {

/* set time to iPad time
 *  Note that time retrieved from iPad is not adjusted for Daylight Savings Time!
 */
 
  int daySet;
  int monthSet;
  int yearSet;
  int hourSet; 
  int minuteSet;
  int secondSet;

  Andee.getDeviceDate(&daySet, &monthSet, &yearSet);
  Andee.getDeviceTime(&hourSet, &minuteSet, &secondSet);
  #ifdef outputSerial
  Serial.println  (F( "Time set test"));
  sprintf(todayDate, "%d/%d/%d", monthSet, daySet, yearSet);
  Serial.println (todayDate);
  sprintf(timeStamp, "%02d:%02d:%02d", hourSet, minuteSet, secondSet);
  Serial.println (timeStamp);
  #endif
  rtc.adjust(DateTime(yearSet, monthSet, daySet, hourSet, minuteSet, secondSet));
}

void writeHeadersToFile() {
  #ifdef outputSerial
  Serial.println(F("writing headers to SD"));
  #endif
  // Write table headers to SD card
  DateTime now = rtc.now();
  sprintf(filename, "log%02d%02d%02d%02d%02d%02d.csv",now.year()-2000, now.month(), now.day(), now.hour(), now.minute(), now.second());
  // split header into parts to decrease size of output buffer
  offset = Andee.appendSD(filename, (char*)"Date,Time,Outsd,", errorMsgBuffer);
  if (offset != -1) {
    offset = Andee.appendSD(filename, (char*)"OutsdAdj,Exh,ExhAdj,", errorMsgBuffer);
  }
  if (offset != -1) {
    offset = Andee.appendSD(filename, (char*)"movingAvg, N, desired,", errorMsgBuffer);
  }
  if (offset != -1) {
    offset = Andee.appendSD(filename, (char*)"actual,adj enab,", errorMsgBuffer);
  }
  if (offset != -1) {
    offset = Andee.appendSD(filename, (char*)"run enab,", errorMsgBuffer);
  }
  if (offset != -1) {
    offset = Andee.appendSD(filename, (char*)"Bed,Off,Kit,Furn,Run\n", errorMsgBuffer);
  }  
  // Show error message on screen if there's a problem, e.g. no SD card found
  if(offset == -1)
  {
    logStats.updateData(errorMsgBuffer);
    logStats.update();
    logData = false;  // turn off logging - file problem (most likely SD card not present)
    offset = 0;  // clear offset
  }

}

void savePercent( int pct ) {
  int offset;
  char tChars[8];
  sprintf(tChars,"%d\n",pct);
  offset = Andee.writeSD( (char*)"PCTFILE.txt", tChars, 0, (char*)"pct error");
}

int retrievePercent( void ) {
  int offset;
  char tChars[9];
  int pct;

  offset = Andee.readLineFromSD( (char*)"PCTFILE.txt", 0, tChars, 9, (char*)"\n");
  if (offset != -1) { 
    pct = atoi(tChars);
  } else {
    pct = defaultPercent;
  }
  sliderAdjustPercent.moveSliderToValue(pct);
  sliderAdjustPercent.update();
  delay(250);
  return pct;
}

void saveLogStatus( bool stat ) {
  int offset;
  char tChars[3];

  if (stat) {
    sprintf (tChars, "1\n");
  } else {
    sprintf (tChars, "0\n");
  }
  offset = Andee.writeSD( (char*)"STATFILE.txt", tChars, 0, (char*)"stat error");
}

bool retrieveLogStatus( void ) {
  int offset;
  char tChars[6];
  
  offset = Andee.readLineFromSD( (char*)"STATFILE.txt", 0, tChars, 6, (char*)"\n");
  if (offset != -1) { 
  	if (tChars[0] == '1') {
  		return true;
  	} else {
  		return false;
  	}
  } else {
    return false;
  }
}

float calcPeriodRatio ( unsigned long currentT, unsigned long onT, unsigned long periodT ) {
  float result;
  if (periodT > currentT)  currentT = currentT + 86400; 
  if (periodT > onT)  onT = onT + 86400;
  result = ((float)(currentT - onT) / (float)(currentT - periodT));
  return result;
}

float calcMovingAverage (float mA, float Xi, int & count){
  if (count < maxN) count++;
  float mAout;
  mAout = (mA * (count - 1) + Xi) / count;
  return mAout;
}





