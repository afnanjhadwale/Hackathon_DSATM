/*
  SmartRural Irrigation System (SRIS) - Final Integrated Code
  - Features: DHT11, Soil, HC-SR04, Pump Control, Web Dashboard (Triple Bar Chart, Full Weather), Telegram Bot, OpenWeatherMap API.
  - Optimization: Non-blocking core logic, efficient data handling, accurate water usage calculation.
*/

// ===== includes =====
#include <WiFi.h>
#include <WiFiClient.h> 
#include <HTTPClient.h> 
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <WebServer.h>
#include <DHT.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ======== CONFIG - from user ========
#define WIFI_SSID "abcd" // *CHANGE THIS*
#define WIFI_PASSWORD "12345678" // *CHANGE THIS*

#define BOT_TOKEN "8350601538:AAAHTc1fpRKBm7XEdUYXwzKBiLSq2xT3StVw" 
const String WEATHER_API_KEY = "6929ff9c9300336462813ae4059e2deac1";
const String WEATHER_CITY = "Bengaluru"; // City for OpenWeatherMap

// ===== pins =====
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define SOIL_MOISTURE_PIN 34
#define TRIG_PIN 5
#define ECHO_PIN 18
#define PUMP_PIN 16

#define RELAY_ACTIVE_STATE HIGH

// Tank geometry (conical frustum)
#define TANK_MAX_CAPACITY 0.500      // *FIXED: 0.5 Liters (500ml)*
#define TANK_HEIGHT 12.0             // cm active height
#define SENSOR_TO_FULL 2.0           // cm distance from sensor to water when full
#define SENSOR_TO_EMPTY (SENSOR_TO_FULL + TANK_HEIGHT)
#define TANK_R_BOTTOM 3.0            // cm
#define TANK_R_TOP 4.0               // cm

// pump flow estimate
#define PUMP_FLOW_RATE 0.1667  // L/sec (10 L/min)
#define MANUAL_IRRIGATION_DURATION_S 3600 // 1 hour safety limit

// Timing (ms)
const unsigned long SENSOR_READ_INTERVAL = 2000;
const unsigned long BOT_MTBS = 500; 
const unsigned long IRRIGATION_CHECK_INTERVAL = 2000; 
const unsigned long WEATHER_UPDATE_INTERVAL = 15UL * 60UL * 1000UL; 
const unsigned long HISTORY_UPDATE_INTERVAL = 3600000; // 1 hour
const unsigned long DAILY_RESET_CHECK_INTERVAL = 60000; // 1 minute 
const unsigned long ALERT_THROTTLE_MS = 30000; // 30s throttle

// ===== objects =====
DHT dht(DHT_PIN, DHT_TYPE);
WebServer server(80);
Preferences preferences;

// Telegram client
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ===== global state variables =====
unsigned long bot_lasttime = 0;
unsigned long sensor_lasttime = 0;
unsigned long irrigation_lasttime = 0;
unsigned long lastWeatherFetch = 0;
unsigned long lastHistoryUpdate = 0;
unsigned long lastAlertTime = 0;
unsigned long lastDailyResetCheck = 0;

float temperature = NAN;
float humidity = NAN;
int soilMoisturePercent = -1;
float distance = -1;
float tankLevelLiters = 0.0;
float tankLevelPercent = 0.0;
float farmEfficiencyScore = 0.0; 
float predictedWaterNeedLiters = 0.0; 

bool pumpState = false;
bool autoMode = true;
unsigned long irrigationStartTime = 0;
float irrigationDurationS = 0.0; 
float totalWaterUsed = 0.0;
float dailyWaterUsed = 0.0;

// ALERT SYSTEM
String lastAlert = ""; 
String alertHistory[5] = {"System Initialized."};
int alertHistoryCount = 1;
String TELEGRAM_CHAT_ID = "";

// Weather
String weatherMain = "N/A";
float rain1h_mm = 0.0;
float rainProb_percent = 0.0; 
float windSpeed = 0.0; 
int windDeg = 0; 
float pressure = 0.0; 
int lastDay = -1; 

// AI Outputs
String aiRecommendation = "System booting...";
String fertilizerRecommendation = "Checking...";
int daysTankLasts = 0;

// History arrays (3 data points * 24 hours = 72 total slots)
float tripleHistory[24 * 3] = {0}; 
int historyIndex = 0; 

// Farmer profile (stored in flash)
struct FarmerProfile {
  char name[50] = "Farmer";
  char phone[20] = "+91-XXXXXXXXXX";
  char location[100] = "Karnataka, India";
  float landSize = 1.0;
  char cropType[50] = "Maize";
  char soilType[20] = "Red";
  char cropStartDate[20] = "2025-10-01";
  char cropEndDate[20] = "2026-02-01"; // INCLUDED
} farmerProfile;

// Forward declarations
float calculateConicalTankLevel(float dist_cm);
void ai_water_budgeting();
void getFertilizerRecommendation();
void controlIrrigation();
void startPump(unsigned long duration);
void stopPump();
void sendAlert(String title, String message);
void fetchWeather();
void handleRoot();
void handleAPIData();
void handleAPIProfileData();
void handleAPIPumpOn();
void handleAPIPumpOff();
void handleAPIAuto();
void handleAPIProfile();
void loadProfileFromFlash();
void saveProfileToFlash();
void updateHistoricalData();
void checkAndResetDailyUsage();
void handleNewMessages(int numNewMessages);
void getChatId(int numNewMessages);


// ================================================================
// TANK GEOMETRY CALCULATION (CLEANED)
// =================================================================
float calculateConicalTankLevel(float dist_cm) {
  float h_water = SENSOR_TO_EMPTY - dist_cm;
  h_water = constrain(h_water, 0.0, TANK_HEIGHT);
  if (h_water <= 0) return 0.0;

  const float H = TANK_HEIGHT;
  
  float r_h = TANK_R_BOTTOM + (TANK_R_TOP - TANK_R_BOTTOM) * (h_water / H);
  float volume_cm3 = (PI / 3.0) * h_water * (TANK_R_BOTTOM * TANK_R_BOTTOM + TANK_R_BOTTOM * r_h + r_h * r_h);
  return volume_cm3 / 1000.0;
}

// ================================================================
// FERTILIZER RECOMMENDATION MODULE
// ================================================================
void getFertilizerRecommendation() {
    int temp = (int)temperature;
    int hum = (int)humidity;
    int soil = soilMoisturePercent; 

    if (soil < 35) {
        fertilizerRecommendation = "Urgent Water needed. Apply N (Urea) post-watering.";
        return;
    } 
    
    if (soil >= 35 && soil <= 60 && temp > 20 && temp < 35) {
        if (strcmp(farmerProfile.soilType, "Sandy") == 0) {
            fertilizerRecommendation = "Light N (Urea) dose. Sandy soil leaks.";
        } else {
            fertilizerRecommendation = "Moderate NPK (20-10-10) for growth.";
        }
        return;
    }

    if (temp >= 35 && hum < 50) {
        fertilizerRecommendation = "P/K Focus: Apply DAP (Phosphorus) for stress resistance.";
        return;
    }
    
    if (soil > 70 || hum > 80) {
        fertilizerRecommendation = "High Moisture Risk! Reduce watering. Use Compost/Fungicide.";
        return;
    }

    fertilizerRecommendation = "Optimal conditions. Maintenance NPK (15-15-15).";
}


// ================================================================
// AI CORE: WATER BUDGETING & IRRIGATION DECISION
// ================================================================
void ai_water_budgeting() {
  float baseWaterNeedLiters = 0.0;
  float evaporationFactor = 1.0;
  float soilFactor = 1.0;
  int targetMoistureMin = 50;

  // Determine base crop need and target moisture
  if (strcmp(farmerProfile.cropType, "Maize") == 0) {
    baseWaterNeedLiters = 25000.0 * farmerProfile.landSize;
    targetMoistureMin = 50;
  } else {
    baseWaterNeedLiters = 20000.0 * farmerProfile.landSize;
    targetMoistureMin = 50;
  }
  
  // Environmental Factor Adjustment 
  if (temperature > 35.0 && humidity < 40.0) {
    evaporationFactor = 1.25;
  } else if (temperature < 20.0 || humidity > 70.0) {
    evaporationFactor = 0.90;
  }

  // Soil Factor Adjustment
  if (strcmp(farmerProfile.soilType, "Red") == 0 || strcmp(farmerProfile.soilType, "Sandy") == 0) {
    soilFactor = 1.15;
  } else {
    soilFactor = 0.85;
  }

  // Final Budget Calculation (Normalized for small demo tank)
  predictedWaterNeedLiters = 0.5 * evaporationFactor * soilFactor;

  // Irrigation Duration Recommendation (Dynamic)
  float moistureDeficit = (float)targetMoistureMin - soilMoisturePercent;

  if (moistureDeficit > 5.0) {
    float requiredWaterLiters = moistureDeficit * 0.01;
    irrigationDurationS = requiredWaterLiters / PUMP_FLOW_RATE;
    irrigationDurationS = constrain(irrigationDurationS, 5.0, 60.0);
    aiRecommendation = "Run pump for " + String((int)irrigationDurationS) + " seconds.";
  } else {
    irrigationDurationS = 0.0;
    aiRecommendation = "Soil optimal, irrigation paused. Target: " + String(targetMoistureMin) + "%";
  }

  // RAIN PREDICTION FACTOR
  if (rain1h_mm > 0.5 || rainProb_percent > 75.0) {
      if (soilMoisturePercent < 45 && irrigationDurationS > 0) {
          irrigationDurationS *= 0.5; // Halve duration
          aiRecommendation = "Rain expected, reduced irrigation to " + String((int)irrigationDurationS) + "s.";
      } else {
          aiRecommendation = "Rain expected, irrigation paused.";
          irrigationDurationS = 0.0;
      }
      sendAlert("🌧 Rain Predicted", "Irrigation adjusted/paused due to weather forecast.");
  }
  
  // Calculate efficiency score
  farmEfficiencyScore = constrain(100.0 - (abs(soilMoisturePercent - targetMoistureMin) * 2.0), 0.0, 100.0);


  // Days Tank Lasts (CONFIRMED LOGIC)
  if (predictedWaterNeedLiters > 0.001) {
    daysTankLasts = (int) (tankLevelLiters / predictedWaterNeedLiters);
  } else {
    daysTankLasts = 999;
  }
}

// ================================================================
// SENSOR READING FUNCTIONS
// ================================================================
void readAllSensors() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  if (isnan(temperature)) temperature = 0.0;
  if (isnan(humidity)) humidity = 0.0;

  int soilRawValue = analogRead(SOIL_MOISTURE_PIN);
  soilMoisturePercent = map(soilRawValue, 4095, 0, 0, 100);
  soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  distance = duration * 0.0343 / 2;

  tankLevelLiters = calculateConicalTankLevel(distance);
  tankLevelPercent = (tankLevelLiters / TANK_MAX_CAPACITY) * 100.0;
  tankLevelPercent = constrain(tankLevelPercent, 0, 100);

  ai_water_budgeting();
  getFertilizerRecommendation();
}

// ================================================================
// IRRIGATION CONTROL
// ================================================================
void startPump(unsigned long duration) {
  if (tankLevelLiters < (0.10 * TANK_MAX_CAPACITY)) {
    sendAlert("⚠ Cannot Start Pump", "Tank level too low for irrigation.");
    return;
  }
  
  digitalWrite(PUMP_PIN, RELAY_ACTIVE_STATE);
  pumpState = true;
  irrigationStartTime = millis();
  if (duration > 0) {
    irrigationDurationS = (float)duration / 1000.0;
  } else {
    irrigationDurationS = MANUAL_IRRIGATION_DURATION_S;
  }
  Serial.println("Pump started");
}

void stopPump() {
  if (!pumpState) return;

  digitalWrite(PUMP_PIN, !RELAY_ACTIVE_STATE);
  pumpState = false;
  
  if (irrigationStartTime > 0) {
    unsigned long run_time_ms = millis() - irrigationStartTime;
    float water_used_liters = (float)run_time_ms / 1000.0 * PUMP_FLOW_RATE;
    totalWaterUsed += water_used_liters;
    dailyWaterUsed += water_used_liters; // This increments for the 'today used' display
    
    tankLevelLiters -= water_used_liters;
    tankLevelLiters = constrain(tankLevelLiters, 0.0, TANK_MAX_CAPACITY);
    tankLevelPercent = (tankLevelLiters / TANK_MAX_CAPACITY) * 100.0;

    Serial.println("Pump stopped. Used: " + String(water_used_liters, 3) + "L");
  }
  irrigationStartTime = 0;
}

void controlIrrigation() {
  // --- Check for Unintentional or Manual Run Override ---
  if (pumpState && tankLevelLiters < (0.05 * TANK_MAX_CAPACITY)) {
    stopPump();
    sendAlert("⚠ EMERGENCY STOP", "Tank almost empty!");
    return;
  }
  
  // Check for stop condition
  if (pumpState) {
    unsigned long currentRunTime = (millis() - irrigationStartTime);
    // 1. Time-based Stop (AI Duration or Manual Safety)
    if (currentRunTime >= (unsigned long)(irrigationDurationS * 1000.0)) {
      stopPump();
      sendAlert("⏹ Irrigation Stopped (Time)", "Duration complete.");
    }
    return;
  }

  if (!autoMode) return;

  // --- Auto-Mode Logic (Start) ---
  if (irrigationDurationS > 0.0 && !pumpState) {
    startPump((unsigned long)(irrigationDurationS * 1000.0));
    sendAlert("🌱 Irrigation Started (AI)", "Running for " + String((int)irrigationDurationS) + "s. Soil: " + String(soilMoisturePercent) + "%");
  }
}

// ================================================================
// ALERT SYSTEM
// ================================================================
void sendAlert(String title, String message) {
  if (millis() - lastAlertTime < ALERT_THROTTLE_MS) return; 
  
  String newAlert = title + ": " + message;
  lastAlert = newAlert;
  lastAlertTime = millis();
  Serial.println("ALERT: " + newAlert);
  
  // Shift old alerts down and add new alert to the front
  for (int i = alertHistoryCount; i > 0; i--) {
      if (i < 5) alertHistory[i] = alertHistory[i-1];
  }
  alertHistory[0] = newAlert;
  if (alertHistoryCount < 5) alertHistoryCount++;
  
  if (TELEGRAM_CHAT_ID.length() > 0) {
     bot.sendMessage(TELEGRAM_CHAT_ID, "🚨 " + newAlert, "");
  }
}

// ================================================================
// TELEGRAM CHAT ID RETRIEVAL FUNCTION
// ================================================================
void getChatId(int numNewMessages) {
  if (TELEGRAM_CHAT_ID.length() > 0) return;
  for (int i = 0; i < numNewMessages; i++) {
    TELEGRAM_CHAT_ID = bot.messages[i].chat_id;
    break; 
  }
}

// ================================================================
// TELEGRAM BOT HANDLER
// ================================================================
void handleNewMessages(int numNewMessages) {
  // 1. Capture ID if missing
  if (TELEGRAM_CHAT_ID.length() == 0) {
    getChatId(numNewMessages);
    if (TELEGRAM_CHAT_ID.length() > 0) {
      bot.sendMessage(TELEGRAM_CHAT_ID, "✅ System connected and ready! Send /help for commands.", "");
    }
  }

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    text.trim();
    
    if (TELEGRAM_CHAT_ID.length() > 0 && chat_id != TELEGRAM_CHAT_ID) continue;
    
    // Commands logic
    if (text == "/sensors") {
      String msg = "📊 Sensor Data\n\n";
      msg += "🌡 Temp: " + String(temperature, 1) + "°C\n";
      msg += "💨 Humidity: " + String(humidity, 1) + "%\n";
      msg += "🌱 Soil: " + String(soilMoisturePercent) + "%\n";
      msg += "💧 Tank: " + String(tankLevelPercent, 1) + "% (" + String(tankLevelLiters, 2) + "L)\n";
      msg += "🌬 Wind: " + String(windSpeed, 1) + " m/s (" + String(windDeg) + "°)\n";
      msg += "📉 Pressure: " + String(pressure, 0) + " hPa\n";
      msg += "☔ Weather: " + weatherMain + " (" + String(rain1h_mm, 1) + "mm, " + String(rainProb_percent, 0) + "% prob)\n";
      msg += "💦 Pump: " + String(pumpState ? "ON" : "OFF") + "\n";
      bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (text == "/water") {
      String msg = "💧 Water Usage & Budget\n\n";
      msg += "Today: " + String(dailyWaterUsed, 3) + " L\n";
      msg += "Total: " + String(totalWaterUsed, 3) + " L\n";
      msg += "Tank: " + String(tankLevelLiters, 3) + " L\n";
      msg += "Tank Lasts: " + String(daysTankLasts) + " days (Est.)";
      bot.sendMessage(chat_id, msg, "Markdown");
    }
    else if (text == "/pumpon") {
      if (tankLevelPercent < 10) {
        bot.sendMessage(chat_id, "⚠ Cannot start - Tank level too low!", "");
      } else {
        autoMode = false;
        startPump(0);
        bot.sendMessage(chat_id, "💧 Pump turned ON (Manual mode, 1hr limit). Use /pumpoff to stop.", "");
      }
    }
    else if (text == "/pumpoff") {
      stopPump();
      bot.sendMessage(chat_id, "⏹ Pump turned OFF", "");
    }
    else if (text == "/auto") { autoMode = true; bot.sendMessage(chat_id, "🤖 Auto mode ENABLED.", ""); }
    else if (text == "/manual") { autoMode = false; bot.sendMessage(chat_id, "👤 Manual mode ENABLED.", ""); }
    else if (text == "/ai") { 
        String msg = "🧠 AI Decision\n";
        msg += "Reco: " + aiRecommendation + "\n";
        msg += "Duration: " + String((int)irrigationDurationS) + "s\n";
        msg += "Fert Reco: " + fertilizerRecommendation;
        bot.sendMessage(chat_id, msg, "Markdown"); 
    }
    else if (text == "/fertilizer") { bot.sendMessage(chat_id, "🧪 " + fertilizerRecommendation, ""); }
    else if (text == "/alerts") { 
        String msg = "🔔 Recent Alerts (Max 5):\n";
        for(int j=0; j<alertHistoryCount; j++) {
            msg += "- " + alertHistory[j] + "\n";
        }
        bot.sendMessage(chat_id, msg, "Markdown"); 
    }
    else if (text == "/help" || text == "/start") {
         String welcome = "🌱 Smart Irrigation System\n\n";
          welcome += "🎛 Control:\n/pumpon | /pumpoff | /auto | /manual\n";
          welcome += "📊 Data:\n/sensors | /water | /ai | /fertilizer\n";
          welcome += "🚨 Alerts:\n/alerts\n";
          bot.sendMessage(chat_id, welcome, "Markdown");
    }
    else { bot.sendMessage(chat_id, "Unknown command. Send /help", ""); }
  }
}

// ================================================================
// WEATHER FETCH
// ================================================================
void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + WEATHER_CITY + "&appid=" + WEATHER_API_KEY + "&units=metric";

  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      // Main weather data
      float temp_c = doc["main"]["temp"] | 0.0;
      weatherMain = doc["weather"][0]["main"].as<String>();
      temperature = temp_c; // Overwrite DHT reading for environmental temp in AI
      humidity = doc["main"]["humidity"] | 0.0; // Overwrite DHT reading for environmental humidity in AI
      pressure = doc["main"]["pressure"] | 0.0; 
      
      // Extended data
      windSpeed = doc["wind"]["speed"] | 0.0; 
      windDeg = doc["wind"]["deg"] | 0; 
      
      rain1h_mm = doc["rain"]["1h"] | 0.0;
      rainProb_percent = doc["clouds"]["all"] | 0;
      Serial.println("Weather: " + weatherMain + " Rain1h:" + String(rain1h_mm) + " Prob:" + String(rainProb_percent));
    } else {
      Serial.println("Weather JSON parse error.");
    }
  } else {
    Serial.println("Weather fetch failed, code: " + String(httpCode));
  }
  http.end();
}


// ================================================================
// WEB SERVER - HTML DASHBOARD
// ================================================================
void handleRoot() {
  String html = R"rawliteral(<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Smart Irrigation Dashboard</title><style>*{margin:0;padding:0;box-sizing:border-box}body{font-family:'Segoe UI',system-ui,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh}
.topbar{background:#fff;box-shadow:0 2px 10px rgba(0,0,0,.1);padding:15px 30px;display:flex;justify-content:space-between;align-items:center}
.status{display:flex;align-items:center;gap:8px}.status-dot{width:12px;height:12px;border-radius:50%;animation:pulse 2s infinite}
.status-active{background:#28a745}.status-inactive{background:#dc3545}@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}
.lang-select{padding:8px 15px;border:2px solid #667eea;border-radius:20px;background:white;cursor:pointer;font-weight:600;color:#667eea}
.nav{background:#fff;margin:20px 30px;border-radius:15px;display:flex;gap:10px;padding:10px;box-shadow:0 2px 15px rgba(0,0,0,.1)}
.nav-btn{flex:1;padding:12px;border:none;background:#f8f9fa;border-radius:10px;cursor:pointer;font-weight:600;transition:all .3s;color:#333}
.nav-btn.active{background:linear-gradient(135deg,#667eea,#764ba2);color:white;transform:translateY(-2px);box-shadow:0 4px 15px rgba(102,126,234,.4)}
.nav-btn:hover{background:#667eea;color:white}
.container{max-width:1400px;margin:0 auto;padding:0 30px 30px}
.section{display:none;background:white;border-radius:15px;padding:30px;box-shadow:0 5px 25px rgba(0,0,0,.1);animation:fadeIn .3s}
.section.active{display:block}@keyframes fadeIn{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:translateY(0)}}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-bottom:30px}
.card{background:linear-gradient(135deg,#667eea15,#764ba215);border-radius:15px;padding:25px;box-shadow:0 3px 15px rgba(0,0,0,.08);transition:transform .3s}
.card:hover{transform:translateY(-5px)}
.card-title{font-size:14px;color:#666;margin-bottom:10px;display:flex;align-items:center;gap:8px}
.card-value{font-size:36px;font-weight:bold;color:#333;margin:10px 0}
.card-unit{font-size:18px;color:#999;font-weight:normal}
.chart-container{background:white;border-radius:15px;padding:25px;margin-top:20px;box-shadow:0 3px 15px rgba(0,0,0,.08)}
.chart{width:100%;height:250px;position:relative;display:flex;justify-content:space-between;align-items:flex-end;padding-top:20px}
.chart-bar{flex:1;margin:0 0.5%;border-radius:4px 4px 0 0;position:relative;transition:all .3s; display: flex; align-items: flex-end; justify-content: space-around; gap: 1px;}
.bar-segment{width: 30%; border-radius: 2px 2px 0 0;}
.chart-bar:hover{opacity:.8;transform:scaleY(1.05)}
.toggle-switch{position:relative;width:60px;height:30px;background:#ddd;border-radius:30px;cursor:pointer;transition:background .3s}
.toggle-switch.on{background:#28a745}
.toggle-slider{position:absolute;width:26px;height:26px;background:white;border-radius:50%;top:2px;left:2px;transition:transform .3s;box-shadow:0 2px 5px rgba(0,0,0,.2)}
.toggle-switch.on .toggle-slider{transform:translateX(30px)}
.control-panel{display:flex;gap:20px;align-items:center;flex-wrap:wrap;background:#f8f9fa;padding:20px;border-radius:15px}
.btn{padding:12px 30px;border:none;border-radius:25px;font-weight:600;cursor:pointer;transition:all .3s;box-shadow:0 3px 10px rgba(0,0,0,.1)}
.btn-primary{background:linear-gradient(135deg,#667eea,#764ba2);color:white}
.btn-primary:hover{transform:translateY(-2px);box-shadow:0 5px 20px rgba(102,126,234,.4)}
.btn-danger{background:#dc3545;color:white}
.btn-danger:hover{background:#c82333;transform:translateY(-2px)}
.alert-item{background:#fff3cd;border-left:4px solid #ffc107;padding:15px;margin:10px 0;border-radius:8px;font-size:14px;}
.crop-card{background:white;border:2px solid #e9ecef;border-radius:15px;padding:20px;margin:10px 0;transition:all .3s}
.crop-card:hover{border-color:#667eea;box-shadow:0 5px 20px rgba(102,126,234,.2)}
.profile-field{margin:15px 0;padding:15px;background:#f8f9fa;border-radius:10px}
.profile-field label{display:block;font-weight:600;color:#666;margin-bottom:8px}
.profile-field input,.profile-field select{width:100%;padding:10px;border:2px solid #e9ecef;border-radius:8px;font-size:16px}
.profile-field input:focus,.profile-field select:focus{outline:none;border-color:#667eea}
.search-box{width:100%;padding:15px;border:2px solid #667eea;border-radius:25px;font-size:16px;margin-bottom:20px}
.search-box:focus{outline:none;box-shadow:0 0 15px rgba(102,126,234,.3)}
h2{color:#333;margin-bottom:20px;font-size:28px}
.status-badge{display:inline-block;padding:5px 15px;border-radius:20px;font-size:14px;font-weight:600}
.badge-success{background:#d4edda;color:#155724}
.badge-warning{background:#fff3cd;color:#856404}
.badge-danger{background:#f8d7da;color:#721c24}
</style>
<style>
/* New bar chart styles */
.bar-soil { background-color: #ffaa00; } /* Orange */
.bar-temp { background-color: #ffdd00; } /* Yellow */
.bar-hum { background-color: #00bfff; } /* Skyblue */
.chart-legend { display: flex; justify-content: center; gap: 20px; margin-top: 10px; font-size: 14px; }
.chart-legend span { display: flex; align-items: center; }
.chart-legend div { width: 10px; height: 10px; margin-right: 5px; border-radius: 50%; }
</style>
</head><body>
<div class='topbar'><div class='status'><div class='status-dot status-active' id='statusDot'></div>
<span id='statusText'>ESP32 Active</span></div>
<select class='lang-select' id='langSelect' onchange='changeLang()'><option value='en'>English</option><option value='kn'>ಕನ್ನಡ</option><option value='hi'>हिन्दी</option></select></div>
<div class='nav'><button class='nav-btn active' onclick='showSection("home")'>🏠 <span data-en='Home' data-kn='ಮುಖಪುಟ' data-hi='होम'>Home</span></button>
<button class='nav-btn' onclick='showSection("control")'>🎛 <span data-en='Control' data-kn='ನಿಯಂತ್ರಣ' data-hi='नियಂತ್ರಣ'>Control</span></button>
<button class='nav-btn' onclick='showSection("crops")'>🌾 <span data-en='Crop Advisory' data-kn='ಬೆಳೆ ಸಲಹೆ' data-hi='फसल सलाह'>Crop Advisory</span></button>
<button class='nav-btn' onclick='showSection("alerts")'>🔔 <span data-en='Alerts' data-kn='ಎಚ್ಚರಿಕೆಗಳು' data-hi='अलर्ट'>Alerts</span></button>
<button class='nav-btn' onclick='showSection("profile")'>👤 <span data-en='Profile' data-kn='ಪ್ರೊಫೈಲ್' data-hi='प्रोफ़ाइल'>Profile</span></button></div>
<div class='container'><div id='home' class='section active'><h2 data-en='Sensor Dashboard' data-kn='ಸಂವೇದಕ ಡ್ಯಾಶ್‌ಬೋರ್ಡ್' data-hi='सेंसर डैशबोर्ड'>Sensor Dashboard</h2>
<div class='grid'>
<div class='card'><div class='card-title'>🌡 <span data-en='Temperature' data-kn='ತಾಪಮಾನ' data-hi='तापमान'>Temperature</span></div><div class='card-value' id='temp'>--<span class='card-unit'>°C</span></div></div>
<div class='card'><div class='card-title'>💨 <span data-en='Humidity' data-kn='ಆರ್ದ್ರತೆ' data-hi='ಆರ್ದ್ರತಾ'>Humidity</span></div><div class='card-value' id='humidity'>--<span class='card-unit'>%</span></div></div>
<div class='card'><div class='card-title'>🌱 <span data-en='Soil Moisture' data-kn='ಮಣ್ಣಿನ ತೇವಾಂಶ' data-hi='मिट्टी की नमी'>Soil Moisture</span></div><div class='card-value' id='soil'>--<span class='card-unit'>%</span></div></div>
<div class='card'><div class='card-title'>💧 <span data-en='Tank Level' data-kn='ತೊಟ್ಟಿ ಮಟ್ಟ' data-hi='टैंक स्तर'>Tank Level</span></div>
    <div class='card-value' id='tank_percent'>--<span class='card-unit'>%</span></div>
    <div class='card-unit' style='font-size:16px;'>(<span id='tank_liters'>--</span> L)</div>
</div>
<div class='card'><div class='card-title'>💦 <span data-en='Water Used Today' data-kn='ಇಂದು ಬಳಸಿದ ನೀರು' data-hi='आज उपयोग किया गया पानी'>Water Used Today</span></div><div class='card-value' id='waterUsed'>--<span class='card-unit'>L</span></div></div>
<div class='card'><div class='card-title'>🗓 <span data-en='Tank Lasts' data-kn='ಟ್ಯಾಂಕ್ ಎಷ್ಟು ದಿನ ಇರುತ್ತದೆ' data-hi='टैंक कितने दिन चलेगा'>Tank Lasts</span></div><div class='card-value' id='daysLasts'>--<span class='card-unit'>days</span></div></div>

<div class='card' style='grid-column: span 3; background: #e6f7ff; color: #005f7c;'>
    <div class='card-title'>🌤 <span data-en='Current Weather Status' data-kn='ಪ್ರಸ್ತುತ ಹವಾಮಾನ ಸ್ಥಿತಿ' data-hi='वर्तमान मौसम स्थिति'>Current Weather Status</span></div>
    <div class='card-value' style='font-size: 20px; font-weight: 600;' id='weather_main'>--</div>
    <div class='card-unit' style='font-size: 14px;'>
        Temp: <span id='weather_temp'>--</span>°C | Pressure: <span id='pressure'>--</span> hPa<br>
        Wind: <span id='wind_speed'>--</span> m/s (<span id='wind_deg'>--</span>°)
    </div>
</div>
<div class='card' style='grid-column: span 3; background: #eafff0; color: #28a745;'>
    <div class='card-title'>☔ <span data-en='Rain Prediction & Fertilizer' data-kn='ಮಳೆ ಮುನ್ಸೂಚನೆ & ಗೊಬ್ಬರ' data-hi='वर्षा पूर्वानुमान और उर्वरक'>Rain Prediction & Fertilizer</span></div>
    <div class='card-value' style='font-size: 18px; font-weight: 600;' id='fertilizer_reco'>--</div>
    <div class='card-unit' style='font-size: 14px;'>
        Rain: <span id='rain_1h'>--</span>mm (1h) | Prob: <span id='rain_prob'>--</span>%
    </div>
</div>

</div>
<div class='chart-container'>
    <h3 data-en='24-Hour Sensor History' data-kn='24-ಗಂಟೆಗಳ ಸಂವೇದಕ ಇತಿಹಾಸ' data-hi='24-घंटे सेंसर इतिहास'>24-Hour Sensor History</h3>
    <div class='chart-legend'>
        <span><div class='bar-soil'></div> Soil Moisture (%)</span>
        <span><div class='bar-temp'></div> Temperature (°C)</span>
        <span><div class='bar-hum'></div> Humidity (%)</span>
    </div>
    <div class='chart' id='soilChart'></div>
    <div style='text-align:center;color:#666;font-size:12px;margin-top:10px'>Each group of bars represents 1 hour.</div>
</div></div>
<div id='control' class='section'><h2 data-en='Irrigation Control' data-kn='ನೀರಾವರಿ ನಿಯಂತ್ರಣ' data-hi='सिंचाई नियंत्रण'>Irrigation Control</h2>
<div class='control-panel'><div><strong data-en='Pump Status:' data-kn='ಪಂಪ್ ಸ್ಥಿತಿ:' data-hi='पंप स्थिति:'>Pump Status:</strong> <span id='pumpStatus' class='status-badge badge-danger' data-en='OFF' data-kn='ಆಫ್' data-hi='बंद'>OFF</span></div>
<button class='btn btn-primary' onclick='startManualPump()' data-en='Pump ON' data-kn='ಪಂಪ್ ಆನ್' data-hi='पंप चालू करें'>Pump ON</button>
<button class='btn btn-danger' onclick='stopManualPump()' data-en='Pump OFF' data-kn='ಪಂಪ್ ಆಫ್' data-hi='पंप बंद करें'>Pump OFF</button>
<div><strong data-en='Auto Mode:' data-kn='ಸ್ವಯಂ ಮೋಡ್:' data-hi='ऑटो मोड:'>Auto Mode:</strong> <div class='toggle-switch' id='autoToggle' onclick='toggleAuto()'><div class='toggle-slider'></div></div></div></div>
<div class='grid' style='margin-top:30px'><div class='card'><div class='card-title'>⏱ <span data-en='AI Irrigation Duration' data-kn='AI ನೀರಾವರಿ ಅವಧಿ' data-hi='एआई सिंचाई अवधि'>AI Irrigation Duration</span></div><div class='card-value' id='aiDuration'>--<span class='card-unit'>sec</span></div></div>
<div class='card'><div class='card-title'>🧠 <span data-en='AI Recommendation' data-kn='AI ಶಿಫಾರಸು' data-hi='एआई सिफारिश'>AI Recommendation</span></div><div class='card-value' style='font-size:20px' id='aiReco'>--</div></div></div></div>
<div id='crops' class='section'><h2 data-en='Crop Advisory - Karnataka' data-kn='ಬೆಳೆ ಸಲಹೆ - ಕರ್ನಾಟಕ' data-hi='फसल सलाह - कर्नाटक'>Crop Advisory - Karnataka</h2>
<input type='text' class='search-box' id='cropSearch' placeholder='Search crops...' oninput='filterCrops()'>
<div id='cropList'><div class='crop-card'><h3>🌽 Maize (Mage)</h3><p><strong>Soil:</strong> Any | <strong>Water:</strong> 500-800mm | <strong>Need:</strong> Medium-High</p></div>
<div class='crop-card'><h3>🌿 Sugarcane</h3><p><strong>Soil:</strong> Black/Clay | <strong>Water:</strong> 1200-1500mm | <strong>Need:</strong> Very High</p></div>
<div class='crop-card'><h3>🌾 Jowar (Sorghum)</h3><p><strong>Soil:</strong> Red/Black | <strong>Water:</strong> 350-500mm | <strong>Need:</strong> Low</p></div>
<div class='crop-card'><h3>🚬 Tobacco</h3><p><strong>Soil:</strong> Sandy | <strong>Water:</strong> 400-600mm | <strong>Need:</strong> Low-Moderate</p></div>
<div class='crop-card'><h3>☁ Cotton</h3><p><strong>Soil:</strong> Black | <strong>Water:</strong> 700-1000mm | <strong>Need:</strong> High</p></div>
</div></div>
<div id='alerts' class='section'><h2 data-en='System Alerts' data-kn='ಸಿಸ್ಟಮ್ ಎಚ್ಚರಿಕೆಗಳು' data-hi='सिस्टम अलर्ट'>System Alerts</h2>
<div id='alertsList'>
    <div class='alert-item'>⚠ <span id='currentAlert'>Current Alert: </span></div>
    <div id='alertHistoryList'></div>
</div></div>
<div id='profile' class='section'><h2 data-en='Farmer Profile' data-kn='ರೈತ ಪ್ರೊಫೈಲ್' data-hi='किसान प्रोफ़ाइल'>Farmer Profile</h2>
<div class='profile-field'><label data-en='Name' data-kn='ಹೆಸರು' data-hi='नाम'>Name</label><input type='text' id='farmerName' value='Farmer'></div>
<div class='profile-field'><label data-en='Phone Number' data-kn='ದೂರವಾಣಿ ಸಂಖ್ಯೆ' data-hi='फ़ोन नंबर'>Phone Number</label><input type='text' id='farmerPhone' value='+91-XXXXXXXXXX'></div>
<div class='profile-field'><label data-en='Location' data-kn='ಸ್ಥಳ' data-hi='स्थान'>Location</label><input type='text' id='farmerLocation' value='Karnataka, India'></div>
<div class='profile-field'><label data-en='Land Size (Acres)' data-kn='ಜಮೀನು ಗಾತ್ರ (ಎಕರೆ)' data-hi='भूमि आकार (एकड़)'>Land Size (Acres)</label><input type='number' id='landSize' value='1.0' step='0.1'></div>
<div class='profile-field'><label data-en='Crop Type' data-kn='ಬೆಳೆ ಪ್ರಕಾರ' data-hi='फसल प्रकार'>Crop Type</label>
<select id='cropType'><option>Maize</option><option>Sugarcane</option><option>Jowar</option><option>Tobacco</option><option>Cotton</option><option>Other</option></select></div>
<div class='profile-field'><label data-en='Soil Type' data-kn='ಮಣ್ಣಿನ ಪ್ರಕಾರ' data-hi='मिट्टी का प्रकार'>Soil Type</label>
<select id='soilType'><option>Red</option><option>Black</option><option>Sandy</option><option>Clay</option></select></div>
<div class='profile-field'><label data-en='Crop Start Date' data-kn='ಬೆಳೆ ಪ್ರಾರಂಭ ದಿನಾಂಕ' data-hi='फसल शुरुआत तिथि'>Crop Start Date</label><input type='date' id='cropStartDate' value='2025-10-01'></div>
<div class='profile-field'><label data-en='Crop End Date' data-kn='ಬೆಳೆ ಮುಕ್ತಾಯ ದಿನಾಂಕ' data-hi='फसल समाप्ति तिथि'>Crop End Date</label><input type='date' id='cropEndDate' value='2026-02-01'></div>
<button class='btn btn-primary' onclick='saveProfile()' style='margin-top:20px' data-en='Save Profile' data-kn='ಪ್ರೊಫೈಲ್ ಉಳಿಸಿ' data-hi='प्रोफ़ाइल सहेजें'>Save Profile</button></div></div>
<script>
let currentLang='en';
function showSection(sec){document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
document.getElementById(sec).classList.add('active');
document.querySelectorAll('.nav-btn').forEach((b,i)=>{b.classList.remove('active');if((sec=='home'&&i==0)||(sec=='control'&&i==1)||(sec=='crops'&&i==2)||(sec=='alerts'&&i==3)||(sec=='profile'&&i==4))b.classList.add('active')})}
function changeLang(){
    currentLang=document.getElementById('langSelect').value;
    document.querySelectorAll('[data-en]').forEach(el=>{
        const translation = el.getAttribute('data-'+currentLang) || el.getAttribute('data-en');
        el.textContent=translation;
    });
    updateData(); 
}

function loadProfileData(){
    fetch('/api/profile_data').then(r=>r.json()).then(data=>{
        document.getElementById('farmerName').value = data.name;
        document.getElementById('farmerPhone').value = data.phone;
        document.getElementById('farmerLocation').value = data.location;
        document.getElementById('landSize').value = data.landSize;
        document.getElementById('cropType').value = data.cropType;
        document.getElementById('soilType').value = data.soilType;
        document.getElementById('cropStartDate').value = data.cropStartDate; 
        document.getElementById('cropEndDate').value = data.cropEndDate; 
    }).catch(e=>console.error("Failed to load profile data:", e));
}

function updateData(){fetch('/api/data').then(r=>r.json()).then(data=>{
document.getElementById('temp').innerHTML=data.temperature.toFixed(1)+'<span class="card-unit">°C</span>';
document.getElementById('humidity').innerHTML=data.humidity.toFixed(1)+'<span class="card-unit">%</span>';
document.getElementById('soil').innerHTML=data.soilMoisture+'<span class="card-unit">%</span>';

// TANK LEVEL FIX: Display both % and Liters
document.getElementById('tank_percent').innerHTML=data.tankLevelPercent.toFixed(1);
document.getElementById('tank_liters').textContent=data.tankLevelLiters.toFixed(2);


document.getElementById('waterUsed').innerHTML=data.dailyWater.toFixed(2)+'<span class="card-unit">L</span>';
document.getElementById('daysLasts').innerHTML=data.daysLasts+'<span class="card-unit">days</span>';

// Full Weather Update
document.getElementById('weather_main').textContent=data.weatherMain;
document.getElementById('weather_temp').textContent=data.temperature.toFixed(1);
document.getElementById('pressure').textContent=data.pressure.toFixed(0);
document.getElementById('wind_speed').textContent=data.windSpeed.toFixed(1);
document.getElementById('wind_deg').textContent=data.windDeg;
document.getElementById('rain_1h').textContent=data.rain1h.toFixed(1);
document.getElementById('rain_prob').textContent=data.rainProb.toFixed(0);

// Fertilizer Update
document.getElementById('fertilizer_reco').textContent=data.fertilizerRecommendation;

// PUMP STATE UPDATE
document.getElementById('pumpStatus').textContent=data.pumpState?'ON':'OFF';
document.getElementById('pumpStatus').className='status-badge '+(data.pumpState?'badge-success':'badge-danger');
document.getElementById('autoToggle').className='toggle-switch '+(data.autoMode?'on':'');
document.getElementById('aiDuration').innerHTML=data.irrigationDurationS.toFixed(0)+'<span class="card-unit">sec</span>';
document.getElementById('aiReco').textContent=data.aiRecommendation;

// Alert display: Show only the newest alert in the primary box
document.getElementById('currentAlert').textContent = data.lastAlert.length > 0 ? data.lastAlert : 'No recent alerts';

// Alert History Display (Fetching history array from API)
let alertList = document.getElementById('alertHistoryList');
alertList.innerHTML = ''; // Clear existing list
data.alertHistory.forEach((alert, index) => {
    // Skip the newest alert since it's already in the main box
    if (index > 0 && alert.length > 0) { 
        let div = document.createElement('div');
        div.className = 'alert-item';
        div.textContent = alert;
        alertList.appendChild(div);
    }
});


// Update 24-hour history graph (Triple Bars)
updateChart(data.tripleHistory);

}).catch(e=>console.error(e))}

// CHART FIX: Updated to handle 3 data points per hour
function updateChart(history){
    let chart=document.getElementById('soilChart'); 
    chart.innerHTML='';
    const maxBarHeight = 250; 
    
    // Process 24 hours (24 * 3 = 72 data points)
    for(let i=0;i<24*3;i+=3){
        const soil = history[i] || 0;
        const temp = history[i+1] || 0;
        const hum = history[i+2] || 0;

        // SCALING FACTORS: 
        // Soil: 0-100% -> 0-250px (Scale * 2.5)
        // Temp: Assuming max temp of 50C -> 0-250px (Scale * 5.0)
        // Hum: 0-100% -> 0-250px (Scale * 2.5)
        const soilHeight = Math.min(maxBarHeight, Math.max(2, soil * 2.5));
        const tempHeight = Math.min(maxBarHeight, Math.max(2, temp * 5.0));
        const humHeight = Math.min(maxBarHeight, Math.max(2, hum * 2.5));
        
        let hourBar = document.createElement('div');
        hourBar.className='chart-bar';
        hourBar.title='Hour: ' + (i/3) + ' | Soil: '+soil+'% | Temp: '+temp+'°C | Hum: '+hum+'%';
        
        // Soil Moisture (Orange)
        let soilSeg = document.createElement('div');
        soilSeg.className = 'bar-segment bar-soil';
        soilSeg.style.height = soilHeight + 'px';

        // Temperature (Yellow)
        let tempSeg = document.createElement('div');
        tempSeg.className = 'bar-segment bar-temp';
        tempSeg.style.height = tempHeight + 'px';

        // Humidity (Blue)
        let humSeg = document.createElement('div');
        humSeg.className = 'bar-segment bar-hum';
        humSeg.style.height = humHeight + 'px';

        hourBar.appendChild(soilSeg);
        hourBar.appendChild(tempSeg);
        hourBar.appendChild(humSeg);
        
        chart.appendChild(hourBar);
    }
}

function startManualPump(){fetch('/api/pump/on',{method:'POST'}).then(r=>r.text()).then(msg=>{alert(msg);updateData()})}
function stopManualPump(){fetch('/api/pump/off',{method:'POST'}).then(r=>r.text()).then(msg=>{alert(msg);updateData()})}

function toggleAuto(){fetch('/api/auto',{method:'POST'}).then(r=>r.text()).then(msg=>{alert(msg);updateData()})}

function saveProfile(){let profile={name:document.getElementById('farmerName').value,
phone:document.getElementById('farmerPhone').value,location:document.getElementById('farmerLocation').value,
landSize:parseFloat(document.getElementById('landSize').value),cropType:document.getElementById('cropType').value,
soilType:document.getElementById('soilType').value,cropStartDate:document.getElementById('cropStartDate').value,
cropEndDate:document.getElementById('cropEndDate').value}; 
fetch('/api/profile',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(profile)})
.then(r=>r.text()).then(msg=>{alert(msg); updateData();})}
function filterCrops(){let search=document.getElementById('cropSearch').value.toLowerCase();
document.querySelectorAll('.crop-card').forEach(card=>{
card.style.display=card.textContent.toLowerCase().includes(search)?'block':'none'})}
setInterval(updateData,2000);updateData();changeLang(); loadProfileData();
</script></body></html>
)rawliteral";
  server.send(200, "text/html", html);
}

// ================================================================
// WEB API ENDPOINTS
// ================================================================
void handleAPIData() {
  StaticJsonDocument<2048> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["soilMoisture"] = soilMoisturePercent;
  doc["tankLevelPercent"] = tankLevelPercent;
  doc["tankLevelLiters"] = tankLevelLiters; 
  doc["dailyWater"] = dailyWaterUsed;
  doc["totalWater"] = totalWaterUsed;
  doc["pumpState"] = pumpState;
  doc["autoMode"] = autoMode;
  doc["irrigationDurationS"] = irrigationDurationS;
  doc["aiRecommendation"] = aiRecommendation;
  doc["fertilizerRecommendation"] = fertilizerRecommendation;
  doc["daysLasts"] = daysTankLasts;
  doc["lastAlert"] = lastAlert;
  doc["farmEfficiencyScore"] = farmEfficiencyScore;
  
  // Full Weather Data
  doc["weatherMain"] = weatherMain;
  doc["rain1h"] = rain1h_mm;
  doc["rainProb"] = rainProb_percent;
  doc["windSpeed"] = windSpeed; 
  doc["windDeg"] = windDeg; 
  doc["pressure"] = pressure; 
  
  // ALERT HISTORY FIX: Send the history array
  JsonArray alertArr = doc.createNestedArray("alertHistory");
  for(int i = 0; i < alertHistoryCount; i++) {
      alertArr.add(alertHistory[i]);
  }
  
  // TRIPLE BAR CHART FIX: Send the linear array containing 3 data points per hour
  JsonArray history = doc.createNestedArray("tripleHistory");
  // Ensure the history is sent in the correct order for the JS visualization
  for (int h = 0; h < 24; h++) {
    int i = (historyIndex + h) % 24;
    int baseIndex = i * 3;
    history.add(tripleHistory[baseIndex]);   // Soil
    history.add(tripleHistory[baseIndex + 1]); // Temp
    history.add(tripleHistory[baseIndex + 2]); // Hum
  }
  
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleAPIProfileData() {
    StaticJsonDocument<512> doc;
    doc["name"] = farmerProfile.name;
    doc["phone"] = farmerProfile.phone;
    doc["location"] = farmerProfile.location;
    doc["landSize"] = farmerProfile.landSize;
    doc["cropType"] = farmerProfile.cropType;
    doc["soilType"] = farmerProfile.soilType;
    doc["cropStartDate"] = farmerProfile.cropStartDate;
    doc["cropEndDate"] = farmerProfile.cropEndDate; 
    
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleAPIPumpOn() {
    if (tankLevelPercent < 10) {
      server.send(400, "text/plain", "Cannot start - Tank level too low!");
      return;
    }
    autoMode = false;
    startPump(0);
    server.send(200, "text/plain", "Pump turned ON (Manual mode)");
}

void handleAPIPumpOff() {
    stopPump();
    server.send(200, "text/plain", "Pump turned OFF");
}

void handleAPIAuto() {
  autoMode = !autoMode;
  if (!autoMode && pumpState) {
    stopPump();
  }
  server.send(200, "text/plain", autoMode ? "Auto mode ENABLED" : "Manual mode ENABLED");
}

void handleAPIProfile() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
    
    // Update farmer profile
    strlcpy(farmerProfile.name, doc["name"] | "Farmer", sizeof(farmerProfile.name));
    strlcpy(farmerProfile.phone, doc["phone"] | "+91-XXXXXXXXXX", sizeof(farmerProfile.phone));
    strlcpy(farmerProfile.location, doc["location"] | "Karnataka, India", sizeof(farmerProfile.location));
    farmerProfile.landSize = doc["landSize"] | 1.0;
    strlcpy(farmerProfile.cropType, doc["cropType"] | "Maize", sizeof(farmerProfile.cropType));
    strlcpy(farmerProfile.soilType, doc["soilType"] | "Red", sizeof(farmerProfile.soilType));
    strlcpy(farmerProfile.cropStartDate, doc["cropStartDate"] | "2025-10-01", sizeof(farmerProfile.cropStartDate));
    strlcpy(farmerProfile.cropEndDate, doc["cropEndDate"] | "2026-02-01", sizeof(farmerProfile.cropEndDate)); 
    
    // Save to flash memory
    saveProfileToFlash();
    server.send(200, "text/plain", "Profile saved successfully!");
  } else {
    server.send(405, "text/plain", "Method not allowed");
  }
}

// ================================================================
// FLASH MEMORY FUNCTIONS
// ================================================================
void loadProfileFromFlash() {
  preferences.begin("irrigation", false);
  preferences.getString("name", farmerProfile.name, sizeof(farmerProfile.name));
  preferences.getString("phone", farmerProfile.phone, sizeof(farmerProfile.phone));
  preferences.getString("location", farmerProfile.location, sizeof(farmerProfile.location));
  farmerProfile.landSize = preferences.getFloat("landSize", 1.0);
  preferences.getString("cropType", farmerProfile.cropType, sizeof(farmerProfile.cropType));
  preferences.getString("soilType", farmerProfile.soilType, sizeof(farmerProfile.soilType));
  preferences.getString("cropStart", farmerProfile.cropStartDate, sizeof(farmerProfile.cropStartDate));
  preferences.getString("cropEnd", farmerProfile.cropEndDate, sizeof(farmerProfile.cropEndDate)); 
  preferences.end();
}

void saveProfileToFlash() {
  preferences.begin("irrigation", false);
  preferences.putString("name", farmerProfile.name);
  preferences.putString("phone", farmerProfile.phone);
  preferences.putString("location", farmerProfile.location);
  preferences.putFloat("landSize", farmerProfile.landSize);
  preferences.putString("cropType", farmerProfile.cropType);
  preferences.putString("soilType", farmerProfile.soilType);
  preferences.putString("cropStart", farmerProfile.cropStartDate);
  preferences.putString("cropEnd", farmerProfile.cropEndDate); 
  preferences.end();
}

// ================================================================
// HISTORICAL DATA UPDATE & DAILY RESET
// ================================================================

// Checks time and resets daily used water if the day has changed
void checkAndResetDailyUsage() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Get current time
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    // If lastDay is uninitialized or the day has changed
    if (lastDay == -1 || tm->tm_mday != lastDay) {
        if (lastDay != -1) { // Only reset if it's not the very first run
            dailyWaterUsed = 0.0;
            Serial.println("Daily water usage reset at midnight.");
        }
        lastDay = tm->tm_mday;
    }
}

void updateHistoricalData() {
  if (millis() - lastHistoryUpdate >= HISTORY_UPDATE_INTERVAL) {
    historyIndex = (historyIndex + 1) % 24; 
    int baseIndex = historyIndex * 3; 

    // Store data: Soil (0), Temp (1), Hum (2)
    tripleHistory[baseIndex] = soilMoisturePercent;
    tripleHistory[baseIndex + 1] = temperature;
    tripleHistory[baseIndex + 2] = humidity;
    
    lastHistoryUpdate = millis();
    Serial.println("History updated. Index: " + String(historyIndex));
  }
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Smart Irrigation System Starting ===");
  
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(PUMP_PIN, !RELAY_ACTIVE_STATE);
  
  dht.begin();
  loadProfileFromFlash();
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  secured_client.setInsecure();
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  // Set up NTP for accurate time keeping (needed for daily reset)
  configTime(5 * 3600 + 30 * 60, 0, "pool.ntp.org"); // IST (+5:30)

  // Initialize web server routes
  server.on("/", handleRoot);
  server.on("/api/data", handleAPIData);
  server.on("/api/profile_data", handleAPIProfileData);
  server.on("/api/pump/on", HTTP_POST, handleAPIPumpOn);
  server.on("/api/pump/off", HTTP_POST, handleAPIPumpOff);
  server.on("/api/auto", HTTP_POST, handleAPIAuto);
  server.on("/api/profile", HTTP_POST, handleAPIProfile);
  server.begin();
  
  // Initial reads
  readAllSensors();
  fetchWeather();
 
  // Check for pending Telegram messages to grab the Chat ID
  int initialMessages = bot.getUpdates(0);
  if (initialMessages > 0) {
    getChatId(initialMessages);
    if (TELEGRAM_CHAT_ID.length() > 0) {
      bot.sendMessage(TELEGRAM_CHAT_ID, "✅ System rebooted and ready! Alerts are now enabled.", "");
    }
  }
  Serial.println("=== System Ready ===\n");
}

// ================================================================
// MAIN LOOP
// ================================================================
void loop() {
  unsigned long now = millis();
  
  // 1. WEB SERVER
  server.handleClient();
  
  // 2. READ SENSORS
  if (now - sensor_lasttime >= SENSOR_READ_INTERVAL) {
    readAllSensors();
    sensor_lasttime = now;
  }
  
  // 3. WEATHER FETCH
  if (now - lastWeatherFetch >= WEATHER_UPDATE_INTERVAL) {
    fetchWeather();
    lastWeatherFetch = now;
  }
  
  // 4. IRRIGATION CONTROL & HISTORY
  if (now - irrigation_lasttime >= IRRIGATION_CHECK_INTERVAL) {
    controlIrrigation();
    updateHistoricalData();
    irrigation_lasttime = now;
  }
  
  // 5. DAILY USAGE RESET
  if (now - lastDailyResetCheck >= DAILY_RESET_CHECK_INTERVAL) {
      checkAndResetDailyUsage();
      lastDailyResetCheck = now;
  }
  
  // 6. TELEGRAM BOT
  if (now - bot_lasttime >= BOT_MTBS) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages > 0) {
      handleNewMessages(numNewMessages);
    }
    bot_lasttime = now;
  }
}