#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <SD.h>

LiquidCrystal_I2C screen = LiquidCrystal_I2C(0x27, 16, 2);
DHT dht(3, DHT11);

// IO/DAT, SCLK, CE/RST
ThreeWire myWire(29, 31, 27);
RtcDS1302<ThreeWire> rtc(myWire);

File file;

// GSM
HardwareSerial &gsmSerial = Serial3;

// push button pin
const byte togglePlantButton = 2;

// rain gauge
const byte rainGaugePin = 18;

// lcd/screen
const unsigned int initScreenTimeout = 2000;
const unsigned int screenTimeout = 2000;
unsigned long lastScreenRefresh = 0;

// dht
int temperature = 0;
int humidity = 0;

// clock
unsigned long lastClockRefresh = 0;
const unsigned int clockInterval = 1000;

// SD
const char *rainFilename = "rain.txt";
unsigned long lastLogTime = 0;
const unsigned int logInteval = 180000; // 3 minutes

// sensors
const unsigned int sensorReadTimeout = 2000;
unsigned long lastSensorRead = 0;

// toggle plant button
unsigned int pressDuration = 3000;
unsigned long lastButtonPress = 0;
bool hasBeenPressed = false;

// rain gauge
volatile unsigned int tipCount = 0;
bool hasTipped = false;
bool skipCountAfterInit = true;
const float oneTip = 0.013;  // 0.012884753042233359696 inch
const unsigned int resetTimeout = 180000; // 3 minutes just for simulation
const unsigned int debounceTime = 100;
unsigned long lastTippedTime = 0;

// plant
byte currentPlantSelected = 1;
struct Plant {
  char name[16];
  char nameInPlural[16];
  char filename[16];
  int temperatureThresholdUpper;
  int temperatureThresholdLower;
  float rainThreshold;
  byte maxTipCount;
} plant1, plant2, currentPlant;


// sms
byte sentMessageCount = 0;
byte maxSentMessageCount = 1;
byte sampleCount = 0;
byte maxSampleCount = 3;
bool hasBeenSetToTextMode = false;
bool isSendingNotification = false;
bool hasStartedSendingSms = false;
bool hasBeenNotifiedRain = false;
bool hasBeenNotifiedTemparature = false;
bool isReadingMessage = true;
unsigned long startedAt = 0;
unsigned long startedGettingMessageAt = 0;
const unsigned int smsTimeout = 1000;
const unsigned int commandTimeout = 1000;
char ownerNumber[16] = "+639682610713";
char inboxMessage[128];
char message[256];
char messageOrigin[32];
char prevCommand[8];
char currentCommand[8];

// gsm
const byte numOfChars = 128;
char gsmResponse[numOfChars];
bool isGsmResponseReady = false;
bool isDoneCheckingStatus = false;
bool isCheckingNetworkStatus = false;
const unsigned int gsmTimeout = 1000;
unsigned long lastStatusCheck = 0;

// icons
byte temperatureIcon[] = {
  B01110,
  B10001,
  B10111,
  B10001,
  B10111,
  B10001,
  B10001,
  B01110
};
byte humidityIcon[] = {
  B00000,
  B00100,
  B01110,
  B11111,
  B11101,
  B11101,
  B11001,
  B01110
};
byte clockIcon[] = {
  B01010,
  B01110,
  B10001,
  B10101,
  B10111,
  B10001,
  B10001,
  B01110
};
byte plantIcon[] = {
  B00100,
  B00111,
  B11110,
  B01100,
  B00100,
  B11111,
  B01110,
  B01110
};
byte rainIcon[] = {
  B01100,
  B11111,
  B11110,
  B00000,
  B10101,
  B01010,
  B00001,
  B01010
};

// common
unsigned long timeElapsed = 0;

void setup() {
  Serial.begin(9600);

  initScreen();
  initRainGauge();
  initDhtSensor();
  initTogglePlantButton();
  initPlantData();
  initRtc();
  initSd();
  initializeGsm();
  systemIsReady();
}

void loop() {
  timeElapsed = millis();
  
  getRainGaugeData();
  getSensorData();
  readTogglePlantButtonState();
  logData();
  
  displayTime();
  displayData();
  
  sendNotification();
  
  getMessage();
  parseMessage();

  resetRainGaugeData();
}

// Initializations
void initScreen() {
  screen.init();
  screen.backlight();
  screen.createChar(0, temperatureIcon);
  screen.createChar(1, humidityIcon);
  screen.createChar(2, clockIcon);
  screen.createChar(3, plantIcon);
  screen.createChar(4, rainIcon);
}
void initRainGauge() {
  pinMode(rainGaugePin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(rainGaugePin), tipCounter, FALLING);
}
void initDhtSensor() {
  screen.setCursor(2, 0);
  screen.print("Initializing");
  screen.setCursor(3, 1);
  screen.print("DHT Sensor");

  dht.begin();

  delay(initScreenTimeout);
}
void initRtc() {
  screen.clear();
  screen.setCursor(2, 0);
  screen.print("Initializing");
  screen.setCursor(6, 1);
  screen.print("RTC");

  rtc.Begin();

  delay(initScreenTimeout);
}
void initSd() {
  screen.clear();
  screen.setCursor(2, 0);
  screen.print("Initializing");
  screen.setCursor(1, 1);
  screen.print("SD card module");

  if (!SD.begin(53)) {
    screen.clear();
    screen.setCursor(1, 0);
    screen.print("SD card failed,");
    screen.setCursor(1, 1);
    screen.print("or not present");
    while (true);
  }

  prepareTextFiles();

  delay(initScreenTimeout);
}
void initPlantData() {
  strcpy(plant1.name, "tomato");
  strcpy(plant1.nameInPlural, "tomatoes");
  strcpy(plant1.filename, "plant1.txt");
  plant1.temperatureThresholdLower = 18;
  plant1.temperatureThresholdUpper = 32;
  // 0.2857142857142857/day or 2 inches/week
  plant1.rainThreshold = 0.286;
  // 22 tips max per day
  plant1.maxTipCount = 22;

  strcpy(plant2.name, "eggplant");
  strcpy(plant2.nameInPlural, "eggplants");
  strcpy(plant2.filename, "plant2.txt");
  // 21 to 32 degree celsius
  plant2.temperatureThresholdLower = 21;
  plant2.temperatureThresholdUpper = 32;
  // 0.1428571428571429/day or 1 inch/week
  plant2.rainThreshold = 0.143;
  // 11 tips max per day
  plant2.maxTipCount = 11;

  setCurrentPlantData();
}
void initTogglePlantButton() {
  pinMode(togglePlantButton, INPUT_PULLUP);
}
void initializeGsm() {
  gsmSerial.begin(9600);
  
  Serial.println(F("Connecting to cellular network"));

  screen.clear();
  screen.setCursor(1, 0);
  screen.print("Connecting to");
  screen.setCursor(0, 1);
  screen.print("cellular network");

  while (!isDoneCheckingStatus) {
    if (millis() - lastStatusCheck >= gsmTimeout) {
      lastStatusCheck = millis();
      if (!isCheckingNetworkStatus) {
        gsmSerial.print("AT+CREG?\r\n");
        isCheckingNetworkStatus = true;
      } else {
        isCheckingNetworkStatus = false;
      }
    }

    readGsmResponse(3);
    getNetworkStatus();
  }

  delay(initScreenTimeout);
}
void systemIsReady() {
  screen.clear();
  screen.setCursor(0, 0);
  screen.print("System is ready!");
  delay(initScreenTimeout);
  screen.clear();
}

void logData() {
  if (timeElapsed - lastLogTime >= logInteval) {
    lastLogTime = timeElapsed;
    
    RtcDateTime current = rtc.GetDateTime();
    file = SD.open(currentPlant.filename, FILE_WRITE);

    file.print(temperature);
    file.print("\t");

    file.print(humidity);
    file.print("\t");

    file.print(current.Hour());
    file.print(":");
    file.print(current.Minute());
    file.print("\t");

    file.print(current.Month());
    file.print("/");
    file.print(current.Day());
    file.print("/");
    file.println(current.Year());

    file.close();
  }
}

void getNetworkStatus() {
  if (!isGsmResponseReady) {
    return;
  }

  char *response;
  char *mode;
  char *networkStatus;

  response = strstr(gsmResponse, "+CREG: ");
  mode = strtok(response, ",");
  Serial.print(F("GSM response >> "));
  Serial.println(response);
  if (mode != NULL) {
    networkStatus = strtok(NULL, ",");
    if (networkStatus != NULL) {
      if (*networkStatus == '1') {
        isDoneCheckingStatus = true;
        lastStatusCheck = 0;

        Serial.println(F("Successfully connected to network!"));

        screen.clear();
        screen.setCursor(2, 0);
        screen.print("Connected to");
        screen.setCursor(4, 1);
        screen.print("network!");
        
        return;
      }
    }
  }

  isGsmResponseReady = false;
}

void getRainGaugeData() {
  if (!hasTipped) {
    return;
  }
  
  // skip first count right after initialization
  if (skipCountAfterInit) {
    tipCount = 0;
    hasTipped = false;
    skipCountAfterInit = false;
    return;
  }

  hasTipped = false;
  logRainData();
  
  if (tipCount >= currentPlant.maxTipCount && !hasBeenNotifiedRain && !isSendingNotification) {
    isSendingNotification = true;
    strcpy(messageOrigin, "rain");
    sprintf(message, "It's raining too much. Move your %s to a dry place!", currentPlant.nameInPlural);
  }
}
void logRainData() {
  RtcDateTime current = rtc.GetDateTime();
  file = SD.open(rainFilename, FILE_WRITE);

  file.print(current.Hour());
  file.print(":");
  file.print(current.Minute());
  file.print(":");
  file.print(current.Second());
  file.print("\t");

  file.print(current.Month());
  file.print("/");
  file.print(current.Day());
  file.print("/");
  file.println(current.Year());

  file.close();
}
void getSensorData() {
  if (timeElapsed - lastSensorRead >= sensorReadTimeout) {
    lastSensorRead = timeElapsed;

    getCurrentTemperature();
    getCurrentHumidity();

    checkTemperature();
  }
}
void getCurrentTemperature() {
  int dhtTemperature = dht.readTemperature();
  if (!isnan(dhtTemperature)) {
    temperature = dhtTemperature;
  }
}
void getCurrentHumidity() {
  int dhtHumidity = dht.readHumidity();
  if (!isnan(dhtHumidity)) {
    humidity = dhtHumidity;
  }
}
void checkTemperature() {
  int upperLimit = currentPlant.temperatureThresholdUpper;
  int lowerLimit = currentPlant.temperatureThresholdLower;
  
  if (temperature <= upperLimit && temperature >= lowerLimit && sampleCount > 0) {
    if (hasBeenNotifiedTemparature) {
      hasBeenNotifiedTemparature = false;
    }
    sampleCount = 0;
    return;
  }

  if (sampleCount >= maxSampleCount && !hasBeenNotifiedTemparature && !isSendingNotification) {
    isSendingNotification = true;
    strcpy(messageOrigin, "temperature");
    sprintf(message, "Current temperature is not good for your %s !", currentPlant.nameInPlural);
    sampleCount = 0;
    return;
  }
  
  sampleCount++;
}
void sendNotification() {
  if (!isSendingNotification) {
    return;
  }
  
  if (sendSms()) {
    if (sentMessageCount >= maxSentMessageCount) {
      if (!strcmp(messageOrigin, "rain")) {
        hasBeenNotifiedRain = true;
      } else {
        hasBeenNotifiedTemparature = true;  
      }
      
      isSendingNotification = false;      
      message[0] = NULL;
      sentMessageCount = 0;
      return;
    }

    sentMessageCount++;
  }
}

void getMessage() {
  if (isSendingNotification) {
    return;
  }

  if (!isReadingMessage) {
    return;
  }

  // send command to gsm
  if (timeElapsed - startedGettingMessageAt >= commandTimeout) {
    startedGettingMessageAt = timeElapsed;
    // set to text mode
    if (!hasBeenSetToTextMode) {
      gsmSerial.println("AT+CMGF=1");
      hasBeenSetToTextMode = true;
    } else {
      hasBeenSetToTextMode = false;
      gsmSerial.println("AT+CMGR=1");
    }
  }

  readGsmResponse(9);

  if (isGsmResponseReady) {
    char *message = strstr(gsmResponse, "+CMGR: \"REC READ\"");

    if (message != NULL) {
      strcpy(inboxMessage, gsmResponse);
      Serial.print(F("Message: "));
      Serial.println(inboxMessage);
      
      isReadingMessage = false;
      // immediately delete message
      gsmSerial.print("AT+CMGD=1,4\r\n");
    }

    isGsmResponseReady = false;
    gsmResponse[0] = NULL;
  }
}
void parseMessage() {
  if (isSendingNotification) {
    return;
  }
  
  if (isReadingMessage) {
     return;
  }
  
  if (strlen(inboxMessage) == 0) {
    return;
  }
  
  if (strstr(inboxMessage, ownerNumber) != NULL) {
    if (strstr(inboxMessage, "GET") != NULL) {
      isSendingNotification = true;

      char rainStatus[32];
      if (tipCount > 0) {
        strcpy(rainStatus, "Didn't rain today.");
      } else {
        strcpy(rainStatus, "It rained today.");
      }
      sprintf(message, " Humidity: %i%% \n Temperature: %i C \n Current plant: %s \n\n %s", humidity, temperature, currentPlant.name, rainStatus);
    }
  }

  inboxMessage[0] = NULL;
  isReadingMessage = true;
}

void displayData() {
  if (timeElapsed - lastScreenRefresh >= screenTimeout) {
    lastScreenRefresh = timeElapsed;
    displayTemperature();
    displayHumidity();
    displayCurrentPlant();
  }
}
void displayTemperature() {
  screen.setCursor(0, 1);
  screen.write(0);
  screen.setCursor(2, 1);
  screen.print(temperature);
  screen.setCursor(4, 1);
  screen.print("\xdf");
  screen.print("C ");
}
void displayHumidity() {
  screen.setCursor(0, 0);
  screen.write(1);
  screen.setCursor(2, 0);
  screen.print(humidity);

  if (humidity >= 10 && humidity < 100) {
    screen.setCursor(4, 0);
  }
  if (humidity >= 100) {
    screen.setCursor(5, 0);
  }

  screen.print("% ");
}
void displayTime() {
  if (timeElapsed - lastClockRefresh >= clockInterval) {
    lastClockRefresh = timeElapsed;

    RtcDateTime current = rtc.GetDateTime();
    int currentHour = current.Hour();
    int currentMinute = current.Minute();

    screen.setCursor(9, 0);
    screen.write(2);
    screen.setCursor(11, 0);
    if (currentHour < 10) {
      screen.print("0");
    }
    screen.print(currentHour);
    screen.setCursor(13, 0);
    screen.print(":");
    screen.setCursor(14, 0);
    if (currentMinute < 10) {
      screen.print("0");
    }
    screen.print(currentMinute);
  }
}
void displayCurrentPlant() {
  screen.setCursor(9, 1);
  screen.write(3);
  screen.setCursor(11, 1);
  screen.print(currentPlantSelected);
}
void resetRainGaugeData() {
  if (timeElapsed - lastTippedTime >= resetTimeout && tipCount > 0) {
    tipCount = 0;
    hasBeenNotifiedRain = false;
  }
}
void tipCounter() {  
  unsigned long currentMillis = millis();
  if (currentMillis - lastTippedTime >= debounceTime) {
    hasTipped = true;
    tipCount = tipCount + 1;
  }

  lastTippedTime = currentMillis;
}
bool sendSms() {
  if (!hasStartedSendingSms) {
    strcpy(currentCommand, "txtMode");
    strcpy(prevCommand, "txtMode");

    gsmSerial.println("AT+CMGF=1");
    Serial.println(F("Sending SMS..."));
    Serial.println(F("AT+CMGF=1"));
    startedAt = timeElapsed;
    hasStartedSendingSms = true;
  }

  if (timeElapsed - startedAt >= smsTimeout && hasStartedSendingSms) {
    startedAt = timeElapsed;
    if (!strcmp(prevCommand, "txtMode")) {
      strcpy(currentCommand, "contact");
      char contactCmd[32];
      sprintf(contactCmd, "AT+CMGS=\"%s\"", ownerNumber);
      gsmSerial.println(contactCmd); 
      
      Serial.print(F("Setting contact: "));
      Serial.println(contactCmd);
    }
    if (!strcmp(currentCommand, "contact") && strcmp(prevCommand, "txtMode")) {
      Serial.print(F("Setting message: "));
      Serial.println(message);
      
      strcpy(currentCommand, "message");
      gsmSerial.println(message);
    }
    if (!strcmp(currentCommand, "message") && strcmp(prevCommand, "contact")) {
      strcpy(currentCommand, "end");
      Serial.print(F("End"));
      gsmSerial.println((char)26);
    }
    if (!strcmp(currentCommand, "end") && strcmp(prevCommand, "message")) {
      currentCommand[0] = NULL;
      hasStartedSendingSms = false;
      return true;
    }

    strcpy(prevCommand, currentCommand);
  }

  return false;
}
void readTogglePlantButtonState() {
  byte buttonState = !digitalRead(togglePlantButton);

  if (buttonState == HIGH && !hasBeenPressed) {
    lastButtonPress = timeElapsed;
    hasBeenPressed = true;
    return;
  }

  if (timeElapsed - lastButtonPress < pressDuration && buttonState == LOW) {
    lastButtonPress = 0;
    hasBeenPressed = false;
    return;
  }

  if (timeElapsed - lastButtonPress >= pressDuration && buttonState == HIGH)  {
    toggleCurrentPlant();

    // reset
    tipCount = 0;
    hasBeenNotifiedTemparature = false;
    hasBeenNotifiedRain = false;

    lastButtonPress = 0;
    hasBeenPressed = false;
    return;
  }

}
void setCurrentPlantData() {
  if (currentPlantSelected == 1) {
      strcpy(currentPlant.name, plant1.name);
      strcpy(currentPlant.nameInPlural, plant1.nameInPlural);
      strcpy(currentPlant.filename, plant1.filename);
      currentPlant.temperatureThresholdLower = plant1.temperatureThresholdLower;
      currentPlant.temperatureThresholdUpper = plant1.temperatureThresholdUpper;
      currentPlant.rainThreshold = plant1.rainThreshold;
      currentPlant.maxTipCount = plant1.maxTipCount;
  } else {
      strcpy(currentPlant.name, plant2.name);
      strcpy(currentPlant.nameInPlural, plant2.nameInPlural);
      strcpy(currentPlant.filename, plant2.filename);
      currentPlant.temperatureThresholdLower = plant2.temperatureThresholdLower;
      currentPlant.temperatureThresholdUpper = plant2.temperatureThresholdUpper;
      currentPlant.rainThreshold = plant2.rainThreshold;
      currentPlant.maxTipCount = plant2.maxTipCount;
  }
}
void toggleCurrentPlant() {
  currentPlantSelected = currentPlantSelected == 1 ? 2 : 1;
  setCurrentPlantData();
}
void prepareTextFiles() {
  if (!SD.exists(plant1.filename)) {
    setFileHeadingsForPlant(plant1.filename);
  }

  if (!SD.exists(plant2.filename)) {
    setFileHeadingsForPlant(plant2.filename);
  }

  if (!SD.exists(rainFilename)) {
    setFileHeadingsForRain(rainFilename);
  }
}
void setFileHeadingsForPlant(char *filename) {
  file = SD.open(filename, FILE_WRITE);
  if (file) {
    file.print("Temperature \t");
    file.print("Humidity \t");
    file.print("Time \t");
    file.println("Date \t");
    file.close();
  }
}
void setFileHeadingsForRain(char *filename) {
  file = SD.open(filename, FILE_WRITE);
  if (file) { 
    char oneTipInString[16];
    char oneTipLabel[16];

    dtostrf(oneTip, 5, 3, oneTipInString);
    sprintf(oneTipLabel, "Rain (%s inches/tip) \t", oneTipInString);

    file.print("Time \t");
    file.print("Date \t");
    file.println(oneTipLabel);
    
    file.close();
  }
}
void readGsmResponse(byte maxLineCount) {
  static byte index = 0;
  static byte lineCount = 0;
  char endMarker = '\n';
  char rc;

  while (gsmSerial.available() > 0 && !isGsmResponseReady) {
    rc = gsmSerial.read();

    // receive char while not end marker
    if (rc != endMarker) {
      gsmResponse[index] = rc;
      index++;

      // buffer overrun guard
      if (index >= numOfChars) {
        index = numOfChars - 1;
      }
      return;
    }

    // if end marker
    // check line count
    if (lineCount < maxLineCount) {
      gsmResponse[index] = ' ';
      index++;
      lineCount++;

      // response is ready
    } else {
      gsmResponse[index] = NULL;
      index = 0;
      lineCount = 0;
      isGsmResponseReady = true;
    }
  }
}
