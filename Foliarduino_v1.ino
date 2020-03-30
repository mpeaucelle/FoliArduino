/*
  Foliarduino V.1, 2019
  https://foliarduino.com
  Marc Peaucelle foliarduino@gmail.com
*/
////////// Transistor check https://forum.arduino.cc/index.php?topic=319975.0

#include <Wire.h>
#include <SPI.h>
#include <RTClibExtended.h>  // https://github.com/FabioCuomo/FabioCuomo-DS3231
//#include <RTClib.h> // https://github.com/adafruit/RTClib
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <SdFat.h>  // https://github.com/greiman/SdFat
#include <ArduCAM.h> // https://github.com/ArduCAM/Arduino
#include "memorysaver.h"

//-------------------------------------------
// User-settable variables
int dawnTime = 6;  // Specify hour before which no pictures should be taken
int duskTime = 20; // Specify hour after which no pictures should be taken
int Interval = 15; // Specify the record interval in minutes (default is 15, the arduino will wake up at 0, 15, 30 and 45 minutes every hour)
int nbRep = 60 / Interval;
// A normal picture+save operation takes ~ 5-8 seconds
int cameraWarmUpTime = 3000; // (ms) Delay to let camera stabilize after waking

// OV2640 resolution and format (default = JPEG) can be specify in setup{}
// choices for resolution are
// OV2640_160x120
// OV2640_176x144
// OV2640_320x240
// OV2640_352x288
// OV2640_640x480
// OV2640_800x600
// OV2640_1024x768
// OV2640_1280x1024
// OV2640_1600x1200 (default)

// For humidity sensor, maximum value has to be calibrated for each sensor!
// See WaterValue_s1, WaterValue_s2, AirValue_s1 and AirValue_s2
//-------------------------------------------

////// RTC module, wake-up arduino every "Interval" min to take measurements
RTC_DS3231 RTC;      //we are using the DS3231 RTC
float waketime;
float RTCtemp;
DateTime myTime;  // variable to keep time
int total_time = 0;

////// Soil temperature and humidity sensors at 10 and 50 cm depth
////// DS1820 soil temperature sensors
// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
// Addresses of 2 DS18B20s
uint8_t sensor1[8] = { 0x28, 0xEE, 0xD5, 0x64, 0x1A, 0x16, 0x02, 0xEC };
uint8_t sensor2[8] = { 0x28, 0x61, 0x64, 0x12, 0x3C, 0x7C, 0x2F, 0x27 };

float Soiltemp1; // storage of soil temperature 1
float Soiltemp2; // storage of soil temperature 2

////// Soil humidity capacitive sensors
const int WaterValue_s1 = 337;  //you need to replace this value with measured water Value_1
const int WaterValue_s2 = 337;  //you need to replace this value with measured water Value_2
int soilMoistureValue1 = 0;
int soilMoistureValue2 = 0;

////// Air temperature and humidity sensor
////// BME280 air humidity temperature and pressure
#define SEALEVELPRESSURE_HPA (1013.25)
Adafruit_BME280 bme;
const int AirValue_s1 = 630;   //you need to replace this value with measured air Value_1
const int AirValue_s2 = 630;   //you need to replace this value with measured air Value_2

float Airtemp; //°C storage of air temperature
float Airpres; //hPa storage of atmospheric pressure
float Airhum;  //% storage of air relative humidity
float Altitude; //m storage of altitude

////// OV2670 sensor
const int SPI_CS = 7 ;
ArduCAM myCAM(OV2640, SPI_CS); // Defines camera module type and chip select pin
uint8_t start_capture = 0;

////// SD module
#define SD_CS 53
SdFat sd;
SdFile outFile;  // this is the Jpeg picture to be written on the SD
SdFile outTxt;   // same, I use another variable name for more clarity

// Declare initial name for output files written to SD card
// The newer versions of SdFat library support long filenames
char txtname[] = "YYYYMMDD_HHMMSS"; 
char picname[] = "YYYYMMDD_HHMMSS";
char filename[] = "YYYYMMDD_HHMMSS";

void setup() {
  Serial.begin(9600);
  uint8_t vid, pid;
  uint8_t temp;
  //Initialize communication with the clock
  Wire.begin();
  RTC.begin();
  myTime = RTC.now();
  //RTC.writeSqwPinMode(DS3231_SquareWave1Hz);

  // soil temperature
  sensors.begin();

  // air humidity
  bme.begin(0x76);

  // Initialize the OV2640 camera
  //set the CS as an output:
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);
  // initialize SPI:
  SPI.begin();

  //***********************************
  // Initialize Arducam module
  //Reset the CPLD
  myCAM.write_reg(0x07, 0x80);
  delay(100);
  myCAM.write_reg(0x07, 0x00);
  delay(100);

  while (1) {
    //Check if the ArduCAM SPI bus is OK
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    temp = myCAM.read_reg(ARDUCHIP_TEST1);
    if (temp != 0x55) {
      Serial.println(F("SPI interface Error!"));
      delay(1000); continue;
    } else {
      Serial.println(F("SPI interface OK.")); break;
    }
  }

  while (1) {
    //Check if the camera module type is OV2640
    myCAM.wrSensorReg8_8(0xff, 0x01);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
    myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
    if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 ))) {
      Serial.println(F("Can't find OV2640 module!"));
      delay(1000); continue;
    }
    else {
      Serial.println(F("OV2640 detected.")); break;
    }
  }
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  myCAM.OV2640_set_JPEG_size(OV2640_1600x1200);

  //**************************************
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
  delay(1000);
}

void loop() {
  // 1 - We set the next alarm
  SetNewAlarm();
  delay(100);
  // 2 - We measure temperature in the hardcase
  RTCtemp = RTC.getTemp();

  // 3 - We measure soil temperature
  sensors.requestTemperatures();
  Soiltemp1 = sensors.getTempC(sensor1);
  Soiltemp2 = sensors.getTempC(sensor2);

  // 4 - We measure capacitive soil moisture
  soilMoistureValue1 = analogRead(A4);
  soilMoistureValue2 = analogRead(A5);

  // 5 - We measure air humidity, pressure and temperature
  Airtemp = bme.readTemperature(); //°C
  Airpres = bme.readPressure() / 100.0F; //hPa
  Airhum  = bme.readHumidity();	//%
  Altitude = bme.readAltitude(SEALEVELPRESSURE_HPA); //m

  // 6 - We measure incomming light and temperature in the pyranometer
  // To be added, still working on it

  // 7 - We write everything on the SD
  // Construct a file name
  initfilename();
  strcpy(txtname,filename);
  strcat(txtname,".TXT");
  Serial.println(txtname);
  myTime = RTC.now();
  writeMETEO(myTime);

  // 8 - We take a picture with the OV2670 sensor and save it on the SD
  if (myTime.hour() >= dawnTime && myTime.hour() <= duskTime) {
    // we only enable the cam now to avoid any problem with the SPI bus and the SD card.
    TakePicture();
  }

  delay(50000);
  // End - Set RTC- SQW pin to OFF = turn-off relay and arduino
  RTC.writeSqwPinMode(DS3231_OFF);
  //RTC.enableOscillator(false, false, 0);
}

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
  //Set new alarm every 15 min

  RTC.setAlarm(ALM1_MATCH_MINUTES, 0, waketime, 0, 0);   //set your wake-up time here
  // Debug RTC.setAlarm(ALM1_MATCH_MINUTES, 0, 52, 0, 0);
  RTC.alarmInterrupt(1, true);
  // RTC.turnOnAlarm(1);

  Serial.println(now.minute());
  Serial.print("Waketime = ");
  Serial.println(waketime);

} // End of SetNewAlarm function
//-----------------------------------------------------------------

//-------------writeMETEO--------------------
// Function to transfer meteo data to
// a file on SD card
void writeMETEO(DateTime myTime)
{
  
  if (!outTxt.open(txtname,  O_RDWR | O_CREAT | O_AT_END)) { //create file
    Serial.println(F("SD card error text file"));
  }
  /////// RTC temperature
  outTxt.print("RTC_T;");
  outTxt.println(RTCtemp);
  /////// Soil temperature and moisture
  outTxt.print("Soil_T1;");
  outTxt.println(Soiltemp1);
  outTxt.print("Soil_T2;");
  outTxt.println(Soiltemp2);
  outTxt.print("Soil_M1;");
  outTxt.println(soilMoistureValue1);
  outTxt.print("Soil_M2;");
  outTxt.println(soilMoistureValue2);
  /////// Air temperature, moisture, pressure
  outTxt.print("Air_T;");
  outTxt.println(Airtemp);
  outTxt.print("Air_P;");
  outTxt.println(Airpres);
  outTxt.print("Air_HR;");
  outTxt.println(Airhum);

  // Update the file's creation date, modify date, and access date.
  outTxt.timestamp(T_CREATE, myTime.year(), myTime.month(), myTime.day(),
                   myTime.hour(), myTime.minute(), myTime.second());
  outTxt.timestamp(T_WRITE, myTime.year(), myTime.month(), myTime.day(),
                   myTime.hour(), myTime.minute(), myTime.second());
  outTxt.timestamp(T_ACCESS, myTime.year(), myTime.month(), myTime.day(),
                   myTime.hour(), myTime.minute(), myTime.second());

  outTxt.close();
}   // End of writeMETEO function
//-----------------------------------------------------------



//-------------TakePicture--------------------
// Function to take a picture and save it on SD card

void TakePicture() {
  char str[8];
  byte buf[256];
  static int i = 0;
  static int k = 0;
  uint8_t temp = 0, temp_last = 0;
  uint32_t length = 0;
  bool is_header = false;
  //File outFile;
  myTime = RTC.now();
  char buf1[25];
  //myTime.toString(buf1, 25);
  Serial.println(myTime.hour());
  Serial.println(myTime.minute());
  total_time = millis(); // store time used to take and store picture
  //Flush the FIFO
  myCAM.flush_fifo();
  //Clear the capture done flag
  myCAM.clear_fifo_flag();

  // Let camera exposure stabilize before picture
  delay(cameraWarmUpTime);

  //Start capture
  digitalWrite(SD_CS, LOW);
  myCAM.start_capture();
  Serial.println(F("start Capture"));
  // Check the Capture Done flag, which is stored in the ARDUCHIP_TRIG
  // register for some reason.
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
    // do nothing while waiting for capture done flag
    delay(1000);
    yield();
  }

  if ( myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK) ) {
    Serial.println(F("Capture Done"));
    digitalWrite(SD_CS, HIGH);
    //**************************************
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

    // Update the time stamp. This will be the actual time
    // value of the image capture (after the warm up), and
    // will be used in the image file name.
    myTime = RTC.now();
    strcpy(picname,filename);
    strcat(picname,".JPG");
    Serial.println(picname); 
    // Open a new file on the SD card with the filename
    if (!outFile.open(picname, O_RDWR | O_CREAT | O_AT_END)) {
      // If file cannot be created, raise an error
      Serial.println(F("SD card error"));
    }
    // Transfer data from ArduCam to the SD card
    writePIC(myTime);

    //      total_time = millis() - total_time;
    Serial.print(F("Total time used:"));
    Serial.print(total_time, DEC);
    Serial.println(F(" ms"));
  } // end of if(myCAM.get_bit(ARDUCHIP_TRIG ,CAP_DONE_MASK))
}   // End of TakePicture function
//-----------------------------------------------------------------


//-------------writePIC--------------------
// Function to transfer data from Arducam buffer to
// a file on SD card
void writePIC(DateTime myTime)
{
  byte buf[256]; // used for moving image data to SD card
  int i = 0; // used to keep track of image data saving
  uint8_t temp = 0; // used to hold each image byte
  uint8_t temp_last = 0; // used to hold previous image byte

  temp = myCAM.read_fifo(); // Get 1 byte from camera fifo buffer
  // Write first image data to buffer
  buf[i++] = temp;


  // Read JPEG data from FIFO
  // JPEGs should end with FF D9 (hex), so this
  // statement looks for that code to determine whether
  // this is the end of the file or not.
  while ( (temp != 0xD9) | (temp_last != 0xFF) )
  {
    temp_last = temp;
    temp = myCAM.read_fifo(); // read another byte
    // Write image data to buffer if not full
    if (i < 256) {
      buf[i++] = temp;
    } else {  // if i == 256, i.e. 'buf' is full
      // Write 256 bytes image data to file on SD card
      outFile.write(buf, 256);
      i = 0;  // reset buf index to 0
      buf[i++] = temp; // copy the recent byte to the start of buf
    }
  }
  // Write the remaining bytes in the buffer since the
  // previous while statement has been satisfied now
  if (i > 0) {
    outFile.write(buf, i);
  }

  // Update the file's creation date, modify date, and access date.
  outFile.timestamp(T_CREATE, myTime.year(), myTime.month(), myTime.day(),
                    myTime.hour(), myTime.minute(), myTime.second());
  outFile.timestamp(T_WRITE, myTime.year(), myTime.month(), myTime.day(),
                    myTime.hour(), myTime.minute(), myTime.second());
  outFile.timestamp(T_ACCESS, myTime.year(), myTime.month(), myTime.day(),
                    myTime.hour(), myTime.minute(), myTime.second());
  // Close the file
  outFile.close();
}   // End of writePIC function
//-----------------------------------------------------------



// ------------ initfilename --------------------------------
// Function to generate a new output file name based on the
// current date and time from the real time clock.
// The character array 'filename' needs to be declared as a
// global variable at the top of the program, and this function
// will overwrite portions of it with the new date/time info
void initfilename() {
   
  DateTime time1;
  time1=RTC.now();
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
