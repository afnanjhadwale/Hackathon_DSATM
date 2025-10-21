# 🌾 AGROSMART: AI-Powered Smart Irrigation and Water Management System

> Developed by **Afnan Jhadwale**, **Abhishek R**, **Darshan**, and **Prajwal**  
> Hackathon 2025 Project  
> Built on **ESP32**, combining IoT + AI + Web + Cloud + Telegram

---

## 📘 Overview

**AGROSMART** is an IoT-based **AI-powered smart irrigation and water management system** designed to automate irrigation using environmental sensors, weather forecasting, and intelligent decision-making.  
It monitors soil, climate, and water level in real time, and automatically controls a water pump — optimizing water usage, reducing wastage, and helping farmers manage crops efficiently.

### 💡 Key Features
- 🌦️ **Real-time Weather Integration** (OpenWeatherMap API)
- 🌱 **Soil Moisture Monitoring**
- 💧 **Tank Level Measurement** (HC-SR04 ultrasonic)
- 🔆 **Temperature & Humidity** (DHT11)
- 🧠 **AI-Based Water Budgeting** (crop, soil, temperature, humidity, rainfall)
- 🚰 **Automatic and Manual Pump Control**
- 🛰️ **Web Dashboard** (live data, 24-hour charts, profile, and control)
- 🤖 **Telegram Bot Alerts & Commands**
- 📊 **Fertilizer & Rain Prediction Advisor**
- 💾 **Flash Storage for Farmer Profile**
- ⚡ **Non-blocking Efficient Code with Safety Mechanisms**

---

## 🧭 System Architecture

The ESP32 acts as the **central controller**, collecting data from multiple sensors, processing it locally with AI logic, fetching external weather data, and making irrigation decisions.  

**High-level flow:**
1. Sensors → ESP32 (data acquisition)
2. ESP32 → OpenWeatherMap API (weather updates)
3. AI core → Calculates irrigation time & fertilizer advice
4. ESP32 → Controls pump (relay) automatically
5. ESP32 → Sends data to Web Dashboard & Telegram Bot
6. Farmer → Views dashboard, receives alerts, sends commands

---

## 🧰 Hardware Requirements

| Component | Quantity | Description |
|------------|-----------|-------------|
| ESP32 Dev Module | 1 | Wi-Fi-enabled microcontroller |
| DHT11 Sensor | 1 | Measures temperature and humidity |
| Soil Moisture Sensor | 1 | Reads soil wetness |
| HC-SR04 Ultrasonic Sensor | 1 | Measures tank water level |
| 1-Channel Relay Module | 1 | Controls the water pump |
| Water Pump (12V) | 1 | For irrigation control |
| Power Supply | 1 | 5V/12V regulated supply |
| Jumper Wires & Breadboard | — | For connections |
| External Power Source | 1 | For pump (not from ESP32) |

---

## ⚙️ Pin Configuration

| Function | GPIO Pin | Description |
|-----------|-----------|-------------|
| DHT11 Data | 4 | Temperature & Humidity Sensor |
| Soil Moisture Sensor | 34 (Analog) | Soil Wetness Level |
| Ultrasonic Trigger | 5 | Sends pulse |
| Ultrasonic Echo | 18 | Reads return pulse |
| Relay (Pump Control) | 16 | Controls pump (Active HIGH) |

---

## 🪜 Circuit Connections (Overview)

- **DHT11** → VCC (3.3V), GND, Data → GPIO 4  
- **Soil Sensor** → VCC (3.3V), GND, AO → GPIO 34  
- **HC-SR04** → VCC (5V), TRIG → GPIO 5, ECHO → GPIO 18 (use voltage divider to 3.3V)  
- **Relay Module** → IN → GPIO 16, VCC → 5V, GND → GND  
- **Pump** → Connected to relay’s normally open (NO) contacts  
- **Common Ground** → All modules must share GND with ESP32  

⚠️ **Safety Tips**
- Never power the pump from ESP32.  
- Use isolated relay modules or MOSFETs with flyback protection.  
- Double-check voltage compatibility before powering.

---

## 🧑‍💻 Software Setup

### 1. Required Libraries
Install these from **Arduino Library Manager**:
```cpp
DHT sensor library (Adafruit)
ArduinoJson
UniversalTelegramBot
Preferences (built-in)
WiFi, WebServer, WiFiClientSecure (ESP32 core)
HTTPClient
````

### 2. Configuration (edit top of code)

Before uploading, update the following:

```cpp
#define WIFI_SSID "Your_WiFi_Name"
#define WIFI_PASSWORD "Your_WiFi_Password"

#define BOT_TOKEN "Your_Telegram_Bot_Token"
const String WEATHER_API_KEY = "Your_OpenWeatherMap_API_Key";
const String WEATHER_CITY = "Bengaluru"; // or your city
```

---

## 🧩 Installation Steps (Arduino IDE)

1. **Install ESP32 Board:**

   * Go to *File → Preferences* → Add this URL:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   * Then open *Tools → Board → Boards Manager* → Install “ESP32”.

2. **Install Libraries** (Sketch → Include Library → Manage Libraries).

3. **Open the Code** (`AGROSMART.ino`).

4. **Select Board & Port**

   * Board: `ESP32 Dev Module`
   * Port: Check COM port under *Tools → Port*

5. **Upload the Code**.

6. **Open Serial Monitor** (`115200 baud`) to view logs:

   ```
   === Smart Irrigation System Starting ===
   Connecting to WiFi...
   WiFi connected!
   IP Address: 192.168.x.x
   === System Ready ===
   ```

---

## 🖥️ Web Dashboard

Once ESP32 is connected to Wi-Fi:

🔗 Open your browser and visit:
**`http://<Your_ESP32_IP_Address>`**

### Dashboard Features

* 🌡️ Temperature, 💨 Humidity, 🌱 Soil Moisture
* 💧 Tank Level in % and Liters
* 📅 Water Usage (Daily & Total)
* 🧠 AI Irrigation Duration and Recommendations
* ☔ Weather and Fertilizer Advisory
* 🧾 Farmer Profile Editor (saved to flash memory)
* 📈 24-hour triple bar chart (Soil, Temperature, Humidity)

### Example Screenshot

![Dashboard Screenshot](docs/dashboard.png)

---

## 🤖 Telegram Bot Commands

### Step 1: Create Bot

* Talk to `@BotFather` on Telegram → create bot → copy token.
* Paste token in code under `#define BOT_TOKEN`.

### Step 2: Send any message to your bot

It will register your chat ID automatically.

### Available Commands

| Command             | Description                        |
| ------------------- | ---------------------------------- |
| `/start` or `/help` | Shows all available commands       |
| `/sensors`          | Displays all sensor data           |
| `/water`            | Shows water usage and tank level   |
| `/ai`               | AI-based irrigation recommendation |
| `/fertilizer`       | Displays fertilizer recommendation |
| `/pumpon`           | Turns on pump manually (1hr limit) |
| `/pumpoff`          | Turns off pump                     |
| `/auto`             | Enables Auto Mode                  |
| `/manual`           | Enables Manual Mode                |
| `/alerts`           | Shows last 5 alerts                |

---

## 🔢 AI Logic Summary

AGROSMART’s AI module calculates:

1. **Base water need** → from crop type & land size
2. **Evaporation factor** → from temperature & humidity
3. **Soil factor** → from soil type
4. **Predicted water need** → normalized for demo tank
5. **Irrigation time** → based on soil moisture deficit and pump flow
6. **Weather forecast** → adjusts or pauses irrigation during rain
7. **Fertilizer recommendation** → based on soil & weather conditions

Example:

```
AI: Run pump for 45 seconds.
Rain expected → duration halved to 22s.
```

---

## 🧾 Example Serial Log Output

```
[INFO] Connecting to WiFi...
[INFO] WiFi connected. IP: 192.168.1.20
[INFO] DHT -> Temp: 29.3°C, Hum: 68%
[INFO] Soil Moisture: 42%
[INFO] Tank Level: 73% (0.36 L)
[AI] Recommendation: Run pump for 35s
[Pump] Started (Auto)
[ALERT] Rain expected, irrigation paused.
```

---

## 🧠 Web APIs for Developers

| Endpoint            | Method | Description                  |
| ------------------- | ------ | ---------------------------- |
| `/api/data`         | GET    | Returns all live data (JSON) |
| `/api/profile_data` | GET    | Returns farmer profile       |
| `/api/profile`      | POST   | Updates profile (JSON body)  |
| `/api/pump/on`      | POST   | Turns on pump                |
| `/api/pump/off`     | POST   | Turns off pump               |
| `/api/auto`         | POST   | Toggles auto/manual mode     |

**Example:**

```bash
curl -X POST http://192.168.x.x/api/pump/on
```

---

## 🧰 Troubleshooting

| Problem                   | Cause                          | Fix                         |
| ------------------------- | ------------------------------ | --------------------------- |
| Wi-Fi not connecting      | Wrong credentials / 5GHz Wi-Fi | Use 2.4GHz network          |
| DHT returns 0 or NAN      | Reading too fast               | Increase interval to >2s    |
| Soil readings unstable    | Resistive sensor corrosion     | Use capacitive type         |
| Ultrasonic distance wrong | 5V echo damaging ESP32         | Use voltage divider         |
| Pump not turning on       | Relay wiring / active state    | Verify RELAY_ACTIVE_STATE   |
| Telegram not working      | Bot not started                | Send first message manually |
| No weather data           | Invalid API key                | Check OpenWeatherMap key    |

---

## 🔒 Safety & Best Practices

* Use **opto-isolated relays** or **MOSFET drivers**.
* Separate power for pump and logic circuits.
* Enclose setup in a **waterproof box**.
* Add **fuse or circuit breaker** for pump line.
* Use **sensors rated for outdoor use**.

---

## 🚀 Future Improvements

* ✅ MQTT / Cloud data logging (Firebase / ThingsBoard)
* ✅ Multi-user authentication
* ✅ Integration with GSM (SMS alerts)
* ✅ Enhanced AI model for seasonal prediction
* ✅ Solar-powered operation with sleep cycles

---

## 📚 License

```
MIT License  
Copyright (c) 2025  
Developed by Afnan Jhadwale, Abhishek R, Darshan, and Prajwal  
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files...
```

---

## 📞 Contact

For technical help, collaboration, or demo requests:

**📧 Email:** [[afnanjhadwale@gmail.com](mailto:afnanjhadwale@gmail.com)]
**💻 GitHub:** [AfnanJhadwale](https://github.com/AfnanJhadwale)

---

## 🏁 Summary

AGROSMART integrates **IoT**, **AI**, and **weather intelligence** into a single platform that provides:

* **Smart irrigation automation**
* **Water budgeting**
* **Real-time monitoring**
* **Farmer-friendly interfaces**

It represents a scalable solution for **rural smart farming**, empowering farmers to **save water**, **reduce costs**, and **increase productivity**.

![System Diagram](docs/system-diagram.png)
![Hardware Setup](docs/hardware-setup.jpg)
