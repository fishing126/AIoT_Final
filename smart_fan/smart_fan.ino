// =============================================
//         手宮智慧冷氣 - 溫控風扇主程式
// =============================================
// 功能：DHT11 偵測溫度，超過 26°C 開啟風扇
//       溫度降至 22°C 以下則關閉風扇
// 接線：
//   DHT11 DATA → GPIO 4
//   MOSFET GATE → GPIO 18 (透過 470Ω 電阻)
// =============================================

#include <SimpleDHT.h>

// --- 腳位設定 ---
const int pinDHT11 = 4;   // DHT11 資料腳
const int fanPin   = 18;  // MOSFET 閘極控制腳
const int ledPin   = 2;   // 板載 LED（風扇開啟時亮起）

// --- 溫度門檻設定 ---
const int TEMP_ON  = 26;  // 超過此溫度 → 開啟風扇
const int TEMP_OFF = 22;  // 低於此溫度 → 關閉風扇

// --- 物件與狀態 ---
SimpleDHT11 dht11(pinDHT11);
bool fanRunning = false;  // 記錄風扇目前狀態

void setup() {
  Serial.begin(115200);
  pinMode(fanPin, OUTPUT);
  pinMode(ledPin, OUTPUT);

  // 開機先確保風扇是關閉的
  digitalWrite(fanPin, LOW);
  digitalWrite(ledPin, LOW);

  delay(2000); // 等待 DHT11 穩定

  Serial.println(F("\n======================================="));
  Serial.println(F("       手宮智慧冷氣 - 系統開機"));
  Serial.println(F("======================================="));
  Serial.print(F("  開啟門檻：超過 "));
  Serial.print(TEMP_ON);
  Serial.println(F(" °C → 開啟風扇"));
  Serial.print(F("  關閉門檻：低於 "));
  Serial.print(TEMP_OFF);
  Serial.println(F(" °C → 關閉風扇"));
  Serial.println(F("=======================================\n"));
}

void loop() {
  byte temperature = 0;
  byte humidity    = 0;
  int  err         = SimpleDHTErrSuccess;

  Serial.println(F("[感測] 讀取 DHT11..."));

  // 讀取溫濕度
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print(F("  ❌ 讀取失敗，錯誤碼: "));
    Serial.println(err);
    Serial.println(F("  (請確認接線是否正確)"));
    delay(3000);
    return; // 讀取失敗就跳過這次，不改變風扇狀態
  }

  int temp = (int)temperature;
  int humi = (int)humidity;

  // 印出目前溫濕度
  Serial.print(F("  🌡️  溫度: "));
  Serial.print(temp);
  Serial.print(F(" °C  |  💧 濕度: "));
  Serial.print(humi);
  Serial.println(F(" %"));

  // --- 溫控邏輯（遲滯控制，防止風扇頻繁開關）---
  if (!fanRunning && temp > TEMP_ON) {
    // 溫度超過 26°C → 開啟風扇
    fanRunning = true;
    digitalWrite(fanPin, HIGH);
    digitalWrite(ledPin, HIGH);
    Serial.println(F("  🔴 溫度過高！→ 風扇【開啟】"));

  } else if (fanRunning && temp < TEMP_OFF) {
    // 溫度低於 22°C → 關閉風扇
    fanRunning = false;
    digitalWrite(fanPin, LOW);
    digitalWrite(ledPin, LOW);
    Serial.println(F("  🟢 溫度正常！→ 風扇【關閉】"));

  } else {
    // 溫度在 22~26°C 之間，維持現狀
    Serial.print(F("  ⚪ 維持現狀 → 風扇目前: "));
    Serial.println(fanRunning ? F("【開啟中】") : F("【關閉中】"));
  }

  Serial.println(F("----------------------------------------"));

  // 每 3 秒讀取一次（DHT11 最快 1 秒一次）
  delay(3000);
}
