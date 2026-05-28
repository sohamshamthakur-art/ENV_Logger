# ENV-LOGGER

## Simple OverView
In simple language, this project Connects to wifi → sets the time from NTP servers → adjusts the RTC according to it → collects data from sensors, 1 reading per minute → takes their rolling average over 5 minutes → displays them on the OLED display → logs data into SD card in CSV format and repeats it → then creates a new file after the day ends.

After setting the time once, the system is fully capable of working entirely offline!

## Technical Overview
* **Standalone Node:** Acts as a self-contained, fault-tolerant environmental data logger with minimal dependency on network availability.
* **Timekeeping:** Uses NTP to fetch accurate time and syncs it to an RTC, ensuring reliable timestamps even during power or network failures.
* **Sensor Interface:** Collects data from multiple sensors-DHT (temp&humidity), BMP180 (pressure), and MQ135 (air quality)-via I2C and analog inputs.
* **Data Processing:** Implements a moving average filter (window size = 5) to reduce noise and stabilize readings.
* **Storage:** Logs data to an SD card over SPI in CSV format, with daily file creation (YYYY-MM-DD.csv) for efficient data management.
* **Output Channels:** Displays real-time data on OLED and streams via Serial for debugging.

## The Motivation
1. Explore sensor fusion, figuring out how to make different modules (temperature, pressure, gas) play nicely together on a single microcontroller.
2. Master robust data logging, ensuring that data isn't just collected, but safely stored, formatted, and easily retrievable without data loss over long periods.

## PINOUTS

| DHT 11 | ESP32 |
| :--- | :--- |
| VCC | 3V3 |
| GND | GND |
| DATA | GPIO 04 |

| BMP180 | ESP32 |
| :--- | :--- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

| RTC DS3231 | ESP32 |
| :--- | :--- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

| OLED SSD1306 | ESP32 |
| :--- | :--- |
| VCC | 5V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

| MQ135 | ESP32 |
| :--- | :--- |
| VCC | 5V |
| GND | GND |
| AOUT | GPIO 34 |

| SD CARD MODULE | ESP32 |
| :--- | :--- |
| VCC | 3V3 |
| GND | GND |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| SCK | GPIO 18 |
| CS | GPIO 15 |

## How It Works & How to Use It

### The Logging Process:
Once powered, the system tries to get a timestamp from NTP with the help of the internet through WIFI. Once the RTC is synced, the main loop starts.
Every 60 seconds, it wakes the sensors, takes a reading and pushes that value into an array. Once 5 readings are collected, it calculates the average, updates the OLED display and logs a single row to the day's CSV file on the SD card. When the RTC hits 00:00:00, it closes the current file and generates a new one for the next day.

### Getting Started (Code Changes):
Before uploading the code to the board, you need to configure a few user-specific variables at the top of the sketch:
* **WiFi Credentials:** Enter your SSID and PASSWORD so the board can initially hit the NTP server.
* **Calibration Factors:** Adjust the MQ135 baseline RO value or any specific temperature offsets depending on your exact sensor batch.
* **Timezone Offset:** Set your GMT offset (in seconds) for the NTP client to ensure the RTC matches your local time.

### Hardware Setup
Here is the final setup of the ESP32 and sensors on the perfboard:

![ESP32 Perfboard Top View](Results%20(1).jpg)

![ESP32 Perfboard Wiring Bottom View](Results%20(2).jpg)

### Accessing Data:
Accessing the data is very simple:
1. Pop the MicroSD card out of the module.
2. Plug it into a card reader and connect it to your PC.
3. Open the .csv files directly in Excel, Google Sheets or Python for analysis!

## Future Enhancements
A few features could take it to the next level:
* **Deep Sleep Integration:** Putting the microcontroller to sleep between the 1-minute read intervals to improve battery life significantly for off-grid solar deployments.
* **Wireless Data Offloading:** Adding a routine to periodically upload the day's CSV file to a local server (via MQTT or FTP) when WiFi is available, so the SD card never needs to be manually pulled.
* **Web Dashboard:** Building a lightweight local web server on the ESP32 to view live graphs from your phone.

## Bye-bye!
Thanks for checking out the project. Happy making, and may your code compile on the first try!
