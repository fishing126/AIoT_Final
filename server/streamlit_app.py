# =============================================
#    手宮智慧冷氣 - Streamlit 即時監控儀表板
# =============================================
# 啟動方式：
#   streamlit run streamlit_app.py
# =============================================

import streamlit as st
import requests
import plotly.graph_objects as go
from datetime import datetime
from collections import deque

# =============================================
#  頁面設定
# =============================================
st.set_page_config(
    page_title="手宮智慧冷氣",
    page_icon="🌡️",
    layout="wide",
    initial_sidebar_state="collapsed"
)

SERVER_URL = "http://localhost:8080"

# =============================================
#  全域 CSS（只載入一次，不會閃爍）
# =============================================
st.markdown("""
<style>
    /* 背景 */
    [data-testid="stAppViewContainer"] {
        background: linear-gradient(160deg, #0d0d1a 0%, #1a1a2e 60%, #16213e 100%);
        min-height: 100vh;
    }
    [data-testid="stHeader"] { background: transparent; }
    [data-testid="stSidebar"] { background: rgba(0,0,0,0.4); }

    /* 指標卡片 */
    .card {
        background: rgba(255,255,255,0.05);
        border: 1px solid rgba(255,255,255,0.10);
        border-radius: 18px;
        padding: 22px 16px;
        text-align: center;
        backdrop-filter: blur(12px);
        margin: 4px 0;
    }
    .card-val {
        font-size: 2.8em;
        font-weight: 800;
        background: linear-gradient(90deg, #00f5ff, #0080ff);
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
        line-height: 1.1;
    }
    .card-val-warn {
        font-size: 2.8em;
        font-weight: 800;
        background: linear-gradient(90deg, #ff6b6b, #ff8e00);
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
        line-height: 1.1;
    }
    .card-val-timer {
        font-size: 2.8em;
        font-weight: 800;
        font-family: 'Courier New', monospace;
        background: linear-gradient(90deg, #a8ff78, #78ffd6);
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
        line-height: 1.1;
    }
    .card-val-timer-low {
        font-size: 2.8em;
        font-weight: 800;
        font-family: 'Courier New', monospace;
        background: linear-gradient(90deg, #ff4444, #ff8800);
        -webkit-background-clip: text;
        -webkit-text-fill-color: transparent;
        line-height: 1.1;
        animation: pulse 1s ease-in-out infinite;
    }
    .card-lbl {
        font-size: 0.85em;
        color: rgba(255,255,255,0.5);
        margin-top: 8px;
        letter-spacing: 0.06em;
        text-transform: uppercase;
    }

    /* 風扇旋轉 */
    @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
    @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.5; } }
    .fan-spin { display:inline-block; animation: spin 0.7s linear infinite; font-size: 2.4em; }
    .fan-stop { display:inline-block; font-size: 2.4em; opacity: 0.4; }

    /* 風扇狀態徽章 */
    .badge-on  { background: linear-gradient(135deg,#ff416c,#ff4b2b); border-radius:40px; padding:8px 20px; color:#fff; font-weight:700; display:inline-block; box-shadow:0 4px 18px rgba(255,65,108,0.4); margin-top:10px; }
    .badge-off { background: linear-gradient(135deg,#11998e,#38ef7d); border-radius:40px; padding:8px 20px; color:#fff; font-weight:700; display:inline-block; box-shadow:0 4px 18px rgba(56,239,125,0.3); margin-top:10px; }

    /* 餵食警告橫幅 */
    .feed-ok   { background: rgba(56,239,125,0.12); border: 1px solid rgba(56,239,125,0.3); border-radius:12px; padding:14px 20px; color:#38ef7d; font-weight:600; }
    .feed-warn { background: rgba(255,65,108,0.15); border: 1px solid rgba(255,65,108,0.4); border-radius:12px; padding:14px 20px; color:#ff416c; font-weight:700; font-size:1.05em; animation: pulse 1.5s ease-in-out infinite; }
    .feed-unknown { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.12); border-radius:12px; padding:14px 20px; color:rgba(255,255,255,0.5); }

    /* 標題 */
    .title-main { font-size:2.3em; font-weight:900;
        background:linear-gradient(90deg,#00f5ff,#0080ff,#a855f7);
        -webkit-background-clip:text; -webkit-text-fill-color:transparent; }
    .title-sub  { color:rgba(255,255,255,0.4); font-size:0.88em; margin-top:2px; }

    /* 控制按鈕美化 */
    [data-testid="stButton"] button {
        border-radius: 10px;
        font-weight: 600;
        border: 1px solid rgba(255,255,255,0.15);
        transition: all 0.2s;
    }

    /* 隱藏 Streamlit 預設元素 */
    #MainMenu { visibility: hidden; }
    footer { visibility: hidden; }
    [data-testid="stToolbar"] { visibility: hidden; }
</style>
""", unsafe_allow_html=True)

# =============================================
#  Session State（歷史資料）
# =============================================
if "temp_h" not in st.session_state:
    st.session_state.temp_h = deque(maxlen=60)
if "humi_h" not in st.session_state:
    st.session_state.humi_h = deque(maxlen=60)
if "time_h" not in st.session_state:
    st.session_state.time_h = deque(maxlen=60)

# =============================================
#  靜態標題（只渲染一次，不閃爍）
# =============================================
st.markdown('<div class="title-main">🌡️ 手宮智慧冷氣</div>', unsafe_allow_html=True)
st.markdown('<div class="title-sub">寵物箱環境即時監控</div>', unsafe_allow_html=True)
st.markdown("<br>", unsafe_allow_html=True)

# =============================================
#  即時更新區塊（@st.fragment 避免全頁閃爍）
# =============================================
@st.fragment(run_every=1)
def live_dashboard():
    # ----- 取得資料 -----
    try:
        resp = requests.get(f"{SERVER_URL}/api/status", timeout=3)
        d = resp.json()
        latest  = d.get("latest", {})
        fan_on  = d.get("fan", False)
        timer   = d.get("timer", {"remaining": 0, "duration": 200})
        feeding = d.get("feeding", {})
        settings = d.get("settings", {})
        remaining = timer.get("remaining", 0)
        duration  = timer.get("duration", 200)
        connected = True

        temp_v = latest.get("temperature")
        humi_v = latest.get("humidity")
        if temp_v is not None:
            st.session_state.temp_h.append(float(temp_v))
            st.session_state.humi_h.append(float(humi_v))
            st.session_state.time_h.append(datetime.now().strftime("%H:%M:%S"))

    except Exception:
        connected = False

    if not connected:
        st.error("❌ 無法連線到伺服器 `localhost:8080`，請確認已執行 `python server.py`")
        return

    # ─── 餵食警告橫幅 ───────────────────────────────
    warning_active = feeding.get("warning_active", False)
    feed_display   = feeding.get("display", "尚未記錄")
    last_feed      = feeding.get("last_feed_time")
    w_hours        = feeding.get("warning_hours", 4)

    if warning_active:
        st.markdown(
            f'<div class="feed-warn">⚠️ 已超過 {w_hours} 小時未餵食！上次餵食：{feed_display}</div>',
            unsafe_allow_html=True
        )
    elif last_feed is None:
        st.markdown('<div class="feed-unknown">🔔 尚未記錄餵食時間，請按實體按鈕或點擊下方「已餵食」</div>', unsafe_allow_html=True)
    else:
        st.markdown(
            f'<div class="feed-ok">✅ 上次餵食：{feed_display}（{w_hours} 小時內無警告）</div>',
            unsafe_allow_html=True
        )

    st.markdown("<br>", unsafe_allow_html=True)

    # ─── 主要指標卡片 4 欄 ──────────────────────────
    temp_v = latest.get("temperature", "--")
    humi_v = latest.get("humidity", "--")
    mins, secs = remaining // 60, remaining % 60

    temp_cls  = "card-val-warn" if (isinstance(temp_v, (int, float)) and temp_v >= 26) else "card-val"
    timer_cls = "card-val-timer-low" if remaining < 30 else "card-val-timer"
    fan_icon  = '<span class="fan-spin">🌀</span>' if fan_on else '<span class="fan-stop">🌀</span>'
    fan_badge = '<div class="badge-on">轉動中</div>' if fan_on else '<div class="badge-off">停止</div>'

    c1, c2, c3, c4 = st.columns(4)
    c1.markdown(f'<div class="card"><div class="{temp_cls}">{temp_v}°</div><div class="card-lbl">🌡️ 溫度 (°C)</div></div>', unsafe_allow_html=True)
    c2.markdown(f'<div class="card"><div class="card-val">{humi_v}%</div><div class="card-lbl">💧 濕度</div></div>', unsafe_allow_html=True)
    c3.markdown(f'<div class="card"><div class="{timer_cls}">{mins:02d}:{secs:02d}</div><div class="card-lbl">⏱️ 餵食計時</div></div>', unsafe_allow_html=True)
    c4.markdown(f'<div class="card">{fan_icon}{fan_badge}<div class="card-lbl">🌀 風扇狀態</div></div>', unsafe_allow_html=True)

    st.markdown("<br>", unsafe_allow_html=True)

    # ─── 控制面板（餵食 + 風扇）───────────────────────
    left_col, right_col = st.columns([1, 1])

    with left_col:
        st.markdown("#### 🐾 餵食記錄")
        if st.button("✅ 已餵食！記錄時間", use_container_width=True, type="primary"):
            try:
                requests.post(f"{SERVER_URL}/api/feed", timeout=3)
                st.success("✅ 已記錄！計時器重置")
            except Exception:
                st.error("伺服器無回應")

        # 警告時間設定
        with st.expander("⚙️ 調整警告時間"):
            new_hours = st.slider("超過幾小時未餵食就警告", 1, 24, int(w_hours))
            if st.button("套用", key="apply_warning"):
                try:
                    requests.patch(f"{SERVER_URL}/api/feed/warning?hours={new_hours}", timeout=3)
                    st.success(f"已設定為 {new_hours} 小時")
                except Exception:
                    st.error("更新失敗")

    with right_col:
        st.markdown("#### 🌀 風扇手動控制")
        mode_str = "🤖 自動" if settings.get("mode") == "auto" else "🖐️ 手動"
        st.caption(f"目前模式：{mode_str}　開啟門檻：{settings.get('temp_on', 26)}°C")

        btn1, btn2, btn3 = st.columns(3)
        with btn1:
            if st.button("🔴 開啟", use_container_width=True):
                try:
                    requests.post(f"{SERVER_URL}/api/fan/on", timeout=3)
                except Exception:
                    pass
        with btn2:
            if st.button("🟢 關閉", use_container_width=True):
                try:
                    requests.post(f"{SERVER_URL}/api/fan/off", timeout=3)
                except Exception:
                    pass
        with btn3:
            if st.button("🤖 自動", use_container_width=True):
                try:
                    requests.post(f"{SERVER_URL}/api/fan/auto", timeout=3)
                except Exception:
                    pass

    st.divider()

    # ─── 計時器進度條 ────────────────────────────────
    st.markdown("#### ⏱️ 餵食計時器")
    pct = remaining / duration if duration > 0 else 0
    label_txt = f"{'⚠️ 即將到期！' if remaining < 30 else ''}剩餘 {remaining} 秒 / {duration} 秒"
    st.progress(pct, text=label_txt)

    # 計時器手動重置
    if st.button("🔄 手動重置計時器"):
        try:
            requests.post(f"{SERVER_URL}/api/timer/reset", timeout=3)
        except Exception:
            pass

    st.divider()

    # ─── 溫濕度歷史圖表 ──────────────────────────────
    if len(st.session_state.temp_h) > 1:
        st.markdown("#### 📈 溫濕度歷史紀錄")

        fig = go.Figure()
        times = list(st.session_state.time_h)

        fig.add_trace(go.Scatter(
            x=times, y=list(st.session_state.temp_h),
            name="溫度 (°C)", mode="lines",
            line=dict(color="#ff6b6b", width=2.5),
            fill="tozeroy", fillcolor="rgba(255,107,107,0.07)"
        ))
        fig.add_trace(go.Scatter(
            x=times, y=list(st.session_state.humi_h),
            name="濕度 (%)", mode="lines",
            line=dict(color="#4ecdc4", width=2.5),
            fill="tozeroy", fillcolor="rgba(78,205,196,0.06)"
        ))
        # 26°C 警戒線
        fig.add_hline(y=26, line_dash="dash",
                      line_color="rgba(255,165,0,0.5)",
                      annotation_text="開啟門檻 26°C",
                      annotation_font_color="rgba(255,165,0,0.8)")

        fig.update_layout(
            paper_bgcolor="rgba(0,0,0,0)",
            plot_bgcolor="rgba(0,0,0,0)",
            font=dict(color="rgba(255,255,255,0.7)", size=12),
            legend=dict(bgcolor="rgba(255,255,255,0.04)", bordercolor="rgba(255,255,255,0.1)", borderwidth=1),
            height=250,
            margin=dict(l=0, r=0, t=10, b=0),
            xaxis=dict(gridcolor="rgba(255,255,255,0.05)"),
            yaxis=dict(gridcolor="rgba(255,255,255,0.05)")
        )
        st.plotly_chart(fig, width="stretch")
    else:
        st.info("等待感測資料…（至少 2 筆才顯示圖表）")

    # 最後更新時間（小字，不閃）
    st.markdown(
        f'<p style="color:rgba(255,255,255,0.25);font-size:0.78em;text-align:right">最後更新：{datetime.now().strftime("%H:%M:%S")}</p>',
        unsafe_allow_html=True
    )


live_dashboard()
