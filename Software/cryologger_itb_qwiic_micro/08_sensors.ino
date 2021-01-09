// Configure attached sensors
void configureSensors()
{
  // SparkFun BME280 Configuration
  if (bme280.beginI2C())
  {
    bme280.setMode(MODE_SLEEP); // Enter sleep mode
    online.bme280 = true;
  }
  else
  {
    DEBUG_PRINTLN("Warning: SparkFun BME280 not detected at default I2C address! Please check wiring.");
    online.bme280 = false;
  }
}

// Read attached sensors
void readSensors()
{
  // Change LED colour
  setLedColour(yellow);

  // Start the loop timer
  unsigned long loopStartTime = millis();

  // Wake sensor and return to sleep once measurement is made
  bme280.setMode(MODE_FORCED);
  // Delay required to allow sensor to perform the measurement
  while (bme280.isMeasuring() && millis() - loopStartTime < 1000);

  float temperature = bme280.readTempC();
  float humidity = bme280.readFloatHumidity();
  float pressure = bme280.readFloatPressure();

  // Write data to union
  moMessage.temperature = temperature * 100;
  moMessage.humidity = humidity * 100;
  moMessage.pressure = pressure / 10;

  // Change LED colour
  setLedColour(green);

  // Stop loop timer
  unsigned long loopEndTime = millis() - loopStartTime;
  DEBUG_PRINT("readSenors() function execution: "); DEBUG_PRINT(loopEndTime); DEBUG_PRINTLN(" ms");
}
