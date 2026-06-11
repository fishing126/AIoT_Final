# ChatGPT 對話紀錄：智慧溫控與餵食計時系統 (ESP32 + FastAPI + Streamlit)

---

**User**:
我期末專案想要用 ESP32 做一個智慧寵物箱的環境控制。
硬體有：
- ESP32 開發板
- DHT11 溫濕度感測器 (接在 GPIO 4)
- 風扇加 MOSFET 模組 (接 GPIO 18)
- 一個實體按鈕 (接 GPIO 19)
- 還有板子上的 LED (GPIO 2)

我想做出每 2 秒讀一次溫濕度，然後有一個 200 秒的餵食倒數計時器，按按鈕就重置回 200 秒。如果溫度大於 26 度開風扇，小於 22 度關風扇。還要能用手機連同個 Wi-Fi 開網頁看到這些資料。
你可以先幫我寫一個 ESP32 自己當 Web Server 的程式碼嗎？

---

**AI**:
沒問題，這個架構很清晰。我們可以先用 ESP32 內建的 `WebServer` 函式庫來寫獨立運行版本。當使用者連上 ESP32 架設的 Web 伺服器時，會回傳一個 HTML 網頁。

以下是 `smart_fan_standalone.ino` 的基本程式碼：

```cpp
#include <SimpleDHT.h>
#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "您的_WiFi_名稱";
const char* WIFI_PASS = "您的_WiFi_密碼";

const int pinDHT11  = 4;
const int fanPin    = 18;
const int ledPin    = 2;
const int buttonPin = 19;

SimpleDHT11 dht11(pinDHT11);
WebServer server(80);

int currentTemp = 0;
int currentHumi = 0;
bool fanRunning = false;
int timerRemaining = 200;
unsigned long lastTimerTick = 0;

void handleRoot() {
  String html = "<html><body>";
  html += "<h1>智能環境監控</h1>";
  html += "<p>溫度: " + String(currentTemp) + " C</p>";
  html += "<p>濕度: " + String(currentHumi) + " %</p>";
  html += "<p>計時器: " + String(timerRemaining) + " 秒</p>";
  html += "<p>風扇狀態: " + String(fanRunning ? "開啟" : "關閉") + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  pinMode(fanPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi 連線成功，IP 位址: " + WiFi.localIP().toString());
  
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  
  // 每秒更新計時器
  if (millis() - lastTimerTick >= 1000) {
    lastTimerTick = millis();
    if (timerRemaining > 0) timerRemaining--;
  }
  
  // 讀取按鈕 (低電位代表按下)
  if (digitalRead(buttonPin) == LOW) {
    timerRemaining = 200;
  }
  
  // 讀取 DHT11
  byte temp = 0, humi = 0;
  if (dht11.read(&temp, &humi, NULL) == SimpleDHTErrSuccess) {
    currentTemp = temp;
    currentHumi = humi;
  }
  
  // 溫控邏輯
  if (currentTemp >= 26) {
    fanRunning = true;
    digitalWrite(fanPin, HIGH);
  } else if (currentTemp <= 22) {
    fanRunning = false;
    digitalWrite(fanPin, LOW);
  }
  delay(100);
}
```

請將程式碼中的 Wi-Fi 設定修改為您的 AP，並在 Arduino IDE 安裝 `SimpleDHT` 函式庫後進行燒錄。

---

**User**:
我編譯報錯了，說 `SimpleDHT.h: No such file or directory` 這是什麼意思？
還有我的按鈕按下去會一直抖，按一下計時器會重置好幾次。
對了，這個網頁一定要手動重整才會更新溫度，有沒有辦法讓它自己每秒更新啊？不然每次都要按重整很卡。

---

**AI**:
這個錯誤代表您尚未安裝 DHT11 函式庫。請在 Arduino IDE 選單中選擇：`草稿碼 (Sketch) -> 匯入函式庫 (Include Library) -> 管理函式庫 (Manage Libraries...)`，搜尋並安裝 **SimpleDHT**。

關於按鈕彈跳與網頁自動更新的問題，我已經將程式碼做以下修改：
1. **按鈕防抖 (Debounce)**：加入時間差判斷（如 50ms 內忽略重複訊號），只在按鈕訊號穩定由高變低時才觸發重置。
2. **網頁異步更新 (AJAX)**：網頁不再每次都重整，而是讓 ESP32 另外提供一個 `/api/status` 的 JSON 路由。前端 HTML 網頁載入後，會透過 JavaScript 的 `fetch()` 每秒自動去跟 ESP32 拿 JSON 資料並動態更新畫面的 HTML 元素，這樣畫面就不會閃爍了。

以下是修正後的程式碼片段與網頁設計邏輯：

```cpp
// 在路徑 /api/status 提供 JSON 數據
void handleStatus() {
  String json = "{";
  json += "\"temperature\":" + String(currentTemp) + ",";
  json += "\"humidity\":" + String(currentHumi) + ",";
  json += "\"fan_state\":" + String(fanRunning ? "true" : "false") + ",";
  json += "\"timer_remaining\":" + String(timerRemaining);
  json += "}";
  server.send(200, "application/json", json);
}
```

網頁的 HTML 部分，我們可以用 CSS 加上暗色系質感（漸層背景與卡片設計），並加入以下 JavaScript：

```javascript
setInterval(() => {
  fetch('/api/status')
    .then(response => response.json())
    .then(data => {
      document.getElementById('temp').innerText = data.temperature;
      document.getElementById('humi').innerText = data.humidity;
      document.getElementById('timer').innerText = data.timer_remaining;
      document.getElementById('fan').innerText = data.fan_state ? "開啟" : "關閉";
    });
}, 1000);
```

---

**User**:
可以了！自動更新好用多了。
不過我們老師說一定要有伺服器架構，把控制邏輯移到電腦上跑，不能只讓 ESP32 自己跑。
我想要用 Python 的 FastAPI 寫後端，然後 ESP32 用 HTTP POST 把溫濕度傳給電腦，電腦判斷完再回傳 JSON 告訴 ESP32 要不要開風扇。
網頁我想用 Streamlit 做個好看的儀表板。你可以幫我寫 FastAPI 的後端 server.py 嗎？

---

**AI**:
這個分散式架構非常適合期末展示。這樣的話：
1. **ESP32**：每 3 秒發送 POST 請求至電腦 IP（例如 `http://<電腦IP>:8080/api/sensor`），上傳溫濕度，並接收回傳的指令執行風扇開關。
2. **FastAPI (電腦)**：處理邏輯決策（遲滯控制：高於 26°C 開、低於 22°C 關），記錄最近 200 筆資料供圖表讀取，並管理餵食計時器。
3. **Streamlit (電腦)**：向 FastAPI 請求狀態並渲染出精美的控制面版。

以下是 FastAPI 伺服器 `server/server.py` 的核心結構：

```python
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from datetime import datetime
import time

app = FastAPI(title="手宮智慧冷氣 API")

# 全域變數儲存狀態
settings = {
    "temp_on": 26,
    "temp_off": 22,
    "mode": "auto",      # auto = 自動溫控, manual = 手動控制
    "manual_fan": False
}
fan_state = False
timer_duration = 200
timer_start = time.time()

class SensorData(BaseModel):
    device_id: str
    temperature: float
    humidity: float

@app.post("/api/sensor")
async def receive_sensor(data: SensorData):
    global fan_state
    
    # 溫控邏輯（自動模式下）
    if settings["mode"] == "auto":
        if not fan_state and data.temperature > settings["temp_on"]:
            fan_state = True
        elif fan_state and data.temperature < settings["temp_off"]:
            fan_state = False
            
    return {"fan": fan_state}

@app.post("/api/feed")
async def manual_feed():
    global timer_start
    timer_start = time.time() # 重置計時器
    return {"status": "ok"}
```

您可以利用 `pip install fastapi uvicorn` 安裝套件，並用 `uvicorn server:app --host 0.0.0.0 --port 8080` 來啟動伺服器。

---

**User**:
後端跑起來了！那 Streamlit 的監控網頁 streamlit_app.py 怎麼寫？
我想做黑底那種比較帥的暗色系，然後用折線圖顯示溫度和濕度歷史紀錄，還有能不能在網頁上加幾個按鈕，讓我可以手動開關風扇或是切回自動模式？

---

**AI**:
您可以使用 **Streamlit** 搭配 **Plotly** 來畫折線圖。為了解決網頁會一直整頁閃爍刷新的問題，我們可以使用 Streamlit 內建的 `@st.fragment` 來做局部每秒更新（抓取 `/api/status` 的數據）。

這是一個帶有暗系風格的 `streamlit_app.py` 範例：

```python
import streamlit as st
import requests
import plotly.graph_objects as go
from datetime import datetime

st.set_page_config(page_title="手宮智慧冷氣", layout="wide")
SERVER_URL = "http://localhost:8080"

# 載入自訂 CSS 達到暗色磨砂玻璃質感
st.markdown("""
<style>
    [data-testid="stAppViewContainer"] {
        background: linear-gradient(160deg, #0d0d1a 0%, #1a1a2e 60%, #16213e 100%);
        color: white;
    }
</style>
""", unsafe_allow_html=True)

@st.fragment(run_every=1)
def live_dashboard():
    try:
        resp = requests.get(f"{SERVER_URL}/api/status", timeout=2)
        data = resp.json()
        
        # 繪製溫濕度指標
        c1, c2, c3 = st.columns(3)
        c1.metric("當前溫度", f"{data['latest'].get('temperature', '--')} °C")
        c2.metric("當前濕度", f"{data['latest'].get('humidity', '--')} %")
        c3.metric("剩餘照顧時間", f"{data['timer']['remaining']} 秒")
        
        # 繪製歷史圖表
        # 透過 plotly_chart 展示歷史趨勢
        ...
    except Exception as e:
        st.error("無法連線到 FastAPI 伺服器。")

live_dashboard()
```

啟動指令：
```bash
streamlit run server/streamlit_app.py
```

---

**User**:
慘了，我遇到大問題。我們學校的 Wi-Fi 有防火牆阻擋，ESP32 連不上我筆電的 IP，FastAPI 根本收不到資料。
我們過幾天就要上台展示了，要是學校網路不能用我就涼了，有沒有不用 Wi-Fi 的傳輸備案啊？救命。

---

**AI**:
這在學校展示很常發生！不要慌，我們可以實作一個 **USB Serial (序列埠) 橋接功能**作為備案。

1. **原理**：ESP32 透過 USB 線插在電腦上，並改由 `Serial.println()` 印出感測器的 JSON 格式數據。
2. **電腦端**：我們在 FastAPI 伺服器中，用 Python 的 `threading` 開啟一個背景執行緒，隨時去讀取指定的 COM Port（例如使用 `pyserial` 函式庫）。
3. **優點**：即使完全沒有 Wi-Fi 網路，只要插著 USB 線，網頁（FastAPI + Streamlit）依然可以正常監控與操作，完全不需要修改前端網頁。

我在 `server.py` 中為您加入以下 Serial 監聽邏輯：

```python
import serial
import threading
import json

def _serial_bridge():
    # 自動尋找電腦上的 ESP32 COM Port 
    # 開啟序列埠並進行監聽
    while True:
        try:
            ser = serial.Serial('COM3', 115200, timeout=1) # 可自動掃描埠口
            while True:
                line = ser.readline().decode('utf-8').strip()
                if line.startswith("SENSOR:"):
                    # 解析 ESP32 傳過來的 json，更新伺服器狀態
                    data = json.loads(line[7:])
                    # 做溫控決策...
                    # 將決策回傳寫入 Serial：ser.write(b'{"fan": true}\n')
        except Exception as e:
            time.sleep(3)
```

---

**User**:
Serial 橋接可以用，插著 USB 線就可以跑了，太救命了。
但現在有一個超煩的問題，就是我用 Arduino IDE 要重新上傳程式碼的時候，一直跳 `COM port busy` 或 `Access denied`。是不是因為後端 Python 程式一直佔著那個 COM port？每次改 code 都要把 Python 關掉重開很麻煩耶，有沒有辦法暫時釋放 COM port？

---

**AI**:
是的，因為 Python 的 `serial.Serial` 會獨佔該 COM Port，導致 Arduino IDE 無法寫入程式。

為了解決這個痛點，我們可以提供一個「暫停/恢復」機制：
1. 在 FastAPI 中新增 `/api/serial/pause` 和 `/api/serial/resume` API。
2. 當調用 `pause` 時，背景執行緒會關閉序列埠連線並進入等待狀態。
3. 當調用 `resume` 時，再重新打開序列埠。
4. 在 Streamlit 儀表板上加上「開始燒錄（釋放 Port）」的按鈕，按下後即可直接在 Arduino IDE 燒錄，燒完按「恢復」即可繼續監控！

這部分已經在 `server.py` 與 `streamlit_app.py` 中更新，讓您的開發流程更加順暢。

---

**User**:
太棒了，這樣真的方便超多！
最後我想把這堆程式碼全部 push 到 GitHub。可是我資料夾裡有一堆 Python 編譯的 `__pycache__` 還有 Arduino 編譯產生的 `build` 資料夾，我不想上傳這些垃圾檔案，要怎麼設定忽略它們？

---

**AI**:
您可以在專案根目錄建立一個叫 `.gitignore` 的檔案，寫入以下設定：

```text
# Python 暫存檔與虛擬環境
__pycache__/
*.py[cod]
.env
.venv/

# OS 暫存檔
.DS_Store
Thumbs.db

# Arduino 編譯快取
build/
```

這樣執行 `git add .` 時，Git 就會自動忽略這些無用的檔案，只會上傳您的程式碼原始檔了！
