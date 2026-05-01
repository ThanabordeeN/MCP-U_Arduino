# MCP-U

**MCP/U — The Unified Interface for AI-Ready Microcontrollers**

[![License: LGPL v3](https://img.shields.io/badge/License-LGPL%20v3-blue.svg)](LICENSE)
[![Arduino Library](https://img.shields.io/badge/Arduino-Library%20Manager-blue)](https://www.arduino.cc/reference/en/libraries/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange)](https://platformio.org/lib/show/MCP-U)

> Transform any Arduino-compatible MCU into an AI-controllable device via the [Model Context Protocol](https://modelcontextprotocol.io).

*By [2edge.co](https://2edge.co)*

---

## What is MCP-U?

MCP-U implements **JSON-RPC 2.0 over any Arduino `Stream`** (Serial, WiFiClient, BluetoothSerial, etc.), exposing your MCU's GPIO, PWM, ADC, and I2C peripherals as MCP tools that any AI agent can discover and call at runtime — zero hardcoded tool names required.

```
┌──────────────────┐         ┌──────────────────────┐          ┌───────────────┐
│  Claude / Gemini │◄──MCP──►│  mcpu-client (npm)   │◄─Serial─►|   Your MCU    │
│  or any LLM      │  stdio  │  Dynamic tool regist.│  /TCP    │  MCP-U lib    │
└──────────────────┘         └──────────────────────┘          └───────────────┘
```

1. The client connects to the MCU over Serial or TCP
2. It calls `list_tools` — the MCU responds with its full tool + pin registry as JSON Schema
3. The client dynamically registers one MCP tool per MCU tool
4. Your AI agent can now call any MCU capability by name

---

## Installation

### Arduino IDE (Library Manager)

1. Open **Sketch → Include Library → Manage Libraries…**
2. Search for **MCP-U**
3. Click **Install**

### PlatformIO

```ini
; platformio.ini
lib_deps =
    ThanabordeeN/MCP-U_Arduino
    bblanchon/ArduinoJson @ ^7
```

### Manual

Download this repo as a ZIP and use **Sketch → Include Library → Add .ZIP Library…**

---

## Dependencies

| Library | Version | Required |
|---------|---------|----------|
| [ArduinoJson](https://arduinojson.org) | `^7` | Yes |

---

## Quick Start

### Serial (USB)

```cpp
#include <MCP-U.h>

McpDevice mcp("my-device", "1.0.0");

void setup() {
  mcp.add_pin(2,  "led",    MCP_DIGITAL_OUTPUT, "Built-in LED");
  mcp.add_pin(34, "sensor", MCP_ADC_INPUT,      "Light sensor");
  mcp.add_pin(19, "motor",  MCP_PWM_OUTPUT,     "Motor speed (0-255)");

  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
```

Connect with:
```bash
claude mcp add mcpu -e SERIAL_PORT=/dev/ttyACM0 -- npx mcpu-client
```

### WiFi TCP (ESP32 / ESP8266)

```cpp
#include <WiFi.h>
#include <MCP-U.h>

McpDevice  mcp("esp32-wifi", "1.0.0");
WiFiServer server(3000);
WiFiClient client;

void setup() {
  mcp.add_pin(2,  "led",    MCP_DIGITAL_OUTPUT, "Built-in LED");
  mcp.add_pin(34, "sensor", MCP_ADC_INPUT,      "Light sensor");

  WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(500);

  server.begin();
}

void loop() {
  if (!client || !client.connected()) {
    WiFiClient incoming = server.accept();
    if (incoming) {
      client = incoming;
      mcp.begin(client);   // swap stream to the new TCP connection
    }
  }
  mcp.loop();
}
```

Connect with (replace IP with what your ESP32 prints to Serial):
```bash
DEVICES=mydevice:192.168.1.x:3000:tcp npx mcpu-client
```

> See **examples/BasicSerial** and **examples/WiFiTCP** for full working sketches.

---

## API Reference

### `McpDevice(name, version)`

Constructor. Call once globally.

```cpp
McpDevice mcp("robot-arm", "2.1.0");
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const char*` | Device name sent during discovery |
| `version` | `const char*` | Firmware version string |

---

### `add_pin(pin, name, type, description)`

Register a hardware pin. Must be called before `begin()`.

```cpp
mcp.add_pin(2,  "led",     MCP_DIGITAL_OUTPUT, "Status LED");
mcp.add_pin(4,  "button",  MCP_DIGITAL_INPUT,  "User button");
mcp.add_pin(19, "fan",     MCP_PWM_OUTPUT,     "Cooling fan 0-255");
mcp.add_pin(34, "temp",    MCP_ADC_INPUT,      "Thermistor raw ADC");
```

| Pin Type | Arduino API | Use Cases |
|----------|-------------|-----------|
| `MCP_DIGITAL_OUTPUT` | `digitalWrite` | LED, relay, buzzer |
| `MCP_DIGITAL_INPUT` | `digitalRead` | Button, reed switch, PIR |
| `MCP_PWM_OUTPUT` | `analogWrite` | Motor, servo, LED dimmer |
| `MCP_ADC_INPUT` | `analogRead` | Sensor, potentiometer, thermistor |

---

### `add_tool(name, description, handler)`

Register a custom RPC tool. Must be called before `begin()`.

```cpp
void beep_handler(int id, JsonObject params) {
  int duration_ms = params["duration"].as<int>();
  tone(BUZZER_PIN, 1000, duration_ms);

  JsonDocument res;
  res["result"]["ok"] = true;
  mcp.send_result(id, res);
}

// In setup():
mcp.add_tool("beep", "Play a beep tone", beep_handler);
```

Handler signature: `void handler(int id, JsonObject params)`

- `id` — JSON-RPC request ID, pass to `send_result()` or `send_error()`
- `params` — `JsonObject` containing the call arguments

---

### `begin(stream, baud)`

Start the MCP device on any Arduino `Stream`.

```cpp
mcp.begin(Serial, 115200);       // UART over USB
mcp.begin(Serial2, 9600);        // Hardware UART
mcp.begin(wifi_client);          // WiFiClient (baud ignored)
mcp.begin(bt);                   // BluetoothSerial (baud ignored)
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `stream` | `Stream&` | Any Arduino Stream |
| `baud` | `unsigned long` | Baud rate — only used for `HardwareSerial`, ignored otherwise |

---

### `begin_i2c(sda, scl, freq)`

Enable built-in I2C tools. Call before `begin()`.

```cpp
mcp.begin_i2c();              // Use board default SDA/SCL pins
mcp.begin_i2c(21, 22);       // ESP32 custom pins
mcp.begin_i2c(21, 22, 400000); // 400 kHz fast mode
```

Enables three additional built-in tools: `i2c_scan`, `i2c_write_reg`, `i2c_read_reg`.

---

### `loop()`

Call from Arduino `loop()`. Reads and dispatches incoming JSON-RPC requests.

```cpp
void loop() {
  mcp.loop();
}
```

---

### `send_result(id, doc)`

Send a JSON-RPC 2.0 success response. Set `doc["result"]` before calling.

```cpp
void my_handler(int id, JsonObject params) {
  JsonDocument res;
  res["result"]["value"] = 42;
  res["result"]["unit"]  = "celsius";
  mcp.send_result(id, res);
}
```

---

### `send_error(id, code, message)`

Send a JSON-RPC 2.0 error response.

```cpp
mcp.send_error(id, -32602, "Required: pin (integer)");
```

Standard JSON-RPC error codes:

| Code | Meaning |
|------|---------|
| `-32700` | Parse error |
| `-32600` | Invalid request |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32603` | Internal error |

---

## Built-in Tools

These tools are always available without any registration:

| Tool | Parameters | Description |
|------|------------|-------------|
| `list_tools` | — | Discovery: full tool + pin registry with JSON Schema |
| `get_info` | — | Device name, version, platform, pin count |
| `gpio_write` | `pin` (int), `value` (bool) | Set digital output HIGH/LOW |
| `gpio_read` | `pin` (int) | Read digital level → `true`/`false` |
| `pwm_write` | `pin` (int), `duty` (0–255) | PWM output via `analogWrite` |
| `adc_read` | `pin` (int) | Read ADC value (0–4095) + voltage (0–3.3V) |
| `i2c_scan` ¹ | — | Scan I2C bus, return all responding addresses |
| `i2c_write_reg` ¹ | `address`, `reg`, `value` | Write byte to I2C register |
| `i2c_read_reg` ¹ | `address`, `reg`, `length` | Read bytes from I2C register |

¹ Only available after calling `begin_i2c()`.

---

## Compile-Time Configuration

Override defaults with build flags:

```ini
; platformio.ini
build_flags =
    -D MCP_MAX_PINS=32
    -D MCP_MAX_TOOLS=48
    -D MCP_SERIAL_BUFFER=1024
```

| Macro | Default | Description |
|-------|---------|-------------|
| `MCP_MAX_PINS` | `16` | Maximum registered pins |
| `MCP_MAX_TOOLS` | `24` | Maximum registered custom tools |
| `MCP_SERIAL_BUFFER` | `512` | Serial input buffer size |

---

## Connecting an AI Agent

The companion [`mcpu-client`](https://www.npmjs.com/package/mcpu-client) npm package bridges your MCU to any MCP-compatible AI agent.

**Claude Code:**
```bash
claude mcp add mcpu -e SERIAL_PORT=/dev/ttyACM0 -- npx mcpu-client
```

**Claude Desktop** (`claude_desktop_config.json`):
```json
{
  "mcpServers": {
    "mcpu": {
      "command": "npx",
      "args": ["mcpu-client"],
      "env": { "SERIAL_PORT": "/dev/ttyACM0" }
    }
  }
}
```

**Windows:** Replace `/dev/ttyACM0` with `COM3`, `COM4`, etc.

---

## Protocol

MCP-U speaks **JSON-RPC 2.0** over any `Stream`. Each message is a single line of JSON terminated with `\n`.

**Request (host → MCU):**
```json
{"jsonrpc":"2.0","id":1,"method":"gpio_write","params":{"pin":2,"value":true}}
```

**Response (MCU → host):**
```json
{"jsonrpc":"2.0","id":1,"result":{"pin":2,"name":"led","value":true}}
```

This means you can test it with any serial terminal or `curl`-equivalent — no special tooling required.

---

## Compatibility

| Architecture | Tested |
|--------------|--------|
| ESP32 | ✅ |
| ESP8266 | ✅ |
| Arduino Uno / Mega (AVR) | ✅ |
| Arduino Nano 33 IoT | ✅ |
| RP2040 (Raspberry Pi Pico) | ✅ |

> Note: `analogWrite` (PWM) and `analogRead` (ADC) resolution varies by platform. The library uses the Arduino default (8-bit PWM, 12-bit ADC on ESP32).

---

## License

[LGPL v3](LICENSE) — Modifications to this library must remain open source. Your application code using this library may be closed source.

*By [2edge.co](https://2edge.co)*
