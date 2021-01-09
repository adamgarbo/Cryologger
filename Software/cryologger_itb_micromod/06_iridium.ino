// Configure SparkFun Qwiic Iridium 9603N
void configureIridium()
{
  if (modem.isConnected())
  {
    modem.adjustATTimeout(30);          // Set AT timeout (Default = 20 seconds)
    modem.adjustSendReceiveTimeout(iridiumTimeout); // Set send/receive timeout (Default = 300 seconds)
    modem.enable841lowPower(true);      // Enable ATtiny841 low-power mode
    online.iridium = true;
  }
  else
  {
    DEBUG_PRINTLN("Warning: Qwiic Iridium 9603N not detected at default I2C address. Please check wiring.");
    online.iridium = false;
  }
}

// Write data from structure to transmit buffer
void writeBuffer()
{
  messageCounter++; // Increment message counter
  transmitCounter++; // Increment data transmission counter
  moMessage.messageCounter = messageCounter; // Write message counter data to union

  // Concatenate current message with existing message(s) stored in transmit buffer
  memcpy(transmitBuffer + (sizeof(moMessage) * (transmitCounter + (retransmitCounter * transmitInterval) - 1)), moMessage.bytes, sizeof(moMessage));

  // Print MO-SBD union/structure
  printMoSbd();
  printMoSbdHex();
  //printTransmitBuffer();

  // Write zeroes to MO-SBD union/structure
  memset(&moMessage, 0, sizeof(moMessage));
}

// Transmit data using SparkFun Qwiic Iridium 9603N
void transmitData() {

  // Check if data can and should be transmitted
  if ((online.iridium && (transmitCounter == transmitInterval)) ||
      (online.iridium && firstTimeFlag))
  {

    // Change LED colour
    //setLedColour(purple);

    // Start loop timer
    unsigned long loopStartTime = millis();

    DEBUG_PRINTLN("Enabling the supercapacitor charger...");
    modem.enableSuperCapCharger(true); // Enable the supercapacitor charger

    // Wait for supercapacitor charger PGOOD signal to go HIGH for up to 2 minutes
    while ((!modem.checkSuperCapCharger()) && millis() - loopStartTime < 2UL * 60UL * 1000UL)
    {
      ISBDCallback();
    }
    DEBUG_PRINTLN("Supercapacitors charged!");

    // Enable power to the Qwiic Iridium 9603N
    modem.enable9603Npower(true);

    // Begin satellite modem operation
    DEBUG_PRINTLN("Starting modem...");
    int err = modem.begin();
    if (err != ISBD_SUCCESS)
    {
      DEBUG_PRINT("Warning: Begin failed with error ");
      DEBUG_PRINTLN(err);
      if (err == ISBD_NO_MODEM_DETECTED)
      {
        DEBUG_PRINTLN("Warning: No modem detected! Check wiring.");
        setLedColourIridium(err); // Set LED colour to appropriate return code
      }
    }
    else
    {
      // Create buffer for Mobile Terminated (MT) SBD message (270 bytes max)
      uint8_t mtBuffer[270];
      size_t mtBufferSize = sizeof(mtBuffer);
      memset(mtBuffer, 0x00, sizeof(mtBuffer)); // Clear mtBuffer array

      DEBUG_PRINTLN("Attempting to transmit message...");

      // Transmit and receieve data in binary format
      err = modem.sendReceiveSBDBinary(transmitBuffer, (sizeof(moMessage) * (transmitCounter + (retransmitCounter * transmitInterval))), mtBuffer, mtBufferSize);

      // Check if transmission was successful
      if (err != ISBD_SUCCESS)
      {
        DEBUG_PRINT("Warning: Transmission failed with error code ");
        DEBUG_PRINTLN(err);
        setLedColourIridium(err); // Set LED colour to appropriate return code
      }
      else
      {
        blinkLed(10, 100);
        DEBUG_PRINTLN("MO-SBD transmission successful!");
        setLedColourIridium(err); // Set LED colour to appropriate return code

        retransmitCounter = 0; // Clear message retransmit counter
        memset(transmitBuffer, 0x00, sizeof(transmitBuffer)); // Clear transmit buffer array

        // Check if a Mobile Terminated (MT) message was received
        // If no message is available, mtBufferSize = 0
        if (mtBufferSize > 0)
        {
          DEBUG_PRINT("MT-SBD message received. Size: ");
          DEBUG_PRINT(sizeof(mtBuffer)); DEBUG_PRINTLN(" bytes.");

          // Write incoming message buffer to union/structure
          for (int i = 0; i < sizeof(mtBuffer); i++) {
            mtMessage.bytes[i] = mtBuffer[i];

            // Print contents of mtBuffer in hexadecimal
            char tempData[50];
            DEBUG_PRINTLN("Byte\tHex");
            for (int i = 0; i < sizeof(mtBuffer); ++i)
            {
              sprintf(tempData, "%d\t0x%02X", i, mtBuffer[i]);
              DEBUG_PRINTLN(tempData);
            }
          }

          // Print MT-SBD message as stored in union/structure
          printMtSbd();

          // Check if MT-SBD message data is valid and update global variables accordingly
          if ((mtMessage.alarmInterval      >= 300  && mtMessage.alarmInterval      <= 1209600) &&
              (mtMessage.transmitInterval   >= 1    && mtMessage.transmitInterval   <= 24) &&
              (mtMessage.retransmitCounter  >= 0    && mtMessage.retransmitCounter  <= 24) &&
              (mtMessage.resetFlag          == 0    || mtMessage.resetFlag          == 255))
          {
            alarmInterval         = mtMessage.alarmInterval;      // Update alarm interval
            transmitInterval      = mtMessage.transmitInterval;   // Update transmit interval
            retransmitCounterMax  = mtMessage.retransmitCounter;  // Update max retransmit counter
            resetFlag             = mtMessage.resetFlag;          // Update force reset flag
          }
          printSettings();
        }
      }
    }

    // Store message in transmit buffer if transmission or modem begin fails
    if (err != ISBD_SUCCESS)
    {
      retransmitCounter++;
      // Reset counter if reattempt limit is exceeded
      if (retransmitCounter >= retransmitCounterMax)
      {
        retransmitCounter = 0;
        memset(transmitBuffer, 0x00, sizeof(transmitBuffer)); // Clear transmitBuffer array
      }
      setLedColourIridium(err); // Set LED colour to appropriate return code
    }

    // Put modem to sleep
    DEBUG_PRINTLN("Putting modem to sleep...");
    err = modem.sleep();
    if (err != ISBD_SUCCESS)
    {
      DEBUG_PRINT("Warning: Sleep failed error "); DEBUG_PRINTLN(err);
      setLedColourIridium(err); // Set LED colour to appropriate return code
    }

    // Disable power to Qwiic Iridium 9603N
    DEBUG_PRINTLN("Disabling power to the Qwiic Iridium 9603N...");
    modem.enable9603Npower(false);

    // Disable the supercapacitor charger
    DEBUG_PRINTLN("Disabling the supercapacitor charger...");
    modem.enableSuperCapCharger(false); // Disable the supercapacitor charger

    // Enable the ATtiny841 low power mode
    DEBUG_PRINTLN("Enabling ATtiny841 low power mode...");
    modem.enable841lowPower(true);      // Enable the ATtiny841 low power mode

    transmitCounter = 0;  // Reset transmit counter
    unsigned long loopEndTime = millis() - loopStartTime; // Stop loop timer
    iridiumTimer = loopEndTime;
    moMessage.transmitDuration = loopEndTime / 1000;

    DEBUG_PRINT("transmitDuration: "); DEBUG_PRINTLN(loopEndTime / 1000);
    DEBUG_PRINT("retransmitCounter: "); DEBUG_PRINTLN(retransmitCounter);

    DEBUG_PRINT("transmitData() function execution: "); DEBUG_PRINT(loopEndTime); DEBUG_PRINTLN(" ms");

    // Check if reset flag was transmitted
    if (resetFlag)
    {
      DEBUG_PRINTLN("Forced system reset triggered...");
      digitalWrite(LED_BUILTIN, HIGH); // Turn on LED
      while (true); // Wait for Watchdog Timer to reset system
    }
  }
}

// Non-blocking RockBLOCK callback function can be called during transmit or GNSS signal acquisition
bool ISBDCallback()
{
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > 1000)
  {
    previousMillis = currentMillis;
    petDog();  // Reset the Watchdog Timer
    readBattery(); // Measure battery voltage during Iridium transmission
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // Blink LED
  }
  return true;
}

#if DEBUG_IRIDIUM
// Callback to sniff the conversation with the Iridium modem
void ISBDConsoleCallback(IridiumSBD *device, char c) {
  DEBUG_WRITE(c);
}

// Callback to to monitor Iridium modem library's run state
void ISBDDiagsCallback(IridiumSBD *device, char c) {
  DEBUG_WRITE(c);
}
#endif

// Change LED colour to indicate return error code
void setLedColourIridium(byte err)
{
  /*
  if (err == 0) // ISBD_SUCCESS
    setLedColour(green);
  else if (err == 1) // ISBD_ALREADY_AWAKE
    setLedColour(yellow);
  else if (err == 2) // ISBD_SERIAL_FAILURE
    setLedColour(yellow);
  else if (err == 3) // ISBD_PROTOCOL_ERROR
    setLedColour(blue);
  else if (err == 4) // ISBD_CANCELLED
    setLedColour(yellow);
  else if (err == 5) // ISBD_NO_MODEM_DETECTED
    setLedColour(red);
  else if (err == 6) // ISBD_SBDIX_FATAL_ERROR
    setLedColour(yellow);
  else if (err == 7) // ISBD_SENDRECEIVE_TIMEOUT
    setLedColour(orange);
  else if (err == 8) // ISBD_RX_OVERFLOW
    setLedColour(yellow);
  else if (err == 9) // ISBD_REENTRANT
    setLedColour(yellow);
  else if (err == 10) // ISBD_IS_ASLEEP
    setLedColour(yellow);
  else if (err == 11) // ISBD_NO_SLEEP_PIN
    setLedColour(yellow);
  else if (err == 12) // ISBD_NO_NETWORK
    setLedColour(yellow);
  else if (err == 13) // ISBD_MSG_TOO_LONG
    setLedColour(yellow);
    */
}

// Call user function 1
void userFunction1()
{
  // Custom code
}

// Call user function 2
void userFunction2()
{
  // Custom code
}

// Call user function 3
void userFunction3()
{
  // Custom code
}

// Call user function 4
void userFunction4()
{
  // Custom code
}