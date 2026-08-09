/**
 * @file main.cpp
 * @brief ESP8266 (Wemos D1 mini) 滤镜轮固件入口
 *
 * 在 Arduino Nano 固件基础上增加:
 *   - Wi-Fi 连接管理 (STA 模式, 初次配置 AP 模式)
 *   - Web 配置页面
 *   - ASCOM Alpaca FilterWheel HTTP API
 *   - mDNS 设备发现
 *
 * 编译: PlatformIO (env:esp8266) 或 Arduino IDE
 * 依赖: ESP8266WiFi, ESP8266WebServer, ESP8266mDNS, DNSServer
 */

#include <Arduino.h>
#include "hal.h"
#include "motion.h"
#include "wheel.h"
#include "transport.h"
#include "eeprom_config.h"

// ESP8266 特定头文件
#if defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
// #include <EEPROM.h>  已在 hal.cpp 中包含
#endif

/* =========================================================================
 * 网络配置
 * ========================================================================= */
#define WIFI_AP_SSID        "EFilterWheel-Setup"
#define WIFI_AP_PASSWORD    "filterwheel"
#define WIFI_CONNECT_TIMEOUT 15000   ///< Wi-Fi 连接超时 (ms)
#define MDNS_NAME           "efilterwheel"

// EEPROM 网络配置地址 (在 EEPROM_CONFIG_SIZE 之后)
#define EEPROM_WIFI_MAGIC   (EEPROM_CONFIG_SIZE + 0)
#define EEPROM_WIFI_SSID    (EEPROM_CONFIG_SIZE + 2)
#define EEPROM_WIFI_PASS    (EEPROM_CONFIG_SIZE + 34)
#define EEPROM_WIFI_MAGIC_VAL 0xBEEF

#if defined(PLATFORM_ESP8266)
static ESP8266WebServer server(80);
static DNSServer dnsServer;
static bool wifi_connected = false;
static bool ap_mode = false;
#endif

/* =========================================================================
 * 全局状态
 * ========================================================================= */
static uint32_t last_tick_ms = 0;

/* =========================================================================
 * Wi-Fi 管理 (ESP8266)
 * ========================================================================= */
#if defined(PLATFORM_ESP8266)

static void wifi_load_credentials(char* ssid, size_t ssid_len,
                                   char* pass, size_t pass_len)
{
    uint16_t magic = hal_eeprom_read_word(EEPROM_WIFI_MAGIC);
    if (magic != EEPROM_WIFI_MAGIC_VAL) {
        ssid[0] = '\0';
        pass[0] = '\0';
        return;
    }

    for (size_t i = 0; i < ssid_len - 1; i++) {
        ssid[i] = (char)hal_eeprom_read_byte(EEPROM_WIFI_SSID + i);
        if (ssid[i] == 0) break;
    }
    ssid[ssid_len - 1] = '\0';

    for (size_t i = 0; i < pass_len - 1; i++) {
        pass[i] = (char)hal_eeprom_read_byte(EEPROM_WIFI_PASS + i);
        if (pass[i] == 0) break;
    }
    pass[pass_len - 1] = '\0';
}

static void wifi_save_credentials(const char* ssid, const char* pass)
{
    hal_eeprom_write_word(EEPROM_WIFI_MAGIC, EEPROM_WIFI_MAGIC_VAL);

    size_t i;
    for (i = 0; i < 32 && ssid[i]; i++) {
        hal_eeprom_write_byte(EEPROM_WIFI_SSID + i, ssid[i]);
    }
    hal_eeprom_write_byte(EEPROM_WIFI_SSID + i, 0);

    for (i = 0; i < 32 && pass[i]; i++) {
        hal_eeprom_write_byte(EEPROM_WIFI_PASS + i, pass[i]);
    }
    hal_eeprom_write_byte(EEPROM_WIFI_PASS + i, 0);

    hal_eeprom_commit();
}

static void wifi_connect(void)
{
    char ssid[33] = {0};
    char pass[33] = {0};
    wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    if (ssid[0] == '\0') {
        // 无保存的凭证，启动 AP 模式
        hal_serial_println("No WiFi credentials saved. Starting AP mode...");
        ap_mode = true;

        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        // 启动 DNS 劫持 (Captive Portal)
        dnsServer.start(53, "*", WiFi.softAPIP());

        hal_serial_print("AP IP: ");
        hal_serial_println(WiFi.softAPIP().toString().c_str());
        return;
    }

    // STA 模式连接
    ap_mode = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    hal_serial_print("Connecting to WiFi: ");
    hal_serial_println(ssid);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT) {
            hal_serial_println("WiFi connection timeout. Starting AP mode...");
            ap_mode = true;
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
            dnsServer.start(53, "*", WiFi.softAPIP());
            return;
        }
        hal_led_toggle();
        delay(500);
        hal_serial_print(".");
    }

    wifi_connected = true;
    hal_led_set(true);  // 常亮表示已连接
    hal_serial_println("");
    hal_serial_print("WiFi connected. IP: ");
    hal_serial_println(WiFi.localIP().toString().c_str());

    // mDNS
    if (MDNS.begin(MDNS_NAME)) {
        hal_serial_print("mDNS: http://");
        hal_serial_print(MDNS_NAME);
        hal_serial_println(".local");
        MDNS.addService("alpaca", "tcp", 80);
    }
}

/* =========================================================================
 * Web 服务器路由 (ESP8266)
 * ========================================================================= */

static void web_handle_root(void)
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EFilterWheel Control</title>
<style>
  body { font-family: Arial, sans-serif; max-width: 600px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #e0e0e0; }
  h1 { color: #00d4ff; }
  .card { background: #16213e; border-radius: 8px; padding: 15px; margin: 10px 0; }
  .btn { padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; font-size: 14px; }
  .btn-home { background: #00d4ff; color: #000; }
  .btn-goto { background: #4caf50; color: #fff; }
  .btn-stop { background: #f44336; color: #fff; }
  .btn-save { background: #ff9800; color: #fff; }
  .btn:disabled { opacity: 0.5; cursor: not-allowed; }
  .status { font-size: 18px; padding: 10px; border-radius: 5px; }
  .ready { background: #1b5e20; }
  .moving { background: #e65100; }
  .error { background: #b71c1c; }
  .homing { background: #0d47a1; }
  input[type=number] { width: 60px; padding: 5px; font-size: 14px; }
  label { display: inline-block; width: 100px; }
</style>
</head>
<body>
<h1>🔭 EFilterWheel</h1>
<div class="card">
  <div id="status" class="status">Loading...</div>
  <p>Slot: <span id="pos">--</span></p>
  <p id="errorMsg"></p>
</div>
<div class="card">
  <h3>Control</h3>
  <button class="btn btn-home" onclick="sendCmd('HOME')">🏠 Home</button>
  <button class="btn btn-stop" onclick="sendCmd('STOP')">⏹ Stop</button>
  <br><br>
  <label>Go to slot:</label>
  <input type="number" id="slotNum" min="0" max="6" value="0">
  <button class="btn btn-goto" onclick="sendCmd('GOTO '+document.getElementById('slotNum').value)">➡ Go</button>
</div>
<div class="card">
  <h3>Settings</h3>
  <label>Slots:</label>
  <select id="slotsSelect" onchange="sendCmd('SLOTS '+this.value)">
    <option value="5">5</option>
    <option value="7" selected>7</option>
  </select>
  <br><br>
  <button class="btn btn-save" onclick="sendCmd('SAVE')">💾 Save Config</button>
  <button class="btn btn-save" onclick="sendCmd('RESET')">🔄 Reboot</button>
</div>
<script>
function sendCmd(cmd) {
  fetch('/api/cmd?c=' + encodeURIComponent(cmd))
    .then(r => r.text())
    .then(t => { console.log(t); refreshState(); });
}
function refreshState() {
  fetch('/api/state')
    .then(r => r.json())
    .then(s => {
      document.getElementById('status').textContent = s.State;
      document.getElementById('status').className = 'status ' + s.State.toLowerCase();
      document.getElementById('pos').textContent = s.Position;
      document.getElementById('errorMsg').textContent = s.ErrorCode ? 'Error: ' + s.ErrorMessage : '';
    });
}
setInterval(refreshState, 1000);
refreshState();
</script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html; charset=utf-8", html);
}

static void web_handle_api_cmd(void)
{
    String cmd = server.arg("c");
    char buf[512];
    transport_set_output(TRANSPORT_OUTPUT_BUFFER, buf, sizeof(buf));
    transport_execute(cmd.c_str());
    server.send(200, "text/plain", buf);
}

static void web_handle_api_state(void)
{
    char buf[256];
    transport_get_state_json(buf, sizeof(buf));
    server.send(200, "application/json", buf);
}

static void web_handle_wifi_setup(void)
{
    if (server.method() == HTTP_POST) {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        if (ssid.length() > 0 && ssid.length() <= 32) {
            wifi_save_credentials(ssid.c_str(), pass.c_str());
            server.send(200, "text/html", "<h2>Saved! Rebooting...</h2><script>setTimeout(function(){location.href='/';},3000);</script>");
            delay(1000);
            hal_system_reset();
            return;
        }
    }

    String html = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>WiFi Setup</title>
<style>body{font-family:Arial;max-width:400px;margin:20px auto;background:#1a1a2e;color:#e0e0e0;}
.card{background:#16213e;border-radius:8px;padding:15px;}
input{width:100%;padding:8px;margin:5px 0;border-radius:4px;border:1px solid #333;background:#0f3460;color:#fff;}
.btn{background:#00d4ff;color:#000;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;}</style>
</head><body>
<h2>WiFi Setup</h2>
<div class="card">
<form method="POST">
<label>SSID:</label><input name="ssid" required>
<label>Password:</label><input name="pass" type="password">
<button class="btn" type="submit">Save & Reboot</button>
</form>
</div>
</body></html>
)rawliteral";
    server.send(200, "text/html; charset=utf-8", html);
}

/* =========================================================================
 * Alpaca API 路由
 * ========================================================================= */

static void alpaca_handle_setup(void)
{
    server.send(200, "application/json",
        "{"
        "\"ServerName\":\"EFilterWheel\","
        "\"Manufacturer\":\"DIY\","
        "\"ServerVersion\":\"" FW_VERSION_STRING "\","
        "\"InterfaceVersion\":2"
        "}");
}

static void alpaca_handle_management(void)
{
    // Alpaca management API
    // GET /management/apiversions
    // GET /management/v1/description
    // GET /management/v1/configureddevices

    String uri = server.uri();
    if (uri.indexOf("apiversions") > 0) {
        server.send(200, "application/json", "[1,2]");
    } else if (uri.indexOf("description") > 0) {
        char buf[512];
        transport_get_device_info_json(buf, sizeof(buf));
        server.send(200, "application/json", buf);
    } else if (uri.indexOf("configureddevices") > 0) {
        server.send(200, "application/json",
            "[{\"DeviceName\":\"filterwheel\","
            "\"DeviceType\":\"FilterWheel\","
            "\"DeviceNumber\":0,"
            "\"UniqueID\":\"" DEVICE_NAME "\"}]");
    } else {
        server.send(404, "text/plain", "Not Found");
    }
}

static void alpaca_handle_filterwheel(void)
{
    // Alpaca FilterWheel API
    // GET  /api/v1/filterwheel/0/...
    // PUT  /api/v1/filterwheel/0/...

    String uri = server.uri();
    String method_str = (server.method() == HTTP_GET) ? "GET" : "PUT";

    if (uri.endsWith("/connected")) {
        if (method_str == "GET") {
            server.send(200, "application/json", "true");
        } else {
            server.send(200, "application/json", "true");
        }
    }
    else if (uri.endsWith("/name")) {
        char buf[64];
        snprintf(buf, sizeof(buf), "\"%s\"", DEVICE_NAME);
        server.send(200, "application/json", buf);
    }
    else if (uri.endsWith("/interfaceversion")) {
        server.send(200, "application/json", "2");
    }
    else if (uri.endsWith("/driverinfo")) {
        server.send(200, "application/json",
            "{\"Name\":\"EFilterWheel\","
            "\"Description\":\"DIY 3D-Printed Filter Wheel\","
            "\"DriverVersion\":\"" FW_VERSION_STRING "\","
            "\"InterfaceVersion\":2}");
    }
    else if (uri.endsWith("/position")) {
        if (method_str == "GET") {
            int8_t pos = wheel_get_current_slot();
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", (int)pos);
            server.send(200, "application/json", buf);
        } else {
            // PUT: 设置位置
            String body = server.arg("plain");
            int slot = body.toInt();
            if (wheel_goto_slot((uint8_t)slot)) {
                server.send(200, "application/json", "{}");
            } else {
                server.send(400, "text/plain", "Invalid slot");
            }
        }
    }
    else if (uri.endsWith("/names")) {
        const wheel_config_t* cfg = wheel_get_config();
        String json = "[";
        for (uint8_t i = 0; i < cfg->num_slots; i++) {
            if (i > 0) json += ",";
            json += "\"";
            json += cfg->slot_names[i];
            json += "\"";
        }
        json += "]";
        server.send(200, "application/json", json);
    }
    else if (uri.endsWith("/focusoffsets")) {
        const wheel_config_t* cfg = wheel_get_config();
        String json = "[";
        for (uint8_t i = 0; i < cfg->num_slots; i++) {
            if (i > 0) json += ",";
            json += String(cfg->focus_offsets[i]);
        }
        json += "]";
        server.send(200, "application/json", json);
    }
    else if (uri.endsWith("/supportedactions")) {
        server.send(200, "application/json", "[]");
    }
    else {
        server.send(404, "text/plain", "Not Found");
    }
}

static void web_setup_routes(void)
{
    server.on("/", web_handle_root);
    server.on("/api/cmd", web_handle_api_cmd);
    server.on("/api/state", web_handle_api_state);
    server.on("/wifi", web_handle_wifi_setup);

    // Alpaca management
    server.on("/management/apiversions", alpaca_handle_management);
    server.on("/management/v1/description", alpaca_handle_management);
    server.on("/management/v1/configureddevices", alpaca_handle_management);

    // Alpaca FilterWheel API
    // 使用通配符处理动态路径
    server.on("/api/v1/filterwheel/0/connected", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/name", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/interfaceversion", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/driverinfo", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/position", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/names", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/focusoffsets", alpaca_handle_filterwheel);
    server.on("/api/v1/filterwheel/0/supportedactions", alpaca_handle_filterwheel);
}

#endif // PLATFORM_ESP8266

/* =========================================================================
 * 初始化
 * ========================================================================= */
void setup(void)
{
    // 1. 硬件初始化
    hal_init();

    // 2. 延时
    hal_delay_ms(500);

    // 3. 启动提示
    hal_buzzer_play_startup();

    // 4. 子系统初始化
    motion_init();
    transport_init();
    wheel_init();

    // 5. 看门狗 (ESP8266 由 SDK 管理，Nano 使用 WDT)
    hal_wdt_enable(8);

    // 6. 启动信息
    hal_serial_println("");
    hal_serial_println("==============================");
    hal_serial_print("EFilterWheel-ESP8266 FW ");
    hal_serial_println(FW_VERSION_STRING);
    hal_serial_println("==============================");

#if defined(PLATFORM_ESP8266)
    // 7. Wi-Fi 初始化
    wifi_connect();

    // 8. Web 服务器路由
    web_setup_routes();
    server.begin();
    hal_serial_println("HTTP server started on port 80");
#endif

    // 9. 自动回零
    hal_serial_println("Starting auto-homing...");
    wheel_start_homing();
}

/* =========================================================================
 * 主循环
 * ========================================================================= */
void loop(void)
{
    uint32_t now = hal_millis();

    // 1. 定时 tick
    if ((int32_t)(now - last_tick_ms) >= 1) {
        last_tick_ms = now;
        hal_tick_process();
    }

    // 2. 状态机
    wheel_tick();

    // 3. 运动超时
    motion_tick();

    // 4. 串口命令
    transport_tick();

    // 5. 空闲释放电机
    motion_release_if_idle();

#if defined(PLATFORM_ESP8266)
    // 6. Web 服务器处理
    server.handleClient();

    // 7. DNS 劫持 (AP 模式)
    if (ap_mode) {
        dnsServer.processNextRequest();
    }

    // 8. mDNS 更新
    if (wifi_connected) {
        MDNS.update();
    }

    // 9. 检查 Wi-Fi 断线重连
    if (!ap_mode && WiFi.status() != WL_CONNECTED) {
        wifi_connected = false;
        hal_led_blink_pattern(0, 500, 500);
        // 尝试重连
        WiFi.reconnect();
    }
#endif

    // 10. 平台 yield（ESP8266 喂狗等）
    hal_platform_yield();
}
