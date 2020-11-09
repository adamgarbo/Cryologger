void configureRtc() {

  // Set RTC using the system __DATE__ and __TIME__ macros from compiler
  //rtc.setToCompilerTime();

  // Set RTC date and time
  //rtc.setTime(12, 59, 50, 0, 1, 11, 20); // 2020-11-01 12:59:50.000 (hour, minutes, seconds, hundredths, day, month, year)

  // Set the RTC alarm
  rtc.setAlarm(0, 0, 0, 0, 0, 0); // (hour, minutes, seconds, hundredth, day, month)
  /*
    Alarm modes:
    0: Alarm interrupt disabled
    1: Alarm match every year   (hundredths, seconds, minutes, hour, day, month)
    2: Alarm match every month  (hundredths, seconds, minutes, hours, day)
    3: Alarm match every week   (hundredths, seconds, minutes, hours, weekday)
    4: Alarm match every day    (hundredths, seconds, minute, hours)
    5: Alarm match every hour   (hundredths, seconds, minutes)
    6: Alarm match every minute (hundredths, seconds)
    7: Alarm match every second (hundredths)
  */
  rtc.setAlarmMode(6); // Set the RTC alarm mode
  rtc.attachInterrupt(); // Attach RTC alarm interrupt
  Serial.print(F("Alarm: ")); printAlarm();
}

void readRtc() {

  unsigned long unixtime = rtc.getEpoch();

  // Write data to union
  message.unixtime = unixtime;

  Serial.print(F("Datetime: ")); printDateTime();
  Serial.print(F("UNIX Epoch time: ")); Serial.println(unixtime);
}

void setRtcAlarm() {

  rtc.getTime();

  // Set the RTC's rolling alarm
  rtc.setAlarm((rtc.hour + alarmHours) % 24,
               (rtc.minute + alarmMinutes) % 60,
               0,
               0, rtc.dayOfMonth, rtc.month);
  rtc.setAlarmMode(5);
  //(rtc.seconds + alarmSeconds) % 60
  // Print the next RTC alarm date and time
  Serial.print("Next alarm: "); printAlarm();
}

void syncRtc() {
  
  unsigned long loopStartTime = millis(); // Loop timer
  bool dateValid = false;
  bool timeValid = false;
  rtcSyncFlag = false;

  // Attempt to sync RTC with GNSS for up to 5 minutes
  Serial.println(F("Attempting to sync RTC with GNSS..."));

  while ((!dateValid  || !timeValid) && millis() - loopStartTime < 1UL * 60UL * 1000UL) {
    
    dateValid = gps.getDateValid();
    timeValid = gps.getTimeValid();

    // Sync RTC if GNSS date and time are valid
    if (dateValid && timeValid) {
      rtc.setTime(gps.getHour(), gps.getMinute(), gps.getSecond(), gps.getMillisecond() / 10,
                  gps.getDay(), gps.getMonth(), gps.getYear() - 2000);

      rtcSyncFlag = true; // Set flag
      blinkLed(10,50);
      Serial.print(F("RTC time synced: ")); printDateTime();
    }

    blinkLed(1,500);
    //ISBDCallback();
  }
  if (rtcSyncFlag == false) {
    Serial.println(F("Warning: RTC sync failed"));
  }
}

// Print the RTC's current date and time
void printDateTime() {
  rtc.getTime();
  Serial.printf("20%02d-%02d-%02d %02d:%02d:%02d.%03d\n",
                rtc.year, rtc.month, rtc.dayOfMonth,
                rtc.hour, rtc.minute, rtc.seconds, rtc.hundredths);
}

// Print the RTC's alarm
void printAlarm() {
  rtc.getAlarm();
  Serial.printf("2020-%02d-%02d %02d:%02d:%02d.%03d\n",
                rtc.alarmMonth, rtc.alarmDayOfMonth,
                rtc.alarmHour, rtc.alarmMinute,
                rtc.alarmSeconds, rtc.alarmHundredths);
}
