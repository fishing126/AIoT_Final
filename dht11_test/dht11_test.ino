#include <SimpleDHT.h>

int pinDHT11 = 4;
SimpleDHT11 dht11(pinDHT11);

// ESP32 開發板上的藍色內建 LED 通常是 GPIO 2
int ledPin = 2;

void setup() {
  // 設定序列埠鮑率為 115200
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  
  // 開機後先刻意等待 3 秒鐘，讓你有時間打開 Serial Monitor
  delay(3000);
  
  Serial.println(F("\n\n"));
  Serial.println(F("======================================="));
  Serial.println(F("        ESP32 主板生存與連線測試       "));
  Serial.println(F("======================================="));
  Serial.println(F("如果你能看到這行字，恭喜！"));
  Serial.println(F("代表你的 ESP32 主板沒壞，USB 線沒壞，"));
  Serial.println(F("而且你的 Serial Monitor 設定完全正確！"));
  Serial.println(F("---------------------------------------\n"));
}

void loop() {
  // 點亮內建 LED (如果你板子上有藍燈，這時應該會亮起)
  digitalWrite(ledPin, HIGH);
  
  Serial.println(F("[動作] 正在嘗試讀取 DHT11 溫度與濕度..."));
  
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;
  
  // 嘗試讀取感測器
  if ((err = dht11.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print(F("❌ 讀取失敗，錯誤碼: ")); 
    Serial.println(err);
    Serial.println(F("   (請檢查 VCC、GND 是否接對，S 是否接在 D4 上)"));
  } else {
    Serial.print(F("✅ 讀取成功！溫度: "));
    Serial.print((int)temperature); 
    Serial.print(F(" °C, 濕度: ")); 
    Serial.print((int)humidity); 
    Serial.println(F(" %"));
  }
  
  // 熄滅內建 LED
  digitalWrite(ledPin, LOW);
  
  // 等待 3 秒再測一次
  Serial.println(F("等待 3 秒..."));
  delay(3000);
}
