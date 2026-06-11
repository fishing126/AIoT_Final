// =============================================
//    手宮智慧冷氣 - ESP32 WiFi 版本
// =============================================
// 架構：
//   1. ESP32 每 3 秒讀取 DHT11 感測器
//   2. 透過 HTTP POST 傳送 JSON 到本地伺服器
//   3. 伺服器回傳風扇控制指令
//   4. ESP32 依照指令控制風扇
//   5. 按下實體按鈕 → 傳送事件到伺服器（手動切換模式）
//
// 需要安裝的函式庫（Arduino IDE → Library Manager）：
//   - SimpleDHT
//   - ArduinoJson (by Benoit Blanchon, 版本 6.x)
// =============================================

#include <SimpleDHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =============================================
// ★ 請修改以下設定 ★
// =============================================
const char* WIFI_SSID   = "DFN";      // WiFi 名稱
const char* WIFI_PASS   = "Illyasviel1120";   // WiFi 密碼
const char* SERVER_IP   = "192.168.10.199";       // 電腦的 IP（Wi-Fi）
const int   SERVER_PORT = 8080;
const char* DEVICE_ID   = "esp32_pet_box_01";     // 裝置識別碼
// =============================================

// --- 腳位設定 ---
const int pinDHT11  = 4;   // DHT11 DATA
const int fanPin    = 18;  // MOSFET Gate（透過 470Ω 電阻）
const int ledPin    = 2;   // 板載 LED
const int buttonPin = 19;  // JTP1230F 按鈕（另一腳接 GND）

// --- 物件與狀態 ---
SimpleDHT11 dht11(pinDHT11);
bool fanRunning = false;

// --- 按鈕防抖 ---
bool     lastButtonState  = HIGH;  // 上次按鈕狀態（未按 = HIGH，因為內部上拉）
bool     buttonState      = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50; // 防抖延遲 50ms

// --- 計時器 (非阻塞式，不用 delay) ---
unsigned long lastSendTime    = 0;
const unsigned long SEND_INTERVAL = 3000; // 每 3 秒傳送一次

// =============================================
//  WiFi 連線
// =============================================
void connectWiFi() {
  Serial.print(F("\n[WiFi] 連線到: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) { // 等待最多 20 秒
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[WiFi] ✅ 連線成功！"));
    Serial.print(F("[WiFi] ESP32 IP 位址 : "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WiFi] Wi-Fi 訊號強度 : "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
    Serial.print(F("[WiFi] 閘道 (Gateway) : "));
    Serial.println(WiFi.gatewayIP());
  } else {
    Serial.println(F("\n[WiFi] ❌ 連線失敗！請確認 SSID/密碼"));
  }
}

// =============================================
//  啟動時測試伺服器連線
// =============================================
void testServerConnection() {
  Serial.println(F("\n[診斷] 測試伺服器連線中..."));
  Serial.print(F("[診斷] 目標：http://"));
  Serial.print(SERVER_IP);
  Serial.print(F(":"));
  Serial.print(SERVER_PORT);
  Serial.println(F("/api/status"));

  HTTPClient http;
  String url = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) + "/api/status";
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    Serial.println(F("[診斷] ✅ 伺服器連線成功！系統準備就緒。"));
  } else if (code < 0) {
    Serial.println(F("[診斷] ❌ 無法連到伺服器！可能原因："));
    Serial.println(F("        1. 電腦伺服器未啟動 (python server.py)"));
    Serial.println(F("        2. SERVER_IP 與電腦 IP 不符"));
    Serial.println(F("        3. 電腦防火牆阻擋 8080 埠"));
    Serial.print(F("        錯誤代碼: "));
    Serial.println(code);
  } else {
    Serial.print(F("[診斷] ⚠️ 伺服器回應異常，HTTP 狀態碼: "));
    Serial.println(code);
  }
  http.end();
  Serial.println(F("----------------------------------------"));
}

// =============================================
//  傳送按鈕事件到伺服器（記錄餵食時間）
// =============================================
void sendButtonPress() {
  StaticJsonDocument<64> reqDoc;
  reqDoc["device_id"] = DEVICE_ID;
  String reqBody;
  serializeJson(reqDoc, reqBody);

  // 透過 USB 傳送按鈕事件（保留 HTTP 風格的 log 輸出）
  Serial.print(F("[HTTP] POST /api/button "));
  Serial.println(reqBody);

  // 實際資料經由 USB 傳送
  Serial.print(F("BUTTON:"));
  Serial.println(reqBody);

  // 等待確認（2 秒）
  unsigned long t = millis();
  while (!Serial.available() && millis() - t < 2000) delay(10);
  if (Serial.available()) {
    String resp = Serial.readStringUntil('\n');
    Serial.println(F("[按鈕] ✅ 饑食時間已記錄！計時器重置。"));
  } else {
    Serial.println(F("[按鈕] ⚠️ 橋接器無回應"));
  }

  Serial.println(F("[HTTP] 按鈕事件傳送完成"));
}


// =============================================
//  傳送感測器資料 → 取得風扇指令
// =============================================
void sendSensorData(int temp, int humi) {
  HTTPClient http;

  String url = "http://" + String(SERVER_IP) + ":" + 
               String(SERVER_PORT) + "/api/sensor";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000); // 5 秒 timeout

  // 建立 JSON 請求
  StaticJsonDocument<192> reqDoc;  // 送出的 JSON 不含中文，192 足夠
  reqDoc["device_id"]   = DEVICE_ID;
  reqDoc["temperature"] = temp;
  reqDoc["humidity"]    = humi;
  reqDoc["fan_state"]   = fanRunning;

  String reqBody;
  serializeJson(reqDoc, reqBody);

  int httpCode = http.POST(reqBody);

  if (httpCode == 200) {
    String response = http.getString();

    // 解析伺服器回傳的 JSON 指令
    StaticJsonDocument<512> resDoc;
    DeserializationError err = deserializeJson(resDoc, response);

    if (!err) {
      bool newFanState = resDoc["fan"].as<bool>();
      String reason    = resDoc["reason"].as<String>();

      // ★ 直接依照伺服器指令執行，不判斷是否改變
      // 伺服器是唯一的事實來源，ESP32 每 3 秒同步一次
      if (newFanState != fanRunning) {
        // 狀態有變化時才印 log
        Serial.print(F("\n[風扇] 狀態改變 → "));
        Serial.println(newFanState ? F("🔴 開啟") : F("🟢 關閉"));
        if (reason.length() > 0) {
          Serial.print(F("[風扇] 原因: "));
          Serial.println(reason);
        }
      }

      // 不管有沒有改變，每次都強制寫入 GPIO
      fanRunning = newFanState;
      digitalWrite(fanPin, fanRunning ? HIGH : LOW);
      digitalWrite(ledPin, fanRunning ? HIGH : LOW);

    } else {
      Serial.print(F("[HTTP] JSON 解析錯誤: "));
      Serial.println(err.c_str());
    }

  } else if (httpCode < 0) {
    Serial.println(F("[HTTP] ❌ 無法連線到伺服器，請確認："));
    Serial.println(F("       1. 伺服器是否已啟動 (python server.py)"));
    Serial.println(F("       2. SERVER_IP 是否正確"));
    Serial.println(F("       3. 電腦防火牆是否阻擋 8080 埠"));
  } else {
    Serial.print(F("[HTTP] 伺服器回應錯誤碼: "));
    Serial.println(httpCode);
  }

  http.end();
}

// =============================================
//  Setup
// =============================================
void setup() {
  Serial.begin(115200);
  pinMode(fanPin,    OUTPUT);
  pinMode(ledPin,    OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);  // 內建上拉，按下時讀到 LOW

  // 開機確保風扇關閉
  digitalWrite(fanPin, LOW);
  digitalWrite(ledPin, LOW);

  Serial.println(F("\n======================================"));
  Serial.println(F("   手宮智慧冷氣 - WiFi 版本 啟動"));
  Serial.println(F("======================================"));

  delay(1000); // 等待 DHT11 穩定
  connectWiFi();

  // WiFi 連上後立刻測試伺服器
  if (WiFi.status() == WL_CONNECTED) {
    testServerConnection();
  }

  Serial.println(F("[系統] 開始每 3 秒傳送感測資料..."));
}

// =============================================
//  Loop
// =============================================
void loop() {
  // WiFi 斷線自動重連（背景，不阻斷主要流程）
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] 連線中斷，背景重連中..."));
    WiFi.reconnect(); // 非阻塞式重連
  }

  // ── 按鈕偵測（防抖）──────────────────────────
  bool reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis(); // 狀態改變，重設防抖計時
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // 訊號穩定超過 50ms
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {  // LOW = 按下（因為 INPUT_PULLUP）
        Serial.println(F("\n[按鈕] 🔘 按下！傳送事件到伺服器..."));
        sendButtonPress();
      }
    }
  }

  lastButtonState = reading;
  // ──────────────────────────────────────────────

  // 非阻塞式計時，每 SEND_INTERVAL 毫秒執行一次
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();

    byte temperature = 0;
    byte humidity    = 0;

    if (dht11.read(&temperature, &humidity, NULL) != SimpleDHTErrSuccess) {
      Serial.println(F("[DHT11] ❌ 讀取失敗，跳過本次傳送"));
      return;
    }

    int temp = (int)temperature;
    int humi = (int)humidity;

    Serial.printf("[感測] 🌡️ %d°C  💧 %d%%  → 傳送到伺服器...\n", temp, humi);
    sendSensorData(temp, humi);
  }
}
