# 手宮智慧冷氣 (AIoT 寵物箱環境控制系統) - AI 開發對話紀錄

本對話紀錄模擬了從專案構想、硬體規劃、程式撰寫、功能擴充到除錯的完整流程，適用於課程作業繳交。

---

## 階段一：專案構想與硬體架構討論

### 🙋‍♂️ 學生：
我這學期的 AIoT 課程要做一個期末專案。我想做一個「智慧寵物箱環境控制系統」，主要功能是：
1. 能夠偵測環境的溫度與濕度。
2. 當溫度太高時，會自動開啟風扇降溫；溫度降下來後自動關閉。
3. 寵物箱需要定時餵食或照顧，我想做一個計時器倒數（例如 200 秒，方便展示），並有一個實體按鈕，按下按鈕就代表已照顧，計時器會重置。
4. 希望有網頁介面可以遠端監控數據和手動控制風扇。

請問我該準備哪些硬體？系統架構該怎麼設計比較好？

### 🤖 AI 助手：
這是一個非常完整且實用的 AIoT 專案！結合了**環境感測**、**自動控制**、**人機互動（按鈕）**以及**遠端儀表板**。

以下是我建議的硬體清單與系統架構設計：

#### 1. 建議硬體清單
- **開發板**：**ESP32**（內建 Wi-Fi 與藍牙，非常適合架設網頁或與伺服器連線）。
- **感測器**：**DHT11** 溫濕度感測器。
- **執行器**：**5V 或 12V 直流小風扇**，配合 **MOSFET 驅動模組**（如 IRF520，因為 ESP32 的 GPIO 腳位輸出電流不足以直接驅動風扇）。
- **輸入元件**：**實體按鈕**（配合內建上拉/下拉電阻，用來重置計時器）與**麵包板、杜邦線**。

#### 2. 系統架構方案選擇
我們可以設計兩種方案，由簡入繁：
*   **方案 A：ESP32 獨立運作版 (Standalone)**
    ESP32 自己讀取數據、自己做自動控制，並在 ESP32 上架設一個簡易的 Web Server。手機或電腦連上同一個 Wi-Fi 後，直接開啟 ESP32 的 IP 即可看見監控網頁。這適合初期測試與簡單展示。
*   **方案 B：分散式架構版 (Server + Dashboard)**
    ESP32 只負責採集數據與執行開關指令，並透過 Wi-Fi 或 USB Serial 將數據傳送到一台電腦（Python 伺服器）。電腦上用 **FastAPI** 寫後端 API，再用 **Streamlit** 寫出極具現代感的即時監控儀表板。

我們建議先從 **方案 A** 開始實作，確認硬體運作無誤後，再升級為 **方案 B**。你覺得如何？

---

## 階段二：ESP32 獨立運作版 (Standalone) 開發

### 🙋‍♂️ 學生：
好，那我們先做方案 A。我的腳位接法是：
- DHT11 DATA 接在 **GPIO 4**
- 風扇的 MOSFET Gate 接在 **GPIO 18**
- 實體按鈕接在 **GPIO 19**（另一腳接 GND）
- ESP32 板載 LED 接在 **GPIO 2**

你能幫我寫一份 ESP32 獨立 Web Server 的 Arduino 程式碼嗎？包含 200 秒的倒數計時器，當按下 GPIO 19 按鈕時重置計時器；網頁上也要能即時顯示溫濕度、計時器和風扇狀態。

### 🤖 AI 助手：
沒問題！這需要用到 `WebServer.h`（ESP32 內建）、`SimpleDHT.h`（DHT11 函式庫）和 `ArduinoJson.h`（方便未來傳輸資料）。

以下是 **方案 A：獨立 Web Server 程式碼 (`smart_fan_standalone.ino`)** 的架構與設計：

*(AI 提供了一份在本地運作的 Arduino 程式碼，其中包含實體按鈕防抖動邏輯、200 秒計時器背景倒數、DHT11 每 2 秒非阻塞讀取、以及內嵌 HTML/CSS 的 `buildHTML()` 函式。網頁使用了現代感的暗色系漸層設計，並使用 JavaScript 定時向 `/api/status` 取得最新 JSON 資料更新網頁，避免網頁整頁重整閃爍。)*

你可以將程式碼寫入 `smart_fan_standalone/smart_fan_standalone.ino`，在 Arduino IDE 中安裝好 `SimpleDHT` 函式庫後進行燒錄。燒錄成功後，開啟序列埠監控器 (Serial Monitor)，它會顯示 ESP32 連接 Wi-Fi 後取得的 IP 位址。

---

## 階段三：Python FastAPI 後端伺服器開發

### 🙋‍♂️ 學生：
獨立版本測試成功了！按鈕按下去計時器真的會重置，網頁也看得到溫度。
現在我想升級成方案 B。我想把控制邏輯移到電腦的伺服器上。由伺服器來決定「溫度大於 26 度開風扇，小於 22 度關風扇」的自動控制，並且希望有更完整的 API，好讓以後可以擴充其他功能（例如記錄餵食歷史、設定警告時間）。
你可以幫我設計 FastAPI 後端程式嗎？

### 🤖 AI 助手：
太棒了！將邏輯移到伺服器（Edge/Cloud）是現代物聯網的核心概念。
我們將在電腦端使用 Python 建立一個後端伺服器，使用 **FastAPI** 來提供 RESTful API，並使用 **Uvicorn** 來運行。

#### 系統 API 設計：
1. `POST /api/sensor`：接收 ESP32 上傳的溫濕度，並在 Response 中回傳風扇應該開啟或關閉。
2. `GET /api/status`：取得當前溫度、濕度、風扇狀態、計時器剩餘時間和餵食警報資訊。
3. `POST /api/button`：接收實體按鈕被按下的事件，重置計時器並更新「上次餵食時間」。
4. `PATCH /api/settings`：可動態更新溫控門檻（`temp_on` / `temp_off`）或切換「自動/手動模式」。

以下是 **`server/server.py`** 的設計，我們使用**遲滯控制 (Hysteresis Control)** 來控制風扇，避免溫度在臨界點（例如 26°C）微幅震盪時，風扇頻繁地啟動與停止（低於 22°C 才關閉，高於 26°C 才開啟）：

*(AI 提供 `server/server.py` 的程式碼，並在其中實作了 `decide_fan` 決策邏輯、計時器剩餘時間計算、餵食時間記錄與警告邏輯。)*

這時，你的 ESP32 需要修改成客戶端版本 (`smart_fan_wifi.ino`)。它不再架設網頁，而是每 2-3 秒向電腦的 FastAPI 伺服器發送 POST 請求上傳資料，並根據伺服器的 JSON 回應來開關風扇。

---

## 階段四：Streamlit 即時儀表板與 UI 設計

### 🙋‍♂️ 學生：
FastAPI 跑起來了，我用 `http://localhost:8080/docs` 測試 API 都正常！
現在我想做一個漂亮的前端網頁。我想要一個看起來很專業、有暗色主題、玻璃面效果、並且能即時顯示溫度折線圖的儀表板。按鈕要有微動畫，風扇在轉動時圖示也要會跟著轉。
你推薦用什麼做？可以直接幫我寫嗎？

### 🤖 AI 助手：
非常推薦使用 **Streamlit**！它是 Python 社群中非常流行用來快速打造精美數據儀表板的工具。結合 **Plotly** 可以畫出高質感的即時動態折線圖。

為了讓網頁「即時更新」且不閃爍，我們可以使用 Streamlit 最新推出的 `@st.fragment` 功能，每秒只局部重新載入感測數據與進度條，而不會整頁重整。

以下是 **`server/streamlit_app.py`** 的精美介面設計：

- **玻璃擬態卡片**：使用自訂 CSS 注入，呈現半透明卡片與彩色漸層發光文字。
- **風扇旋轉動畫**：當伺服器狀態顯示風扇為 `True` 時，透過 CSS Animation 讓 `🌀` 圖示以 0.7 秒一圈的速度無限旋轉。
- **餵食警告橫幅**：若超過自訂時間（預設 4 小時）未按按鈕餵食，會顯示閃爍的紅色警報。
- **動態折線圖**：使用 Plotly 繪製，包含 26°C 的橘色警戒虛線，即時記錄最近的溫濕度變化。

*(AI 提供了 `streamlit_app.py` 的程式碼，包含詳細的自訂 CSS 與 Plotly 設定。)*

你可以透過以下指令啟動儀表板：
```bash
pip install streamlit plotly requests
streamlit run server/streamlit_app.py
```

---

## 階段五：USB 序列埠橋接 (Serial Bridge) 與除錯

### 🙋‍♂️ 學生：
我們學校的 Wi-Fi 有防火牆，ESP32 常常連不上電腦的 FastAPI 伺服器。這樣我的展示就卡住了，有什麼備份方案嗎？

### 🤖 AI 助手：
這在學校或公開場合展示物聯網專案時非常常見！
我們可以用 **USB Serial (序列埠)** 作為備用傳輸介面。當 Wi-Fi 無法連線時，ESP32 可以直接透過 USB 線將感測數據以 JSON 格式印到序列埠，而電腦端的 Python 伺服器則透過一個背景執行緒監聽這個 COM Port，讀取 JSON 後直接呼叫內部的控制邏輯。

#### 這樣做有兩個主要優點：
1. **完全不需要 Wi-Fi**，只要插著 USB 線就能運作。
2. 電腦的 **Streamlit 網頁完全不用改**，因為資料一樣是進到 FastAPI 伺服器。

為了實作這個，我們要在 `server/server.py` 裡面加入背景執行緒，利用 `pyserial` 自動掃描 COM 埠並與 ESP32 進行雙向通訊：

```python
# 伺服器會自動偵測 CP210x / CH340 等 ESP32 晶片常用的 COM 埠
def _find_esp32_port():
    ...
```

#### 🚨 遇過的問題：序列埠被佔用無法燒錄程式
當我們啟用這個功能後，你可能會發現：**每次想用 Arduino IDE 重新燒錄 ESP32 時，都會顯示 `COM port busy` 的錯誤**。這是因為 Python 伺服器一直抓著這個 COM Port 不放。

**解決方案**：
我們在 FastAPI 增加了兩個 API 路由：
- `POST /api/serial/pause`：暫時釋放 COM Port。
- `POST /api/serial/resume`：重新連接 COM Port。

並在 Streamlit 網頁上提供暫停/恢復按鈕，這樣你不需要關閉 Python 伺服器，就能順利燒錄 ESP32 了！

---

## 階段六：Git 管理與專案整理

### 🙋‍♂️ 學生：
我的程式碼都寫好了，測試也完全正常。我想把它們上傳到 GitHub 當作期末成果。
但是我的專案資料夾裡有很多 Python 的編譯暫存檔（如 `__pycache__`）和 Arduino 編譯時產生的 `build/` 資料夾，我不想把它們也 push 上去。我該怎麼設定？

### 🤖 AI 助手：
你需要在專案根目錄建立一個名為 **`.gitignore`** 的文字檔案。這個檔案會告訴 Git 哪些檔案和資料夾應該被忽略，不追蹤也不會被提交。

這是我幫你配置的 `.gitignore` 檔案：

```text
# Python 暫存與虛擬環境
__pycache__/
*.py[cod]
*.pyo
*.pyd
.env
*.env
venv/
.venv/
env/

# OS 產生的垃圾檔案
.DS_Store
Thumbs.db
desktop.ini

# IDE 設定檔
.vscode/
.idea/
*.swp
*.swo

# Arduino 編譯快取
build/
```

建立好 `.gitignore` 後，你可以執行以下 Git 指令來初始化並推送至 GitHub：
1. `git init` （初始化）
2. `git add .` （暫存所有檔案，Git 會自動忽略 `.gitignore` 中設定的項目）
3. `git commit -m "Initial commit"` （建立初始提交）
4. `git remote add origin https://github.com/your-username/AIoT_Final.git` （綁定遠端倉庫）
5. `git push -u origin main` （推送至 GitHub）

恭喜你！這樣你的期末專案就擁有非常完整且專業的開發架構與版控記錄了！
