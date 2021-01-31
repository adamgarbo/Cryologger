// Configure SparkFun GPS Breakout SAM-M8Q
void configureGnss()
{
  if (gnss.begin())
  {
    gnss.setI2COutput(COM_TYPE_UBX);  // Set I2C port to output UBX only
    gnss.saveConfiguration();         // Save current settings to Flash and BBR
    online.gnss = true;
  }
  else
  {
    DEBUG_PRINTLN("Warning: u-blox GNSS not detected at default I2C address. Please check wiring.");
    online.gnss = false;
    setLedColour(red); // Change LED colour to indicate
    //while (1);
  }
}

// Read the GNSS receiver
void readGnss()
{
  // Start loop timer
  unsigned long loopStartTime = millis();

  // Check if GNSS initialized successfully
  if (online.gnss)
  {
    bool gnssFixFlag = false; // Reset GNSS fix flag
    byte gnssFixCounter = 0; // Reset GNSS fix counter
    bool rtcSyncFlag = false; // Clear RTC sync flag

    setLedColour(cyan); // Change LED colour to indicate signal acquisition

    DEBUG_PRINTLN("Acquiring GNSS fix...");

    // Attempt to acquire a valid GNSS position fix
    while (!gnssFixFlag && millis() - loopStartTime < gnssTimeout * 1000UL)
    {
      byte fixType = gnss.getFixType();
      bool timeValidFlag = gnss.getTimeValid();
      bool dateValidFlag = gnss.getDateValid();

#if DEBUG_GNSS
      char gnssBuffer[75];
      sprintf(gnssBuffer, "%04u-%02d-%02d %02d:%02d:%02d,%ld,%ld,%d,%d,%d,%d,%d",
              gnss.getYear(), gnss.getMonth(), gnss.getDay(),
              gnss.getHour(), gnss.getMinute(), gnss.getSecond(),
              gnss.getLatitude(), gnss.getLongitude(), gnss.getSIV(),
              gnss.getPDOP(), fixType, timeValidFlag, dateValidFlag);
      DEBUG_PRINTLN(gnssBuffer);
#endif

      // Check if GNSS fix is valid
      if (fixType == 3)
      {
        gnssFixCounter += 2; // Increment fix counter
      }
      else if (fixType == 2)
      {
        gnssFixCounter += 1; // Increment counter
      }

      // If GNSS fix threshold has been reached record GNSS position
      if (gnssFixCounter >= gnssFixCounterMax)
      {
        DEBUG_PRINTLN("A GNSS fix was found!");
        gnssFixFlag = true; // Set fix flag

        // Write data to union
        moMessage.latitude = gnss.getLatitude();
        moMessage.longitude = gnss.getLongitude();
        moMessage.satellites = gnss.getSIV();
        moMessage.pdop = gnss.getPDOP();

        // Attempt to sync RTC with GNSS
        if ((fixType == 3) && timeValidFlag && dateValidFlag)
        {
          // Convert to Unix Epoch time
          tmElements_t tm;
          tm.Year = gnss.getYear() - 1970;
          tm.Month = gnss.getMonth();
          tm.Day = gnss.getDay();
          tm.Hour = gnss.getHour();
          tm.Minute = gnss.getMinute();
          tm.Second = gnss.getSecond();
          time_t gnssEpoch = makeTime(tm);
          rtc.updateTime(); // Update time variables from RTC

          // Calculate drift
          int rtcDrift = rtc.getEpoch() - gnssEpoch;

          DEBUG_PRINT("RTC drift: "); DEBUG_PRINTLN(rtcDrift);

          // Write data to union
          moMessage.rtcDrift = rtcDrift;

          // Set RTC date and time
          rtc.setTime(gnss.getHour(), gnss.getMinute(), gnss.getSecond(), gnss.getMillisecond() / 10,
                      gnss.getDay(), gnss.getMonth(), gnss.getYear() - 2000);

          rtcSyncFlag = true; // Set flag
          DEBUG_PRINT("RTC time synced to: "); printDateTime();
          setLedColour(green); // Change LED colour to indicate GNSS fix was found
        }
        else
        {
          DEBUG_PRINTLN("Warning: RTC not synced due to invalid GNSS fix!");
        }
      }
      ISBDCallback();
    }

    // Check if a valid GNSS fix was acquired
    if (!gnssFixFlag)
    {
      DEBUG_PRINTLN("Warning: No GNSS fix was found!");
      setLedColour(red); // Change LED colour to indicate no GNSS fix found
    }
  }
  else
  {
    DEBUG_PRINTLN("Warning: u-blox GNSS offline!");
  }

  // Stop the loop timer
  timer.gnss = millis() - loopStartTime;
}

// Synchornize the RTC with the GNSS
void syncRtc()
{
  // Start loop timer
  unsigned long loopStartTime = millis();

  bool rtcSyncFlag = false;       // Clear RTC sync flag
  bool timeValidityFlag = false;  // Clear time and date validity flag
  byte timeValidityCounter = 0;   // Reset time validity counter

  DEBUG_PRINTLN("Attempting to sync RTC with GNSS...");

  setLedColour(pink); // Change LED colour to indicate RTC sync

  // Sync RTC with GNSS
  while (!timeValidityFlag && millis() - loopStartTime < rtcSyncTimeout * 1000UL)
  {
    byte fixType = gnss.getFixType();
    bool timeValidFlag = gnss.getTimeValid();
    bool dateValidFlag = gnss.getDateValid();

    // Check for GNSS fix and valid time and date flags
    if ((fixType == 3) && timeValidFlag && dateValidFlag)
    {
      timeValidityCounter += 2; // Increment counter
    }
    else if ((fixType == 2) && timeValidFlag && dateValidFlag)
    {
      timeValidityCounter += 1; // Increment counter
    }

    // Sync RTC with GNSS if date and time are valid
    if (timeValidityCounter >= 10)
    {
      timeValidityFlag = true; // Set flag

      // Convert to Unix Epoch time
      tmElements_t tm;
      tm.Year = gnss.getYear() - 1970;
      tm.Month = gnss.getMonth();
      tm.Day = gnss.getDay();
      tm.Hour = gnss.getHour();
      tm.Minute = gnss.getMinute();
      tm.Second = gnss.getSecond();
      time_t gnssEpoch = makeTime(tm);
      rtc.updateTime(); // Update time variables from RTC

      // Calculate drift
      int rtcDrift = rtc.getEpoch() - gnssEpoch;

      DEBUG_PRINT("RTC drift: "); DEBUG_PRINTLN(rtcDrift);

      // Write data to union
      moMessage.rtcDrift = rtcDrift;

      // Set RTC date and time
      rtc.setTime(gnss.getHour(), gnss.getMinute(), gnss.getSecond(), gnss.getMillisecond() / 10,
                  gnss.getDay(), gnss.getMonth(), gnss.getYear() - 2000);

      rtcSyncFlag = true; // Set flag
      setLedColour(green); // Change LED colour to indicate RTC was synced
      DEBUG_PRINT("RTC time synced to: "); printDateTime();
    }
    ISBDCallback();
  }
  if (!rtcSyncFlag)
  {
    DEBUG_PRINTLN("Warning: RTC sync failed!");
    setLedColour(orange); // Change LED colour to indicate RTC was synced
  }
  // Stop loop timer
  timer.sync = millis() - loopStartTime;
}
