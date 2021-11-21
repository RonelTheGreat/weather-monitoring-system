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
const char *plant1Filename = "plant1.txt";
const char *plant2Filename = "plant2.txt";
char *currentFilename = plant1Filename;
unsigned long lastLogTime = 0;
const unsigned int logInteval = 180000; // 3 minutes

// sensors
const unsigned int sensorReadTimeout = 2000;
unsigned long lastSensorRead = 0;

// toggle plant button
unsigned int pressDuration = 3000;
unsigned long lastButtonPress = 0;
bool hasBeenPressed = false;

// plant
byte currentPlant = 1;
struct Plant {
  int temperatureThreshold;
  int humidityThreshold;
} plant1, plant2;

// gsm
const byte numOfChars = 64;
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

// temporary delete later
const byte ledPin = 4;
byte ledState = LOW;

void setup() {
  Serial.begin(9600);
  pinMode(ledPin, OUTPUT);  // temporary remove later
  
  initScreen();
  initDhtSensor();
  initRtc();
  initSd();
  initPlantThreshold();
  initTogglePlantButton();
  initializeGsm();
  systemIsReady();
}

void loop() {
  Serial.begin(9600);
  timeElapsed = millis();
  readTogglePlantButtonState();
  getSensorData();
  logData();
  displayTime();
  displayData();
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
void initPlantThreshold() {
  plant1.temperatureThreshold = 38;
  plant1.humidityThreshold = 90;

  plant2.temperatureThreshold = 39;
  plant2.humidityThreshold = 91;
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

    file = SD.open(currentFilename, FILE_WRITE);

    file.print(temperature);
    file.print("\t");

    file.print(humidity);
    file.print("\t");

    file.print(0);
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

void getSensorData() {
  if (timeElapsed - lastSensorRead >= sensorReadTimeout) {
    lastSensorRead = timeElapsed;

    getCurrentTemperature();
    getCurrentHumidity();
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

void displayData() {
  if (timeElapsed - lastScreenRefresh >= screenTimeout) {
    lastScreenRefresh = timeElapsed;
    displayTemperature();
    displayHumidity();
    displayRainThreshold();
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
  screen.setCursor(13, 1);
  screen.write(3);
  screen.setCursor(15, 1);
  screen.print(currentPlant);
}
void displayRainThreshold() {
  screen.setCursor(9, 1);
  screen.write(4);
  screen.setCursor(11, 1);
  screen.print("N");
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
    ledState = !ledState;

    toggleCurrentPlant();
    toggleLogFile();
    digitalWrite(ledPin, ledState);

    lastButtonPress = 0;
    hasBeenPressed = false;
    return;
  }

}
void toggleCurrentPlant() {
  currentPlant = currentPlant == 1 ? 2 : 1;
}
void toggleLogFile() {
  currentFilename = currentPlant == 1 ? plant1Filename : plant2Filename;
}
void prepareTextFiles() {
  if (!SD.exists(plant1Filename)) {
    setFileHeadings(plant1Filename);
  }

  if (!SD.exists(plant2Filename)) {
    setFileHeadings(plant2Filename);
  }
}
void setFileHeadings(char *filename) {
  file = SD.open(filename, FILE_WRITE);
  if (file) {
    file.print("Temperature \t");
    file.print("Humidity \t");
    file.print("Rain (mm/hr) \t");
    file.print("Time \t");
    file.println("Date \t");
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
