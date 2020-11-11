/*
  Bud temperature measurement with Thermocouples
  Last update: 2020/11/11
  Part of the Foliarduino project
   https://foliarduino.com
   https://github.com/mpeaucelle/FoliArduino
  Marc Peaucelle mpeau.pro@gmail.com
*/
#include <Wire.h>
#include <SPI.h>
#include <SdFat.h>  // https://github.com/greiman/SdFat
#include <RTClibExtended.h>  // https://github.com/FabioCuomo/FabioCuomo-DS3231
#include <LowPower.h> //https://github.com/rocketscream/Low-Power
#include <Adafruit_MAX31856.h>

//-------------------------------------------
// User-settable variables
int Interval = 30; // Specify the record interval in minutes (default is 15, the arduino will wake up at 0, 15, 30 and 45 minutes every hour)
int nbRep = 60 / Interval;


////// RTC module, wake-up arduino every "Interval" min to take measurements
RTC_DS3231 RTC;      //we are using the DS3231 RTC
float waketime;
float RTCtemp;
DateTime myTime;  // variable to keep time
int total_time = 0;
#define wakePin 2
byte AlarmFlag = 1;

////// SD module
#define SD_CS 4
SdFat sd;
SdFile outTxt;  // this is the text file to be written on the SD
// Declare initial name for output files written to SD card
// The newer versions of SdFat library support long filenames
char txtname[] = "YYYYMMDD_HHMMSS";
char filename[] = "YYYYMMDD_HHMMSS";

////// Thermistor
int ThermistorPin = 0; //A0
float Vo;
float R1 = 10000;
float logR2, R2, T;
float c1 = 1.009249522e-03, c2 = 2.378405444e-04, c3 = 2.019202697e-07;
float ColdJ_T1, ColdJ_T2, ColdJ_T3, ColdJ_T4 ; //°C storage of cold junction temperature form thermistor

//  Vo = analogRead(ThermistorPin);
//  R2 = R1 * (1023.0 / (float)Vo - 1.0);
//  logR2 = log(R2);
//  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2));
//  Tc = T - 273.15;


////// Thermocouple
//Adafruit_MAX31856 maxthermo = Adafruit_MAX31856(10, 11, 12, 13);
Adafruit_MAX31856 maxthermo = Adafruit_MAX31856(10);

// could be stored in struct
float HotJ_T1, HotJ_T2, HotJ_T3, HotJ_T4; //°C Hot junction temperature from thermocouple
float HotJ_cT1, HotJ_cT2, HotJ_cT3, HotJ_cT4;  //°C Cold junction temperature from thermocouple
int faulttxt1, faulttxt2, faulttxt3, faulttxt4; // store error message

////// relay
int relay1 = 5;
int relay2 = 6;
int relay3 = 7;
int relay4 = 8;

//----------------------------------------------
void setup() {
  Serial.begin(9600);
  //Set pin D2 as INPUT for accepting the interrupt signal from DS3231
  pinMode(wakePin, INPUT);

  //Initialize communication with the clock
  Wire.begin();
  RTC.begin();
  myTime = RTC.now();

  // Initialize the SD card object
  // Try SPI_FULL_SPEED, or SPI_HALF_SPEED if full speed produces
  // errors on a breadboard setup.
  if (!sd.begin(SD_CS, SPI_FULL_SPEED)) {
    // If the above statement returns FALSE after trying to
    // initialize the card, enter into this section and
    // hold in an infinite loop.
    Serial.println(F("SD card error"));
  } else {
    Serial.println(F("SD init"));
  }
  //**************************************
  // initialize thermocouple
  maxthermo.begin();
  maxthermo.setThermocoupleType(MAX31856_VMODE_G32);
  //case MAX31856_VMODE_G8: Serial.println("Voltage x8 Gain mode"); break;
  // case MAX31856_VMODE_G32: MAX31856_TCTYPE_T  Serial.println("Voltage x8 Gain mode"); break;
  //maxthermo.setConversionMode(MAX31856_CONTINUOUS);

  // Pin for relay module set as output
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT);
  pinMode(relay4, OUTPUT);
  Serial.println(F("set relay HIGH : circuit open"));
  digitalWrite(relay1, HIGH);
  digitalWrite(relay2, HIGH);
  digitalWrite(relay3, HIGH);
  digitalWrite(relay4, HIGH);
  delay(1000);
}

void loop() {

  if (AlarmFlag == 0) {
    attachInterrupt(0, wakeUp, LOW);                       //use interrupt 0 (pin 2) and run function wakeUp when pin 2 gets LOW
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);   //arduino enters sleep mode here
    detachInterrupt(0);                                    //execution resumes from here after wake-up
  }
  Serial.println(F("waking up"));


  // 1 - We set the next alarm
  Serial.println(F("Setting new alarm"));
  SetNewAlarm();

  // 2 - We measure temperature from thermocouple (hot junction) and thermistor (cold junction)

  uint8_t fault1, fault2, fault3, fault4; // error message from max31856 to be reinitialized at every measure

  //thermocouple 1
  Serial.println(F("Thermocouple 1"));
  getTT(relay1, ThermistorPin, HotJ_cT1, HotJ_T1, ColdJ_T1, fault1, faulttxt1);
  delay(1000);
  //thermocouple 2
  Serial.println(F("Thermocouple 2"));
  getTT(relay2, ThermistorPin, HotJ_cT2, HotJ_T2, ColdJ_T2, fault2, faulttxt2);
  delay(1000);
  //thermocouple 3
  Serial.println(F("Thermocouple 3"));
  getTT(relay3, ThermistorPin, HotJ_cT3, HotJ_T3, ColdJ_T3, fault3, faulttxt3);
  delay(1000);
  //thermocouple 4
  Serial.println(F("Thermocouple 4"));
  getTT(relay4, ThermistorPin, HotJ_cT4, HotJ_T4, ColdJ_T4, fault4, faulttxt4);

  delay(1000);
  // 4 - We write everything on the SD
  // Construct a file name
  initfilename();
  strcpy(txtname, filename);
  strcat(txtname, ".TXT");
  Serial.println(txtname);
  myTime = RTC.now();
  writeDATA(myTime);

  delay(1000);

  Serial.println(F("Going to sleep"));
  AlarmFlag = 0;
  // End - Set RTC- SQW pin to OFF = turn-off relay and arduino
  // going to sleep here....

  //  RTC.writeSqwPinMode(DS3231_OFF);
  //  //RTC.enableOscillator(false, false, 0);
  //  digitalWrite(RTC_SWITCH, LOW);
}


//-------------------------------------------------

void wakeUp()        // here the interrupt is handled after wakeup
{
}

//------------------------------------------------------------

//-------------SetNewAlarm--------------------
// Function to set a new alarm on the RTC module

void SetNewAlarm() {
  // Interval = 15
  DateTime now = RTC.now();
  int rep = now.minute() / Interval;
  if (rep < nbRep) {
    waketime = Interval * (rep + 1);
  } else {
    waketime = 0;
  }

  //if (now.minute() >= 45) {
  //  waketime = 0;
  //} else if (now.minute() >= 30) {
  //  waketime = 45;
  //} else if (now.minute() >= 15) {
  //  waketime = 30;
  //} else {
  //  waketime = 15;
  //}

  //clear any pending alarms
  RTC.armAlarm(1, false);
  RTC.clearAlarm(1);
  RTC.alarmInterrupt(1, false);
  RTC.armAlarm(2, false);
  RTC.clearAlarm(2);
  RTC.alarmInterrupt(2, false);

  // RTC.turnOffAlarm(1);
  // RTC.turnOffAlarm(2);

  //Set SQW pin to OFF (in my case it was set by default to 1Hz)
  //The output of the DS3231 INT pin is connected to this pin
  //It must be connected to arduino D2 pin for wake-up
  RTC.writeSqwPinMode(DS3231_OFF);

  //Set new alarm every 15 min
  RTC.setAlarm(ALM1_MATCH_MINUTES, 0, waketime, 0, 0);   //set your wake-up time here
  // Debug RTC.setAlarm(ALM1_MATCH_MINUTES, 0, 52, 0, 0);
  RTC.alarmInterrupt(1, true);
  // RTC.turnOnAlarm(1);

  Serial.print("Now minute = ");
  Serial.println(now.minute());
  Serial.print("Waketime = ");
  Serial.println(waketime);

} // End of SetNewAlarm function
//-----------------------------------------------------------------


//-------------getTT--------------------
// Function to measure temperature from the thermocouple and thermistor
void getTT(int relay, int ThermistorPin, float &HotJ_cT, float &HotJ_T, float &ColdJ_T, uint8_t &fault, int &out_txt)
{
  digitalWrite(relay, LOW);
  delay(2000);
  // We measure temperature from thermocouple (hot junction)
  HotJ_cT = maxthermo.readCJTemperature();
  HotJ_T = maxthermo.readThermocoupleTemperature();
  Serial.println(HotJ_cT);
  Serial.println(HotJ_T);

  // We measure temperature from the thermistor (cold junction)
  ColdJ_T = analogRead(ThermistorPin);
  Serial.println(ColdJ_T);
  fault = maxthermo.readFault();
  out_txt = 0;
  if (fault) {
    if (fault & MAX31856_FAULT_CJRANGE) Serial.println("Cold Junction Range Fault");
    if (fault & MAX31856_FAULT_TCRANGE) Serial.println("Thermocouple Range Fault");
    if (fault & MAX31856_FAULT_CJHIGH)  Serial.println("Cold Junction High Fault");
    if (fault & MAX31856_FAULT_CJLOW)   Serial.println("Cold Junction Low Fault");
    if (fault & MAX31856_FAULT_TCHIGH)  Serial.println("Thermocouple High Fault");
    if (fault & MAX31856_FAULT_TCLOW)   Serial.println("Thermocouple Low Fault");
    if (fault & MAX31856_FAULT_OVUV)    Serial.println("Over/Under Voltage Fault");
    if (fault & MAX31856_FAULT_OPEN)    Serial.println("Thermocouple Open Fault");
    out_txt = 1;
  }

  digitalWrite(relay, HIGH);
} // End of getTT
//-----------------------------------------------------------------


//-------------writeData--------------------
// Function to transfer data to
// a file on SD card
void writeDATA(DateTime myTime)
{
  if (!outTxt.open(txtname,  O_RDWR | O_CREAT | O_AT_END)) { //create file
    Serial.println(F("SD card error text file"));
  }
  /////// format data to csv with sep = ";"

  outTxt.println("Thermocouple 1,Thermocouple 2,Thermocouple 3,Thermocouple 4");
  outTxt.println("Hot T;" + String(HotJ_T1) + ";" + String(HotJ_T2) + ";" + String(HotJ_T3) + ";" + String(HotJ_T4));
  outTxt.println("MAX T;" + String(HotJ_cT1) + ";" + String(HotJ_cT2) + ";" + String(HotJ_cT3) + ";" + String(HotJ_cT4));
  outTxt.println("Fault;" + String(faulttxt1) + ";" + String(faulttxt2) + ";" + String(faulttxt3) + ";" + String(faulttxt4));
  outTxt.println("Cold T;" + String(ColdJ_T1) + ";" + String(ColdJ_T2) + ";" + String(ColdJ_T3) + ";" + String(ColdJ_T4));

  // Update the file's creation date, modify date, and access date.
  outTxt.timestamp(T_CREATE, myTime.year(), myTime.month(), myTime.day(),
                   myTime.hour(), myTime.minute(), myTime.second());
  outTxt.timestamp(T_WRITE, myTime.year(), myTime.month(), myTime.day(),
                   myTime.hour(), myTime.minute(), myTime.second());
  outTxt.timestamp(T_ACCESS, myTime.year(), myTime.month(), myTime.day(),
                   myTime.hour(), myTime.minute(), myTime.second());

  Serial.println(F("Data transfered on SD card"));

  outTxt.close();
}   // End of writeDATA function
//-----------------------------------------------------------

// ------------ initfilename --------------------------------
// Function to generate a new output file name based on the
// current date and time from the real time clock.
// The character array 'filename' needs to be declared as a
// global variable at the top of the program, and this function
// will overwrite portions of it with the new date/time info
void initfilename() {

  DateTime time1;
  time1 = RTC.now();
  char buf[5];
  // integer to ascii function itoa(), supplied with numeric year value,
  // a buffer to hold output, and the base for the conversion (base 10 here)
  itoa(time1.year(), buf, 10);
  // copy the ascii year into the filename array
  for (byte i = 0; i <= 4; i++) {
    filename[i] = buf[i];
  }
  // Insert the month value
  if (time1.month() < 10) {
    filename[4] = '0';
    filename[5] = time1.month() + '0';
  } else if (time1.month() >= 10) {
    filename[4] = (time1.month() / 10) + '0';
    filename[5] = (time1.month() % 10) + '0';
  }
  // Insert the day value
  if (time1.day() < 10) {
    filename[6] = '0';
    filename[7] = time1.day() + '0';
  } else if (time1.day() >= 10) {
    filename[6] = (time1.day() / 10) + '0';
    filename[7] = (time1.day() % 10) + '0';
  }
  // Insert an underscore between date and time
  filename[8] = '_';
  // Insert the hour
  if (time1.hour() < 10) {
    filename[9] = '0';
    filename[10] = time1.hour() + '0';
  } else if (time1.hour() >= 10) {
    filename[9] = (time1.hour() / 10) + '0';
    filename[10] = (time1.hour() % 10) + '0';
  }
  // Insert minutes
  if (time1.minute() < 10) {
    filename[11] = '0';
    filename[12] = time1.minute() + '0';
  } else if (time1.minute() >= 10) {
    filename[11] = (time1.minute() / 10) + '0';
    filename[12] = (time1.minute() % 10) + '0';
  }
  // Insert seconds
  if (time1.second() < 10) {
    filename[13] = '0';
    filename[14] = time1.second() + '0';
  } else if (time1.second() >= 10) {
    filename[13] = (time1.second() / 10) + '0';
    filename[14] = (time1.second() % 10) + '0';
  }
}   // End of initfilename function
//-----------------------------------------------------------------
