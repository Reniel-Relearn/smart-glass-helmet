#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <NimBLEDevice.h>

// ============================================================
// PROJECT: Helmet-Visor Smart Glass
// Tunnel Worker Distance and Temperature Awareness
// Board: ESP32-S3
// Sensors: DHT11 + HC-SR04
// Display: SSD1306 OLED
// Wireless: BLE + Wi-Fi + Firebase RTDB
// Device Name: Tunnel Worker 1
// Device ID: tunnel-worker-1
// ============================================================

// ============================================================
// WIFI CONFIG - ADD AS MANY WIFI NETWORKS AS NEEDED
// ============================================================
struct WiFiCredential {
  const char* ssid;
  const char* password;
};

WiFiCredential wifiCredentials[] = {
  {"PLDTHOMEFIBR6gzYE", "ulap12@MJ@5x"},
  {"Redmi Note 13 Pro 5G", "12345678"},
  {"Lloydie", "zzzzzzzz"},
  {"Bili ka wifi mo !!!!", "qwertyuioplkjhgfdsa1234567890"},
  {"Niel Pakonek", "hahaha123"},

  // Add more Wi-Fi networks here if needed:
  // {"WIFI/HOTSPOT_NAME", "WIFI/HOTSPOT_PASSWORD"},
};

const int WIFI_COUNT = sizeof(wifiCredentials) / sizeof(wifiCredentials[0]);
const unsigned long WIFI_ATTEMPT_TIMEOUT = 10000;
const unsigned long WIFI_CHECK_INTERVAL = 10000;

unsigned long lastWifiCheck = 0;

// ============================================================
// FIREBASE RTDB CONFIG
// Anonymous sign-in removed.
// This uses direct REST writes based on your revised RTDB rules.
// ============================================================
#define FIREBASE_DATABASE_URL "https://hairfit-ar-mirror-default-rtdb.asia-southeast1.firebasedatabase.app"

#define DEVICE_ID "tunnel-worker-1"
#define DEVICE_NAME "Tunnel Worker 1"

const unsigned long FIREBASE_WRITE_INTERVAL = 5000;
unsigned long lastFirebaseWrite = 0;

// ============================================================
// OLED CONFIG
// ============================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

#define I2C_SDA 8
#define I2C_SCL 9

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================
// DHT11 CONFIG
// ============================================================
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// ============================================================
// HC-SR04 CONFIG
// Updated working ESP32-S3 pins
// ============================================================
#define TRIG_PIN 15
#define ECHO_PIN 16

const unsigned long ULTRASONIC_TIMEOUT = 30000UL;

// ============================================================
// BLE UART CONFIG
// ============================================================
#define BLE_DEVICE_NAME "Tunnel Worker 1"

#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_CHAR_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_CHAR_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

NimBLEServer* pServer = nullptr;
NimBLEService* pService = nullptr;
NimBLECharacteristic* pTxCharacteristic = nullptr;
NimBLECharacteristic* pRxCharacteristic = nullptr;

bool bleClientConnected = false;

unsigned long bleConnectedAt = 0;
unsigned long lastBleSend = 0;

const unsigned long BLE_SEND_INTERVAL = 2000;

// ============================================================
// THRESHOLDS
// ============================================================
const float TEMP_CAUTION_LIMIT = 32.0;
const float TEMP_DANGER_LIMIT  = 35.0;

const float HUM_CAUTION_LIMIT  = 70.0;
const float HUM_DANGER_LIMIT   = 85.0;

const float DIST_CAUTION_LIMIT = 70.0;
const float DIST_DANGER_LIMIT  = 30.0;

// ============================================================
// TIMING
// ============================================================
unsigned long lastSensorRead = 0;
unsigned long lastBlink = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long BLINK_INTERVAL  = 350;

bool blinkState = false;
bool oledNeedsRedraw = false;

// ============================================================
// SENSOR VALUES
// ============================================================
float temperatureC = NAN;
float humidityPct = NAN;
float distanceCM = -1.0;

bool dhtOk = false;
bool ultrasonicOk = false;

// ============================================================
// SYSTEM STATUS
// ============================================================
enum SystemStatus {
  STATUS_SAFE,
  STATUS_CAUTION,
  STATUS_DANGER
};

SystemStatus currentStatus = STATUS_SAFE;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================
float readDistanceCM();
SystemStatus getSystemStatus();
const char* getStatusText();
const char* getAlertMessage();

void drawCenteredText(const char* text, int y, int textSize, uint16_t color);
void drawRow(int y, const char* label, const char* value);
void drawMatrixBorder(bool phase);
void drawOLED();

void printSerialData();
void readSensors();

void sendBleStatus();
void sendBleLine(String line);
void setupBLE();

bool connectWiFi();
void checkWiFiReconnect();
bool syncTimeNTP();
String getEpochMillisString();

String escapeJsonString(const String& text);
String buildFirebasePayload();
bool firebasePutJSON(const String& path, const String& jsonPayload);
bool firebasePostJSON(const String& path, const String& jsonPayload);
void writeToFirebase(bool force = false);

// ============================================================
// BLE CALLBACKS
// ============================================================
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    bleClientConnected = true;
    bleConnectedAt = millis();
    oledNeedsRedraw = true;

    Serial.println("BLE client connected.");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    bleClientConnected = false;
    oledNeedsRedraw = true;

    Serial.print("BLE client disconnected, reason = ");
    Serial.println(reason);

    NimBLEDevice::startAdvertising();
    Serial.println("BLE advertising restarted.");
  }
};

// ============================================================
// READ ULTRASONIC DISTANCE
// ============================================================
float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, ULTRASONIC_TIMEOUT);

  if (duration == 0) {
    return -1.0;
  }

  return duration * 0.0343 / 2.0;
}

// ============================================================
// DETERMINE SYSTEM STATUS
// ============================================================
SystemStatus getSystemStatus() {
  bool tempDanger = dhtOk && temperatureC >= TEMP_DANGER_LIMIT;
  bool humDanger  = dhtOk && humidityPct >= HUM_DANGER_LIMIT;
  bool distDanger = ultrasonicOk && distanceCM <= DIST_DANGER_LIMIT;

  bool tempCaution = dhtOk && temperatureC >= TEMP_CAUTION_LIMIT;
  bool humCaution  = dhtOk && humidityPct >= HUM_CAUTION_LIMIT;
  bool distCaution = ultrasonicOk &&
                     distanceCM > DIST_DANGER_LIMIT &&
                     distanceCM <= DIST_CAUTION_LIMIT;

  if (tempDanger || humDanger || distDanger) {
    return STATUS_DANGER;
  }

  if (tempCaution || humCaution || distCaution) {
    return STATUS_CAUTION;
  }

  return STATUS_SAFE;
}

// ============================================================
// STATUS TEXT
// ============================================================
const char* getStatusText() {
  if (currentStatus == STATUS_DANGER) {
    return "DANGER";
  }

  if (currentStatus == STATUS_CAUTION) {
    return "CAUTION";
  }

  return "SAFE";
}

// ============================================================
// ALERT MESSAGE
// ============================================================
const char* getAlertMessage() {
  if (ultrasonicOk && distanceCM <= DIST_DANGER_LIMIT) {
    return "OBSTACLE TOO CLOSE";
  }

  if (dhtOk && temperatureC >= TEMP_DANGER_LIMIT) {
    return "HIGH TEMPERATURE";
  }

  if (dhtOk && humidityPct >= HUM_DANGER_LIMIT) {
    return "VERY HUMID AREA";
  }

  if (ultrasonicOk &&
      distanceCM > DIST_DANGER_LIMIT &&
      distanceCM <= DIST_CAUTION_LIMIT) {
    return "NEARBY OBSTACLE";
  }

  if (dhtOk && temperatureC >= TEMP_CAUTION_LIMIT) {
    return "WARM ENVIRONMENT";
  }

  if (dhtOk && humidityPct >= HUM_CAUTION_LIMIT) {
    return "HUMID AREA";
  }

  return "NORMAL OPERATION";
}

// ============================================================
// CENTERED TEXT HELPER
// ============================================================
void drawCenteredText(const char* text, int y, int textSize, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(textSize);
  display.setTextColor(color);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - w) / 2;

  if (x < 0) {
    x = 0;
  }

  display.setCursor(x, y);
  display.print(text);
}

// ============================================================
// RIGHT-ALIGNED SENSOR ROW
// ============================================================
void drawRow(int y, const char* label, const char* value) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(4, y);
  display.print(label);

  display.getTextBounds(value, 0, y, &x1, &y1, &w, &h);

  int x = SCREEN_WIDTH - w - 4;

  if (x < 45) {
    x = 45;
  }

  display.setCursor(x, y);
  display.print(value);
}

// ============================================================
// MATRIX-STYLE WARNING BORDER
// ============================================================
void drawMatrixBorder(bool phase) {
  for (int x = 0; x < SCREEN_WIDTH; x += 4) {
    if (((x / 4) + phase) % 2 == 0) {
      display.drawPixel(x, 0, SSD1306_WHITE);
      display.drawPixel(x, SCREEN_HEIGHT - 1, SSD1306_WHITE);
    }
  }

  for (int y = 0; y < SCREEN_HEIGHT; y += 4) {
    if (((y / 4) + phase) % 2 == 0) {
      display.drawPixel(0, y, SSD1306_WHITE);
      display.drawPixel(SCREEN_WIDTH - 1, y, SSD1306_WHITE);
    }
  }
}

// ============================================================
// DRAW OLED DISPLAY
// ============================================================
void drawOLED() {
  char tempStr[16];
  char humStr[16];
  char distStr[16];

  display.clearDisplay();

  if (currentStatus == STATUS_SAFE) {
    display.fillRect(0, 0, SCREEN_WIDTH, 11, SSD1306_WHITE);
    drawCenteredText("SAFE", 2, 1, SSD1306_BLACK);
  }
  else if (currentStatus == STATUS_CAUTION) {
    if (blinkState) {
      display.fillRect(0, 0, SCREEN_WIDTH, 11, SSD1306_WHITE);
      drawCenteredText("CAUTION", 2, 1, SSD1306_BLACK);
    } else {
      display.drawRect(0, 0, SCREEN_WIDTH, 11, SSD1306_WHITE);
      drawCenteredText("CAUTION", 2, 1, SSD1306_WHITE);
    }
  }
  else {
    if (blinkState) {
      display.fillRect(0, 0, SCREEN_WIDTH, 11, SSD1306_WHITE);
      drawCenteredText("DANGER", 2, 1, SSD1306_BLACK);
    } else {
      display.drawRect(0, 0, SCREEN_WIDTH, 11, SSD1306_WHITE);
      drawCenteredText("DANGER", 2, 1, SSD1306_WHITE);
    }
  }

  display.setTextSize(1);

  if (currentStatus == STATUS_SAFE || blinkState) {
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }

  display.setCursor(108, 2);
  display.print(bleClientConnected ? "B" : "-");

  display.setCursor(118, 2);
  display.print(WiFi.status() == WL_CONNECTED ? "W" : "-");

  display.drawLine(0, 13, SCREEN_WIDTH, 13, SSD1306_WHITE);

  if (dhtOk) {
    snprintf(tempStr, sizeof(tempStr), "%.1f C", temperatureC);
    snprintf(humStr, sizeof(humStr), "%.1f %%", humidityPct);
  } else {
    snprintf(tempStr, sizeof(tempStr), "ERROR");
    snprintf(humStr, sizeof(humStr), "ERROR");
  }

  if (ultrasonicOk) {
    snprintf(distStr, sizeof(distStr), "%.1f cm", distanceCM);
  } else {
    snprintf(distStr, sizeof(distStr), "NO ECHO");
  }

  drawRow(18, "TEMP", tempStr);
  drawRow(30, "HUM ", humStr);
  drawRow(42, "DIST", distStr);

  display.drawLine(0, 52, SCREEN_WIDTH, 52, SSD1306_WHITE);
  drawCenteredText(getAlertMessage(), 55, 1, SSD1306_WHITE);

  if (currentStatus != STATUS_SAFE) {
    drawMatrixBorder(blinkState);
  }

  display.display();
}

// ============================================================
// SERIAL MONITOR OUTPUT
// ============================================================
void printSerialData() {
  Serial.println("================================");

  Serial.print("Temperature: ");
  if (dhtOk) {
    Serial.print(temperatureC);
  } else {
    Serial.print("ERROR");
  }
  Serial.println(" C");

  Serial.print("Humidity   : ");
  if (dhtOk) {
    Serial.print(humidityPct);
  } else {
    Serial.print("ERROR");
  }
  Serial.println(" %");

  Serial.print("Distance   : ");
  if (ultrasonicOk) {
    Serial.print(distanceCM);
  } else {
    Serial.print("NO ECHO");
  }
  Serial.println(" cm");

  Serial.print("Status     : ");
  Serial.println(getStatusText());

  Serial.print("Message    : ");
  Serial.println(getAlertMessage());

  Serial.print("BLE        : ");
  Serial.println(bleClientConnected ? "CONNECTED" : "ADVERTISING / NOT CONNECTED");

  Serial.print("Wi-Fi      : ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("CONNECTED to ");
    Serial.println(WiFi.SSID());
  } else {
    Serial.println("NOT CONNECTED");
  }
}

// ============================================================
// READ ALL SENSORS
// ============================================================
void readSensors() {
  humidityPct = dht.readHumidity();
  temperatureC = dht.readTemperature();

  dhtOk = !(isnan(humidityPct) || isnan(temperatureC));

  distanceCM = readDistanceCM();
  ultrasonicOk = distanceCM > 0;

  currentStatus = getSystemStatus();

  printSerialData();
  drawOLED();
}

// ============================================================
// BLE SEND LINE
// ============================================================
void sendBleLine(String line) {
  if (!bleClientConnected) {
    return;
  }

  line += "\r\n";

  pTxCharacteristic->setValue(line.c_str());
  pTxCharacteristic->notify();

  delay(20);
}

// ============================================================
// BLE SEND STATUS
// ============================================================
void sendBleStatus() {
  if (!bleClientConnected) {
    return;
  }

  if (millis() - bleConnectedAt < 1000) {
    return;
  }

  if (millis() - lastBleSend < BLE_SEND_INTERVAL) {
    return;
  }

  lastBleSend = millis();

  sendBleLine("====================");
  sendBleLine("Tunnel Worker 1");

  String statusLine = "Status: ";
  statusLine += getStatusText();
  sendBleLine(statusLine);

  if (dhtOk) {
    String tempLine = "Temp  : ";
    tempLine += String(temperatureC, 1);
    tempLine += " C";
    sendBleLine(tempLine);

    String humLine = "Hum   : ";
    humLine += String(humidityPct, 1);
    humLine += " %";
    sendBleLine(humLine);
  } else {
    sendBleLine("Temp  : ERROR");
    sendBleLine("Hum   : ERROR");
  }

  if (ultrasonicOk) {
    String distLine = "Dist  : ";
    distLine += String(distanceCM, 1);
    distLine += " cm";
    sendBleLine(distLine);
  } else {
    sendBleLine("Dist  : NO ECHO");
  }

  String alertLine = "Alert : ";
  alertLine += getAlertMessage();
  sendBleLine(alertLine);

  if (WiFi.status() == WL_CONNECTED) {
    String wifiLine = "WiFi  : CONNECTED ";
    wifiLine += WiFi.SSID();
    sendBleLine(wifiLine);
  } else {
    sendBleLine("WiFi  : NOT CONNECTED");
  }

  sendBleLine("BLE   : CONNECTED");
  sendBleLine("");
}

// ============================================================
// BLE INITIALIZATION
// ============================================================
void setupBLE() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setDeviceName(BLE_DEVICE_NAME);
  NimBLEDevice::setMTU(185);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  pService = pServer->createService(NUS_SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    NUS_TX_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pRxCharacteristic = pService->createCharacteristic(
    NUS_RX_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );

  pTxCharacteristic->setValue("Tunnel Worker 1 READY\r\n");

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->enableScanResponse(true);
  pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->start();

  Serial.println("BLE UART ready.");
  Serial.print("Device Name: ");
  Serial.println(BLE_DEVICE_NAME);
}

// ============================================================
// WIFI CONNECT WITH MULTIPLE CREDENTIALS
// ============================================================
bool connectWiFi() {
  Serial.println();
  Serial.println("Starting Wi-Fi connection loop...");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  for (int i = 0; i < WIFI_COUNT; i++) {
    Serial.print("Trying Wi-Fi ");
    Serial.print(i + 1);
    Serial.print(" of ");
    Serial.print(WIFI_COUNT);
    Serial.print(": ");
    Serial.println(wifiCredentials[i].ssid);

    WiFi.disconnect(false, false);
    delay(300);
    WiFi.mode(WIFI_STA);

    WiFi.begin(wifiCredentials[i].ssid, wifiCredentials[i].password);

    unsigned long startAttempt = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttempt < WIFI_ATTEMPT_TIMEOUT) {
      delay(500);
      Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wi-Fi connected successfully.");
      Serial.print("Connected SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      oledNeedsRedraw = true;
      return true;
    }

    Serial.println("Failed to connect to this Wi-Fi. Trying next credential...");
  }

  Serial.println("All Wi-Fi credentials failed.");
  Serial.println("System will continue using OLED and BLE monitoring.");

  oledNeedsRedraw = true;
  return false;
}

// ============================================================
// WIFI RECONNECT CHECK
// ============================================================
void checkWiFiReconnect() {
  if (millis() - lastWifiCheck < WIFI_CHECK_INTERVAL) {
    return;
  }

  lastWifiCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Running Wi-Fi credential loop again...");
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      syncTimeNTP();
    }

    oledNeedsRedraw = true;
  }
}

// ============================================================
// NTP TIME SYNC
// ============================================================
bool syncTimeNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NTP skipped because Wi-Fi is not connected.");
    return false;
  }

  Serial.println("Syncing time with NTP...");

  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

  time_t now = time(nullptr);
  unsigned long startAttempt = millis();

  while (now < 1700000000 && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println();

  if (now >= 1700000000) {
    Serial.println("NTP time synchronized.");
    return true;
  }

  Serial.println("NTP time sync failed. Timestamp may use runtime fallback.");
  return false;
}

// ============================================================
// GET UNIX EPOCH MILLISECONDS AS STRING
// ============================================================
String getEpochMillisString() {
  time_t now = time(nullptr);

  if (now >= 1700000000) {
    unsigned long long epochMs = ((unsigned long long)now * 1000ULL) + (millis() % 1000);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu", epochMs);
    return String(buffer);
  }

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%lu", millis());
  return String(buffer);
}

// ============================================================
// ESCAPE JSON STRING
// ============================================================
String escapeJsonString(const String& text) {
  String escaped = "";

  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);

    if (c == '"') {
      escaped += "\\\"";
    } else if (c == '\\') {
      escaped += "\\\\";
    } else {
      escaped += c;
    }
  }

  return escaped;
}

// ============================================================
// BUILD FIREBASE PAYLOAD
// ============================================================
String buildFirebasePayload() {
  float sendTemp = dhtOk ? temperatureC : -1.0;
  float sendHum = dhtOk ? humidityPct : -1.0;
  float sendDist = ultrasonicOk ? distanceCM : -1.0;

  String payload = "{";

  payload += "\"deviceName\":\"";
  payload += DEVICE_NAME;
  payload += "\",";

  payload += "\"temperatureC\":";
  payload += String(sendTemp, 1);
  payload += ",";

  payload += "\"humidityPct\":";
  payload += String(sendHum, 1);
  payload += ",";

  payload += "\"distanceCM\":";
  payload += String(sendDist, 1);
  payload += ",";

  payload += "\"dhtOk\":";
  payload += dhtOk ? "true" : "false";
  payload += ",";

  payload += "\"ultrasonicOk\":";
  payload += ultrasonicOk ? "true" : "false";
  payload += ",";

  payload += "\"bleConnected\":";
  payload += bleClientConnected ? "true" : "false";
  payload += ",";

  payload += "\"status\":\"";
  payload += getStatusText();
  payload += "\",";

  payload += "\"alert\":\"";
  payload += escapeJsonString(String(getAlertMessage()));
  payload += "\",";

  payload += "\"timestamp\":";
  payload += getEpochMillisString();

  payload += "}";

  return payload;
}

// ============================================================
// FIREBASE PUT JSON
// Writes latest snapshot.
// ============================================================
bool firebasePutJSON(const String& path, const String& jsonPayload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Firebase PUT skipped because Wi-Fi is not connected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = String(FIREBASE_DATABASE_URL) + "/" + path + ".json?print=silent";

  Serial.print("PUT URL: ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Firebase PUT HTTP begin failed.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  int httpCode = http.PUT(jsonPayload);
  String response = http.getString();

  http.end();

  if (httpCode == 200 || httpCode == 204) {
    return true;
  }

  Serial.print("Firebase PUT failed. HTTP code: ");
  Serial.println(httpCode);
  Serial.println(response);

  return false;
}

// ============================================================
// FIREBASE POST JSON
// Pushes history record.
// ============================================================
bool firebasePostJSON(const String& path, const String& jsonPayload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Firebase POST skipped because Wi-Fi is not connected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = String(FIREBASE_DATABASE_URL) + "/" + path + ".json?print=silent";

  Serial.print("POST URL: ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Firebase POST HTTP begin failed.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(jsonPayload);
  String response = http.getString();

  http.end();

  if (httpCode == 200 || httpCode == 204) {
    return true;
  }

  Serial.print("Firebase POST failed. HTTP code: ");
  Serial.println(httpCode);
  Serial.println(response);

  return false;
}

// ============================================================
// WRITE TO FIREBASE
// ============================================================
void writeToFirebase(bool force) {
  if (!force && millis() - lastFirebaseWrite < FIREBASE_WRITE_INTERVAL) {
    return;
  }

  lastFirebaseWrite = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Firebase write skipped because Wi-Fi is not connected.");
    return;
  }

  String payload = buildFirebasePayload();

  String latestPath = "helmetLogs/devices/";
  latestPath += DEVICE_ID;
  latestPath += "/latest";

  String historyPath = "helmetLogs/devices/";
  historyPath += DEVICE_ID;
  historyPath += "/history";

  Serial.println("Writing to Firebase RTDB...");
  Serial.println(payload);

  bool latestOk = firebasePutJSON(latestPath, payload);
  bool historyOk = firebasePostJSON(historyPath, payload);

  if (latestOk && historyOk) {
    Serial.println("Firebase write successful: latest + history updated.");
  } else if (latestOk) {
    Serial.println("Firebase partial success: latest updated, history failed.");
  } else if (historyOk) {
    Serial.println("Firebase partial success: history updated, latest failed.");
  } else {
    Serial.println("Firebase write failed.");
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found. Check wiring and I2C address.");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("TUNNEL WORKER", 12, 1, SSD1306_WHITE);
  drawCenteredText("SMART HELMET", 25, 1, SSD1306_WHITE);
  drawCenteredText("BLE STARTING", 43, 1, SSD1306_WHITE);
  display.display();

  setupBLE();

  delay(1000);

  display.clearDisplay();
  drawCenteredText("CONNECTING WIFI", 20, 1, SSD1306_WHITE);
  drawCenteredText("PLEASE WAIT", 36, 1, SSD1306_WHITE);
  display.display();

  bool wifiOk = connectWiFi();

  if (wifiOk) {
    syncTimeNTP();
  }

  display.clearDisplay();
  drawCenteredText("TUNNEL WORKER", 12, 1, SSD1306_WHITE);
  drawCenteredText("SMART HELMET", 25, 1, SSD1306_WHITE);
  drawCenteredText("INITIALIZING", 43, 1, SSD1306_WHITE);
  display.display();

  delay(1500);

  readSensors();
  writeToFirebase(true);

  Serial.println("System initialized successfully.");
  Serial.println("Device is now monitoring through OLED, BLE, and Firebase RTDB.");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  checkWiFiReconnect();

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    readSensors();
  }

  if (currentStatus != STATUS_SAFE && now - lastBlink >= BLINK_INTERVAL) {
    lastBlink = now;
    blinkState = !blinkState;
    drawOLED();
  }

  if (oledNeedsRedraw) {
    oledNeedsRedraw = false;
    drawOLED();
  }

  sendBleStatus();
  writeToFirebase(false);
}
