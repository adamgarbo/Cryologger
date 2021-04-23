/*
    Title:    Cryologger Ice Tracking Beacon (ITB) - v3.0
    Date:     April 19, 2021
    Author:   Adam Garbo

    Description:
    - Code is currently under development for the next iteration of the
    Cryologger iceberg tracking beacon to be deployed during the 2021
    Amundsen Expedition.

    Components:
    - Adafruit Feather M0
    - Adafruit Ultimate GPS Featherwing
    -
    - Rock7 RockBLOCK 9603
    - Maxtena M1621HCT-P-SMA Iridium antenna
*/

// ------------------------------------------------------------------------------------------------
// Libraries
// ------------------------------------------------------------------------------------------------
#include <Adafruit_Sensor.h>        // https://github.com/adafruit/Adafruit_Sensor
#include <Adafruit_BME280.h>        // https://github.com/adafruit/Adafruit_BME280_Library
#include <Arduino.h>                // https://github.com/arduino/ArduinoCore-samd
#include <ArduinoLowPower.h>        // https://github.com/arduino-libraries/ArduinoLowPower
#include <FastLED.h>                // https://github.com/adafruit/Adafruit_NeoPixel
#include <IridiumSBD.h>             // https://github.com/sparkfun/SparkFun_IridiumSBD_I2C_Arduino_Library
#include <LSM303.h>                 // https://github.com/pololu/lsm303-arduino
#include <RTCZero.h>                // https://github.com/arduino-libraries/RTCZero
#include <SAMD_AnalogCorrection.h>  // https://github.com/arduino/ArduinoCore-samd/tree/master/libraries/SAMD_AnalogCorrection
#include <TimeLib.h>                // https://github.com/PaulStoffregen/Time
#include <TinyGPS++.h>              // https://github.com/mikalhart/TinyGPSPlus
#include <Wire.h>                   // https://www.arduino.cc/en/Reference/Wire
#include <wiring_private.h>         // Required for creating new Serial instance with pinPeripheral() function 

// ------------------------------------------------------------------------------------------------
// Debugging macros
// ------------------------------------------------------------------------------------------------
#define DEBUG           true   // Output debug messages to Serial Monitor
#define DEBUG_GPS       false  // Output GPS debug information
#define DEBUG_IRIDIUM   true   // Output Iridium debug messages to Serial Monitor

#if DEBUG
#define DEBUG_PRINT(x)            SERIAL_PORT.print(x)
#define DEBUG_PRINTLN(x)          SERIAL_PORT.println(x)
#define DEBUG_PRINT_HEX(x)        SERIAL_PORT.print(x, HEX)
#define DEBUG_PRINTLN_HEX(x)      SERIAL_PORT.println(x, HEX)
#define DEBUG_PRINT_DEC(x, y)     SERIAL_PORT.print(x, y)
#define DEBUG_PRINTLN_DEC(x, y)   SERIAL_PORT.println(x, y)
#define DEBUG_WRITE(x)            SERIAL_PORT.write(x)

#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT_HEX(x)
#define DEBUG_PRINTLN_HEX(x)
#define DEBUG_PRINT_DEC(x, y)
#define DEBUG_PRINTLN_DEC(x, y)
#define DEBUG_WRITE(x)
#endif

// ------------------------------------------------------------------------------------------------
// Pin definitions
// ------------------------------------------------------------------------------------------------
#define PIN_VBAT            A0
#define PIN_GPS_EN          A5
#define PIN_LED             8
#define PIN_IRIDIUM_EN      6
#define PIN_IRIDIUM_RX      10 // Pin 1 RXD (Yellow wire)
#define PIN_IRIDIUM_TX      11 // Pin 6 TXD (Orange)
#define PIN_IRIDIUM_SLEEP   12 // Pin 7 OnOff (Grey)

// ------------------------------------------------------------------------------------------------
// Port configuration
// ------------------------------------------------------------------------------------------------
// Create a new UART instance and assign it to pins 10 (RX) and 11 (TX).
// For more information see: https://www.arduino.cc/en/Tutorial/SamdSercom
Uart Serial2 (&sercom1, PIN_IRIDIUM_RX, PIN_IRIDIUM_TX, SERCOM_RX_PAD_2, UART_TX_PAD_0);

#define SERIAL_PORT   Serial
#define GPS_PORT      Serial1
#define IRIDIUM_PORT  Serial2

// Attach interrupt handler to SERCOM for new Serial instance
void SERCOM1_Handler()
{
  Serial2.IrqHandler();
}

// ------------------------------------------------------------------------------------------------
// Object instantiations
// ------------------------------------------------------------------------------------------------
Adafruit_BME280   bme280;
CRGB              led[1];
IridiumSBD        modem(IRIDIUM_PORT, PIN_IRIDIUM_SLEEP);
LSM303            imu; //
RTCZero           rtc;
TinyGPSPlus       gps;

// ------------------------------------------------------------------------------------------------
// User defined global variable declarations
// ------------------------------------------------------------------------------------------------
unsigned long alarmInterval     = 3600;  // Sleep duration in seconds
unsigned int  transmitInterval  = 1;     // Messages to transmit in each Iridium transmission (340 byte limit)
unsigned int  retransmitLimit   = 5;     // Failed data transmission reattempt (340 byte limit)
unsigned int  gpsTimeout        = 180;   // Timeout for GPS signal acquisition
unsigned int  iridiumTimeout    = 180;   // Timeout for Iridium transmission (s)
bool          firstTimeFlag     = true;  // Flag to determine if the program is running for the first time

// ------------------------------------------------------------------------------------------------
// Global variable declarations
// ------------------------------------------------------------------------------------------------
const float   R1                = 9875000.0; // Resistor values of voltage divider
const float   R2                = 988600.0;
volatile bool alarmFlag         = false;  // Flag for alarm interrupt service routine
volatile bool wdtFlag           = false;  // Flag for Watchdog Timer interrupt service routine
volatile int  wdtCounter        = 0;      // Watchdog Timer interrupt counter
bool          resetFlag         = 0;      // Flag to force system reset using Watchdog Timer
uint8_t       moSbdBuffer[340];           // Buffer for Mobile Originated SBD (MO-SBD) message (340 bytes max)
uint8_t       mtSbdBuffer[270];           // Buffer for Mobile Terminated SBD (MT-SBD) message (270 bytes max)
size_t        moSbdBufferSize;
size_t        mtSbdBufferSize;
unsigned int  iterationCounter  = 0;      // Counter to track total number of code iterations (zero indicates a reset)
byte          retransmitCounter = 0;      // Counter to track Iridium 9603 transmission reattempts
byte          transmitCounter   = 0;      // Counter to track Iridium 9603 transmission intervals
unsigned int  failureCounter    = 0;      // Counter to track consecutive failed Iridium transmission attempts
unsigned long previousMillis    = 0;      // Global millis() timer
unsigned long alarmTime         = 0;      // Global epoch alarm time variable
unsigned long unixtime          = 0;      // Global epoch time variable
tmElements_t  tm;                         // Time elements variable for converting time

// ------------------------------------------------------------------------------------------------
// Data transmission unions/structures
// ------------------------------------------------------------------------------------------------
// Union to store Iridium Short Burst Data (SBD) Mobile Originated (MO) messages
typedef union
{
  struct
  {
    uint32_t  unixtime;         // UNIX Epoch time                (4 bytes)
    int16_t   temperature;      // Temperature (°C)               (2 bytes)
    uint16_t  humidity;         // Humidity (%)                   (2 bytes)
    uint16_t  pressure;         // Pressure (Pa)                  (2 bytes)
    int16_t   pitch;            // LSM303D pitch (°)              (2 bytes)
    int16_t   roll;             // LSM303D roll (°)               (2 bytes)
    uint16_t  heading;          // LSM303D heading (°)            (2 bytes)
    int32_t   latitude;         // Latitude (DD)                  (4 bytes)
    int32_t   longitude;        // Longitude (DD)                 (4 bytes)
    uint8_t   satellites;       // # of satellites                (1 byte)
    uint16_t  hdop;             // PDOP                           (2 bytes)
    int32_t   rtcDrift;         // RTC offset from GPS time       (4 bytes)
    uint16_t  voltage;          // Battery voltage (V)            (2 bytes)
    uint16_t  transmitDuration; // Previous transmission duration (2 bytes)
    byte      transmitStatus;   // Iridium return code            (1 byte)
    uint16_t  iterationCounter; // Message counter                (2 bytes)
  } __attribute__((packed));                            // Total: (38 bytes)
  uint8_t bytes[38];
} SBD_MO_MESSAGE;

SBD_MO_MESSAGE moSbdMessage;

// Union to store received Iridium SBD Mobile Terminated (MT) message
typedef union
{
  struct
  {
    uint32_t  alarmInterval;      // 4 bytes
    uint8_t   transmitInterval;   // 1 byte
    uint8_t   retransmitLimit;    // 1 byte
    uint8_t   resetFlag;          // 1 byte
  };
  uint8_t bytes[7]; // Size of message to be received in bytes
} SBD_MT_MESSAGE;

SBD_MT_MESSAGE mtSbdMessage;

// Structure to store device online/offline states
struct struct_online
{
  bool imu = false;
  bool gnss = false;
  bool bme280 = false;
} online;

// Union to store loop timers`
struct struct_timer
{
  unsigned long rtc;
  unsigned long battery;
  unsigned long sensors;
  unsigned long imu;
  unsigned long gps;
  unsigned long iridium;
} timer;

// ------------------------------------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------------------------------------
void setup()
{
  // Pin assignments
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_IRIDIUM_EN, OUTPUT);
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(PIN_GPS_EN, HIGH); // Disable power to GPS
  digitalWrite(PIN_IRIDIUM_EN, LOW); // Disable power to Iridium 9603

  // Set analog resolution to 12-bits
  analogReadResolution(12);

  // Apply ADC gain and offset error calibration correction
  analogReadCorrection(10, 2054);

  Wire.begin(); // Initialize I2C
  Wire.setClock(400000); // Set I2C clock speed to 400 kHz

#if DEBUG
  SERIAL_PORT.begin(115200); // Open serial port at 115200 baud
  blinkLed(2, 1000); // Non-blocking delay to allow user to open Serial Monitor
#endif

  DEBUG_PRINTLN();
  printLine();
  DEBUG_PRINTLN("Cryologger - Iceberg Tracking Beacon v3.0");
  printLine();

  // Configure devices
  //configureLed();       // Configure RGB LED
  configureRtc();       // Configure real-time clock (RTC)
  configureWdt();       // Configure Watchdog Timer (WDT)
  printSettings();      // Print configuration settings
  readGps();            // Synchronize RTC with GNSS
  configureIridium();   // Configure Iridium 9603 transceiver

  // Close serial port if immediately entering deep sleep
  if (!firstTimeFlag)
  {
    disableSerial();
  }
  //setLedColour(CRGB::White); // Change LED colour to indicate completion of setup
  blinkLed(5, 500);
}

// ------------------------------------------------------------------------------------------------
// Loop
// ------------------------------------------------------------------------------------------------
void loop()
{
  // Check if RTC alarm triggered or if program is running for first time
  if (alarmFlag || firstTimeFlag)
  {
    // Read the RTC
    readRtc();

    // Check if program is running for the first time
    if (!firstTimeFlag)
    {
      wakeUp();
    }

    DEBUG_PRINT("Info: Alarm trigger "); printDateTime();

    // Reconfigure devices
    configureSensors();
    configureImu();

    // Perform measurements
    petDog();         // Reset the Watchdog Timer
    readBattery();    // Read the battery voltage
    readGps();        // Read the GPS
    readImu();        // Read the IMU
    readSensors();    // Read sensors
    writeBuffer();    // Write the data to transmit buffer
    transmitData();   // Transmit data via Iridium transceiver
    printTimers();    // Print function execution timers
    setRtcAlarm();    // Set the RTC alarm

    DEBUG_PRINTLN("Info: Entering deep sleep...");
    DEBUG_PRINTLN();

    // Prepare system for sleep
    prepareForSleep();
  }

  // Check for Watchdog Timer interrupts
  if (wdtFlag)
  {
    petDog(); // Reset the Watchdog Timer
  }

  // Blink LED to indicate WDT interrupt and nominal system operation
  blinkLed(1, 25);

  // Enter deep sleep and wait for WDT or RTC alarm interrupt
  goToSleep();
}
