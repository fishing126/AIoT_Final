// 這是風扇控制除錯測試程式
// 將 MOSFET 的 GATE (G) 腳位接到 ESP32 的 D18 (GPIO 18)
int fanPin = 18;

void setup() {
  // 初始化 Serial Monitor (序列埠監控視窗)，設定鮑率為 115200
  Serial.begin(115200);
  
  // 設定 D18 為輸出模式
  pinMode(fanPin, OUTPUT);
  
  Serial.println(F("\n--- 系統開機 ---"));
  Serial.println(F("開始執行風扇除錯測試..."));
  Serial.print(F("控制腳位設定為: D"));
  Serial.println(fanPin);
  Serial.println(F("----------------\n"));
}

void loop() {
  Serial.println(F("[動作] 準備導通 MOSFET (設定腳位為 HIGH)"));
  digitalWrite(fanPin, HIGH);  
  
  // 讀取腳位目前的真實狀態來驗證 ESP32 內部有沒有當機
  int state = digitalRead(fanPin);
  Serial.print(F("[驗證] 讀取 D18 腳位內部狀態: "));
  Serial.println(state == HIGH ? F("HIGH (有通電)") : F("LOW (沒通電)"));
  Serial.println(F(">> 此時風扇應該要【轉動】 (維持 10 秒)"));
  Serial.println(F("----------------\n"));
  
  delay(10000); // 10秒

  Serial.println(F("[動作] 準備關閉 MOSFET (設定腳位為 LOW)"));
  digitalWrite(fanPin, LOW);   
  
  state = digitalRead(fanPin);
  Serial.print(F("[驗證] 讀取 D18 腳位內部狀態: "));
  Serial.println(state == HIGH ? F("HIGH (有通電)") : F("LOW (沒通電)"));
  Serial.println(F(">> 此時風扇應該要【停止】 (維持 5 秒)"));
  Serial.println(F("----------------\n"));
  
  delay(5000); // 5秒
}
