# AI Usage Dashboard — Waveshare ESP32-C6 e-Paper 1.54

一台桌上型電子紙儀表板,顯示你**真實的 AI 用量還剩多少**。目標硬體為
Waveshare ESP32-C6-ePaper-1.54（SKU 34393,200×200 黑白電子紙）。

- **CLAUDE**：真實訂閱配額(5 小時 + 7 天視窗)。
- **CODEX**：真實用量(5 小時 + 週視窗)。
- **GEMINI**：無官方 usage API,標示為 unavailable,**絕不虛構數字**。

裝置本身**不持有任何 AI 服務的帳號憑證**。用量由一支在你 PC 上執行的
collector 統一提供,ESP32 只透過 LAN 打一個 `GET /v1/dashboard`。

## 畫面

```text
┌────────────────────────┐
│ AI USAGE        .ii [=] │
├────────────────────────┤
│ CLAUDE                 │
│ 5H [######......]  45% │
│ WK [###.........]  25% │
│ RESET 3H 59M           │
├────────────────────────┤
│ CODEX                  │
│ 5H [............]   0% │
│ WK [#####.......]  43% │
│ RESET --               │
├────────────────────────┤
│ BAT 84% WIFI ONLINE    │
│ UPDATED 23:40          │
└────────────────────────┘
```

每個 provider 顯示 5 小時與週兩條進度條、以及最近視窗的 reset 倒數。視窗剛
歸零(rollover)時倒數顯示 `RESET --`。

## 架構

```text
Claude Code (statusLine)  Codex CLI (session files)
        │                        │
        ▼                        ▼
   本機檔案 ~/.ai-usage-dashboard/claude.json、~/.codex/sessions
        │                        │
        └────────┬───────────────┘
                 ▼
     PC collector  usage_collector.py   ← 只讀本機檔,不持有憑證
                 │  GET /v1/dashboard (Bearer token, LAN, plain HTTP)
                 ▼
   ESP32-C6  HttpUsageCollector → DashboardData → e-paper
```

- **Claude** 的訂閱配額百分比只出現在官方 statusLine payload。`claude_statusline.py`
  把它寫進本機快取;`claude_refresh_daemon.py` 每 30 分鐘用一次極短的互動
  session 觸發它更新。
- **Codex** 由官方 CLI 每輪寫進 `rollout-*.jsonl`,collector 直接讀,零成本。
- 視窗過期即歸零(rollover rule),舊資料不會謊報。

資料契約見 [`docs/API_CONTRACT.md`](docs/API_CONTRACT.md);collector 細節見
[`tools/collector/README.md`](tools/collector/README.md)。

## 硬體注意事項(踩過的坑)

1. **內建 LiPo 電池**:拔 USB **不等於**斷電/重置。I2C bus 若卡死,只有實際拔掉
   MX1.25 電池接頭才能解除(要開殼)。
2. **沒有 RST 鍵**,只有 PWR(GPIO2)與 BOOT(GPIO9)。
3. **深睡版本不好重燒**:deep sleep 開啟時 USB 序列埠只在每次醒來幾秒出現,一般
   `upload.ps1` 會撲空。改用 [`tools/flash_deepsleep.ps1`](tools/flash_deepsleep.ps1)
   並按住 BOOT —— 下次定時喚醒時 ROM 讀到 BOOT=低,進入穩定的下載模式。
4. **`configTzTime("CST-8")` 在這塊板子沒生效**,時間會慢 8 小時。因此時鐘與 reset
   倒數改用「絕對 epoch + 固定 +8h 位移」(`AI_DASH_UTC_OFFSET_SECONDS`)計算,不依賴
   裝置時區。

完整電源時序稽核見 [`docs/POWER_SEQUENCE_AUDIT.md`](docs/POWER_SEQUENCE_AUDIT.md),
硬體來源對照見 [`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md)。

## 設定：PC 端(collector)

collector 只讀本機檔,不轉發任何憑證。它在 LAN 上以 plain HTTP 提供服務,用一個
device token 認證。

### 1. 相依

- Python(collector server 只用標準庫;Claude 刷新的 `refresh_claude.py` 需要
  `pywinpty`)。
- 已登入的 Claude Code CLI 與 Codex CLI。

```powershell
C:\path\to\python.exe -m pip install pywinpty
```

### 2. 開機自動常駐(推薦)

兩支 installer 會在登入時以隱藏視窗(pythonw / VBScript window style 0)啟動,幾乎
不耗能(閒置 socket + 每天約十幾秒 CPU),且不會阻止 PC 睡眠:

```powershell
# Claude 配額刷新 daemon(間隔分鐘數,建議 15)
.\tools\collector\setup_schedule.ps1 -Minutes 15
# collector HTTP server(0.0.0.0:8770,附自癒 supervisor)
.\tools\collector\setup_server_autostart.ps1
```

兩者都支援 `-Status` 與 `-Remove`。Startup 資料夾會出現兩個可讀的檔名:
`AI Usage Dashboard - Claude Refresh.vbs`、`AI Usage Dashboard - Collector Server.vbs`。

collector server 由 `collector_server_daemon.py` 這個 supervisor 看守:server 一旦
崩潰或在 PC 睡眠時被收掉,supervisor 會自動重啟它,不必等重新登入。

### 3. 防火牆

裝置要能連到 PC 的 8770 埠(以系統管理員身分執行一次):

```powershell
New-NetFirewallRule -DisplayName "AI Usage Dashboard collector" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8770 -Profile Any -RemoteAddress LocalSubnet
```

### 4. token 與 IP

- device token 由 `run.ps1` 產生,存在 `tools/collector/token.local`(**gitignored**)。
- 把 PC 的 LAN IP(例如 `192.168.0.5`)填進裝置的 `config.local.h`。建議在路由器把
  該 IP 設成 DHCP 固定保留,否則 IP 一變裝置就連不到、需重燒。

前景手動啟動(除錯用):`.\tools\collector\run.ps1`。

## 設定：裝置端(firmware)

### 1. config.local.h

複製 `config.local.h.example` 為 `config.local.h`(gitignored,**不要 commit**),填入:

```cpp
#define AI_DASH_WIFI_SSID     "你的2.4GHz Wi-Fi"   // ESP32-C6 只支援 2.4GHz
#define AI_DASH_WIFI_PASSWORD "你的密碼"
#define AI_DASH_ENABLE_WIFI 1
#define AI_DASH_USE_MOCK_DEVICE_STATUS 0

#define AI_DASH_USE_HTTP_COLLECTOR 1
#define AI_DASH_COLLECTOR_HOST "192.168.0.5"        // 你 PC 的 LAN IP
#define AI_DASH_COLLECTOR_PORT 8770
#define AI_DASH_DEVICE_TOKEN "貼上 token.local 的內容"

#define AI_DASH_USE_DEEP_SLEEP 1                     // 電池使用
#define AI_DASH_REFRESH_INTERVAL_MS (30UL * 60UL * 1000UL)
```

不設 `AI_DASH_USE_HTTP_COLLECTOR` 時,firmware 會退回 `MockUsageCollector`,可離線
驗證版面。

### 2. 燒錄

Arduino IDE 2.x + `arduino-esp32 3.3.11` + `ESP32C6 Dev Module`,Tools 選項見
[`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md)。命令列一次完成 compile+upload:

```powershell
# 一般(裝置未深睡時)
.\tools\upload.ps1

# 深睡版本重燒：執行後按住 BOOT,等定時喚醒進下載模式
.\tools\flash_deepsleep.ps1
```

務必用這兩支腳本,不要用裸 `arduino-cli upload`(會讀到過期 build 快取,曾把
known-bad firmware 燒回板子)。

## 電池運作

`AI_DASH_USE_DEEP_SLEEP 1` 時,裝置每次醒來刷新一次、然後深睡到下個間隔,期間只耗
微安培,電子紙以 0 功耗保留畫面。喚醒的那一刻 **PC 需開著在網路上**才抓得到新資料;
PC 睡/關時保留上一張,PC 回來後下次自動補上。深睡喚醒失敗時按 BOOT 重燒即可,不會
變磚。

## Repo 結構

```text
ai-usage-dashboard/
├─ AI_Usage_Dashboard/
│  ├─ AI_Usage_Dashboard.ino      # setup/refresh、deep sleep、wake 偵測
│  ├─ app_config.h                # 所有可調參數的安全預設
│  ├─ config.local.h.example      # 私人設定範本(實檔 gitignored)
│  ├─ board_config.h              # 產品腳位
│  ├─ board_power.*               # TCA9554 電源軌 + deep-sleep resume()
│  ├─ epaper_display.*            # 200×200 顯示驅動
│  ├─ canvas_1bit.*              # framebuffer 繪圖 + 內建 font
│  ├─ dashboard_model.h           # 正規化資料模型
│  ├─ dashboard_renderer.*        # 兩欄雙進度條版面
│  ├─ usage_collector.h           # collector 介面
│  ├─ mock_usage_collector.*      # 離線 mock 資料源
│  ├─ http_usage_collector.*      # 真實資料源(打 /v1/dashboard)
│  └─ device_status.*             # Wi-Fi、電池、NTP 時間
├─ tools/
│  ├─ compile.ps1                 # compile-only
│  ├─ upload.ps1                  # compile+upload(一般)
│  ├─ flash_deepsleep.ps1         # compile+upload(深睡版本)
│  └─ collector/                  # PC 端 collector 與排程
│     ├─ usage_collector.py       # HTTP server + Codex/Claude 讀取
│     ├─ claude_statusline.py     # statusLine 指令,寫 Claude 快取
│     ├─ refresh_claude.py        # 用 ConPTY 觸發一次刷新
│     ├─ claude_refresh_daemon.py # 定期刷新 Claude 的常駐 supervisor
│     ├─ collector_server_daemon.py # 看守 server,崩潰即自動重啟
│     ├─ run.ps1                  # 前景啟動 server、產 token(除錯用)
│     ├─ setup_schedule.ps1       # 安裝/移除 Claude 刷新 daemon 自動啟動
│     ├─ setup_server_autostart.ps1 # 安裝/移除 server 自癒常駐
│     └─ README.md
├─ docs/
│  ├─ API_CONTRACT.md
│  ├─ HARDWARE_NOTES.md
│  └─ POWER_SEQUENCE_AUDIT.md
└─ .github/workflows/compile.yml  # CI compile,pinned arduino-esp32 3.3.11
```

## 安全與紀律

- ESP32 **不含**任何 provider 的 OAuth token / 帳密;只有一個 collector device token。
- 不可用時明確標 `unavailable` + `error_code`,不虛構數字。
- collector 目前是 **plain HTTP**,token 在 LAN 上明文,僅限自控網路;對外前必須加 TLS。
- 不要 commit `config.local.h`、`tools/collector/token.local` 或任何憑證(皆已 gitignore)。

## Hardware source

腳位、power rail、display controller sequence 與 waveform 以
[Waveshare 官方 ESP32-C6-ePaper-1.54 repo](https://github.com/waveshareteam/ESP32-C6-ePaper-1.54)
commit `c4c47b6a8001f9daa50b38912393c158371e03be` 為基準。授權邊界見
[`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md) 與
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
