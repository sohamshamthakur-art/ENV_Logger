#include <Adafruit_Sensor.h>      // Base sensor library
#include <DHT.h>                  // DHT temperature & humidity sensor
#include <Wire.h>                 // I2C communication (RTC + BMP)
#include <Adafruit_BMP085.h>      // Pressure sensor library
#include <WiFi.h>                 // WiFi (for NTP sync)
#include <time.h>                 // Time functions (NTP handling)
#include "FS.h"                   // File system support
#include "SD.h"                   // SD card support
#include <SPI.h>                  // SPI communication (SD card)
#include <RTClib.h>               // RTC library
#include <Adafruit_GFX.h>         // Graphics core
#include <Adafruit_SSD1306.h>     // OLED driver

#define DHTPIN 4              // DHT data pin
#define DHTTYPE DHT11         // Sensor type
#define SD_CS 15              // SD chip select
#define AVG_WINDOW 5          // Moving average window size
#define MQ135_PIN 34          // Analog pin for MQ135
#define RL_VALUE 10000.0      // Load resistance (10k ohm)
#define R0_VALUE //add value after calibration     Sensor baseline resistance
#define ROOM_BASELINE 24.00   // Reference baseline value

const char *ssid = "name";
const char *password = "pass";

DHT dht(DHTPIN, DHTTYPE);      // DHT object
Adafruit_BMP085 bmp;           // BMP object
RTC_DS3231 rtc;                // RTC object
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

float dhtTemp;
float dhtHum;
float bmpTemp;
float bmpPressure;
float rci;

float dhtTempBuf[AVG_WINDOW];
float dhtHumBuf[AVG_WINDOW];
float bmpTempBuf[AVG_WINDOW];
float bmpPressBuf[AVG_WINDOW];
float rciBuf[AVG_WINDOW];

float dhtTempSum = 0;
float dhtHumSum = 0;
float bmpTempSum = 0;
float bmpPressSum = 0;
float rciSum = 0;

int avgIndex = 0;
int samplesFilled = 0;

float dhtTempAvg = 0;
float dhtHumAvg = 0;
float bmpTempAvg = 0;
float bmpPressAvg = 0;
float rciAvg = 0;

uint32_t lastMinuteEpoch = 0;

bool readSensors(float &t, float &h, float &bt, float &bp, float &rci);
void updateMovingAverage(float t, float h, float bt, float bp, float rci);
void printData(const DateTime &now);
void logToSD(const DateTime &now);
String getDailyFilename(const DateTime &now);
float getRelativeCO2Index();
void displayOnOLED(const DateTime &now);

void setup()
{
  Serial.begin(115200);
  delay(500);
Wire.begin(21, 22);                  // I2C pins

if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
} else {
    oled.clearDisplay();
    oled.setTextColor(WHITE);
    oled.setTextSize(1);
    oled.setCursor(20, 25);
    oled.print("Starting up...");
    oled.display();
}

analogReadResolution(12);            // 12-bit ADC
analogSetPinAttenuation(MQ135_PIN, ADC_11db);// Wider voltage range

  if (!rtc.begin())
  {
    Serial.println("RTC not found!");
    while (1)
      delay(1000);
  }

  if (rtc.lostPower())
  {
    Serial.println("RTC lost power, setting time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\nWiFi not available, proceeding offline");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      rtc.adjust(DateTime(
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday,
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec));
      Serial.println("RTC updated from NTP");
    }
    //    WiFi.disconnect(true);
    //    WiFi.mode(WIFI_OFF);
  }
  dht.begin();
  if (!bmp.begin())
  {
    Serial.println("Could not find a valid BMP085/BMP180 sensor, check wiring!");
    while (1)
    {
    }
  }

  for (int i = 0; i < AVG_WINDOW; i++)
  {
    dhtTempBuf[i] = 0;
    dhtHumBuf[i] = 0;
    bmpTempBuf[i] = 0;
    bmpPressBuf[i] = 0;
    rciBuf[i] = 0;
  }

  SPI.begin(18, 19, 23, SD_CS);

  if (!SD.begin(SD_CS))
  {
    Serial.println("SD card mount failed");
    while (1)
      ;
  }

  if (!SD.exists("/Project_01.csv"))
  {
    File file = SD.open("/Project_01.csv", FILE_WRITE);
    if (file)
    {
      file.println("epoch,Date,Time,dhtTemp,dhtHum,bmpTemp,bmpPressure,Relative CO2 Index");
      file.close();
    }
  }
}

void loop()
{

  DateTime now = rtc.now();
  uint32_t epochNow = now.unixtime();

  if (epochNow - lastMinuteEpoch >= 60)
  {
    lastMinuteEpoch = epochNow - (epochNow % 60);

    float t, h, bt, bp, rci;
    if (readSensors(t, h, bt, bp, rci))
    {
      updateMovingAverage(t, h, bt, bp, rci);
      printData(now);
      displayOnOLED(now);
      logToSD(now);
    }
  }
}

float getRelativeCO2Index()
{
  int adc = analogRead(MQ135_PIN);
  if (adc < 1)
    adc = 1;

  float rs = RL_VALUE * (4095.0 / adc - 1.0);
  return (ROOM_BASELINE * R0_VALUE) / rs;
}

bool readSensors(float &t, float &h, float &bt, float &bp, float &rci)
{
  rci = getRelativeCO2Index();
  t = dht.readTemperature();
  h = dht.readHumidity();
  bt = bmp.readTemperature();
  bp = bmp.readPressure() / 100.0;

  if (isnan(t) || isnan(h))
  {
    Serial.println("DHT read failed");
    return false;
  }
  return true;
}

void updateMovingAverage(float t, float h, float bt, float bp, float rci)
{

  dhtTempSum -= dhtTempBuf[avgIndex];
  dhtHumSum -= dhtHumBuf[avgIndex];
  bmpTempSum -= bmpTempBuf[avgIndex];
  bmpPressSum -= bmpPressBuf[avgIndex];
  rciSum -= rciBuf[avgIndex];

  dhtTempBuf[avgIndex] = t;
  dhtHumBuf[avgIndex] = h;
  bmpTempBuf[avgIndex] = bt;
  bmpPressBuf[avgIndex] = bp;
  rciBuf[avgIndex] = rci;

  dhtTempSum += t;
  dhtHumSum += h;
  bmpTempSum += bt;
  bmpPressSum += bp;
  rciSum += rci;

  avgIndex = (avgIndex + 1) % AVG_WINDOW;

  if (samplesFilled < AVG_WINDOW)
    samplesFilled++;

  dhtTempAvg = dhtTempSum / samplesFilled;
  dhtHumAvg = dhtHumSum / samplesFilled;
  bmpTempAvg = bmpTempSum / samplesFilled;
  bmpPressAvg = bmpPressSum / samplesFilled;
  rciAvg = rciSum / samplesFilled;
}

void printData(const DateTime &now)
{
  Serial.printf(
      "%04d-%02d-%02d %02d:%02d:%02d\n",
      now.year(), now.month(), now.day(),
      now.hour(), now.minute(), now.second());

  Serial.printf("DHT Temp: %.2f\n", dhtTempAvg);
  Serial.printf("Humidity: %.2f\n", dhtHumAvg);
  Serial.printf("BMP Temp: %.2f\n", bmpTempAvg);
  Serial.printf("Pressure: %.0f\n", bmpPressAvg);
  Serial.printf("Relative CO2 Index : %.2f\n\n", rciAvg);
}

String getDailyFilename(const DateTime &now)
{
  char filename[20];
  snprintf(filename, sizeof(filename),
           "/%04d-%02d-%02d.csv",
           now.year(), now.month(), now.day());
  return String(filename);
}


void logToSD(const DateTime &now)
{
  String filename = getDailyFilename(now);

  bool newFile = !SD.exists(filename);

  File file = SD.open(filename, FILE_APPEND);
  if (!file)
  {
    Serial.println("SD open failed");
    return;
  }

  if (newFile)
  {
    file.println("epoch,date,time,dhtTemp,dhtHum,bmpTemp,bmpPressure,rel_CO2_index");
  }

  file.printf(
      "%lu,%04d/%02d/%02d,%02d:%02d:%02d,%.2f,%.2f,%.2f,%.0f,%.2f\n",
      now.unixtime(),
      now.year(), now.month(), now.day(),
      now.hour(), now.minute(), now.second(),
      dhtTempAvg, dhtHumAvg, bmpTempAvg, bmpPressAvg, rciAvg);

  file.close();


}

void displayOnOLED(const DateTime &now)
{
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(WHITE);

    // Row 0 — Date & Time
    oled.setCursor(0, 0);
    oled.printf("%04d-%02d-%02d %02d:%02d",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute());

    // Row 1 — DHT Temp
    oled.setCursor(0, 14);
    oled.printf("Temp : %.1f C", dhtTempAvg);

    // Row 2 — Humidity
    oled.setCursor(0, 26);
    oled.printf("Hum  : %.1f %%", dhtHumAvg);

    // Row 3 — Pressure
    oled.setCursor(0, 38);
    oled.printf("Press: %.0f hPa", bmpPressAvg);

    // Row 4 — CO2 Index
    oled.setCursor(0, 50);
    oled.printf("CO2  : %.2f", rciAvg);

    oled.display();
}