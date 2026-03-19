/**
 * McpIot.cpp — MCP/U: The Unified Interface for AI-Ready Microcontrollers
 * By 2edge.co — LGPL-3.0 — https://2edge.co
 */

#include "MCP-U.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

McpDevice::McpDevice(const char* device_name, const char* version)
  : _device_name(device_name),
    _version(version),
    _stream(nullptr),
    _pin_count(0),
    _tool_count(0) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void McpDevice::add_pin(uint8_t pin, const char* name, McpPinType type, const char* description) {
  if (_pin_count >= MCP_MAX_PINS) return;
  _pins[_pin_count++] = { pin, name, type, description };
}

void McpDevice::add_tool(const char* name, const char* description, McpToolHandler handler) {
  if (_tool_count >= MCP_MAX_TOOLS) return;
  _tools[_tool_count++] = { name, description, handler };
}

void McpDevice::begin(Stream& stream, unsigned long baud) {
  _stream = &stream;

  if (baud > 0) {
    // Only HardwareSerial / SoftwareSerial have begin(baud).
    // Cast attempt is safe: if it isn't a serial, we simply skip.
    HardwareSerial* hw = (HardwareSerial*)&stream;
    hw->begin(baud);
    delay(10);
  }

  // Initialise pins
  for (uint8_t i = 0; i < _pin_count; i++) {
    switch (_pins[i].type) {
      case MCP_DIGITAL_OUTPUT:
        pinMode(_pins[i].pin, OUTPUT);
        digitalWrite(_pins[i].pin, LOW);
        break;
      case MCP_DIGITAL_INPUT:
        pinMode(_pins[i].pin, INPUT);
        break;
      case MCP_PWM_OUTPUT:
        pinMode(_pins[i].pin, OUTPUT);
        break;
      case MCP_ADC_INPUT:
        // ADC pins need no pinMode
        break;
    }
  }
}

void McpDevice::loop() {
  if (!_stream || !_stream->available()) return;
  uint16_t len = 0;
  while (_stream->available() && len < sizeof(_buf) - 1) {
    char c = _stream->read();
    if (c == '\n') break;
    if (c != '\r') _buf[len++] = c;
  }
  _buf[len] = '\0';
  if (len > 0) _dispatch(_buf);
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void McpDevice::send_error(int id, int code, const char* message) {
  JsonDocument res;
  res["jsonrpc"]          = F("2.0");
  res["id"]               = id;
  res["error"]["code"]    = code;
  res["error"]["message"] = message;
  serializeJson(res, *_stream);
  _stream->println();
}

void McpDevice::send_error(int id, int code, const __FlashStringHelper* message) {
  JsonDocument res;
  res["jsonrpc"]          = F("2.0");
  res["id"]               = id;
  res["error"]["code"]    = code;
  res["error"]["message"] = message;
  serializeJson(res, *_stream);
  _stream->println();
}

void McpDevice::send_result(int id, JsonDocument& doc) {
  doc["jsonrpc"] = F("2.0");
  doc["id"]      = id;
  serializeJson(doc, *_stream);
  _stream->println();
}

// ---------------------------------------------------------------------------
// RPC Dispatcher
// ---------------------------------------------------------------------------

void McpDevice::_dispatch(char* buf) {
  JsonDocument req;
  DeserializationError err = deserializeJson(req, buf);

  if (err) { send_error(0, -32700, F("Parse error")); return; }

  if (req["jsonrpc"] != "2.0") { send_error(0, -32600, F("Invalid request")); return; }

  // Notifications (no id) are silently ignored
  if (!req["id"].is<int>()) return;

  int         id     = req["id"].as<int>();
  const char* method = req["method"];

  if (!method) { send_error(id, -32600, F("Missing method")); return; }

  JsonObject params = req["params"].as<JsonObject>();

  // Built-in methods
  if      (strcmp(method, "get_info")   == 0) { _handle_get_info(id);              return; }
  else if (strcmp(method, "list_tools") == 0) { _handle_list_tools(id);            return; }
  else if (strcmp(method, "gpio_write") == 0) { _handle_gpio_write(id, params);    return; }
  else if (strcmp(method, "gpio_read")  == 0) { _handle_gpio_read(id, params);     return; }
  else if (strcmp(method, "pwm_write")  == 0) { _handle_pwm_write(id, params);     return; }
  else if (strcmp(method, "adc_read")   == 0) { _handle_adc_read(id, params);      return; }

  // User-registered tools
  ToolEntry* tool = _find_tool(method);
  if (tool) { tool->handler(id, params); return; }

  send_error(id, -32601, F("Method not found"));
}

// ---------------------------------------------------------------------------
// Built-in handlers
// ---------------------------------------------------------------------------

void McpDevice::_handle_get_info(int id) {
  JsonDocument res;
  res["result"]["device"]    = _device_name;
  res["result"]["version"]   = _version;
  res["result"]["platform"]  = F("arduino");
  res["result"]["pin_count"] = _pin_count;
  send_result(id, res);
}

void McpDevice::_handle_list_tools(int id) {
  JsonDocument res;
  JsonObject   result = res["result"].to<JsonObject>();

  result["device"]  = _device_name;
  result["version"] = _version;

  JsonArray tools = result["tools"].to<JsonArray>();

#if defined(ARDUINO_ARCH_AVR)
  // AVR: emit name+description only — no inputSchema to avoid heap alloc
  auto add_builtin = [&](const char* name, const char* desc) {
    JsonObject t = tools.add<JsonObject>();
    t["name"]        = name;
    t["description"] = desc;
  };
  add_builtin("gpio_write", "Write HIGH/LOW to digital output pin");
  add_builtin("gpio_read",  "Read level of a digital pin");
  add_builtin("pwm_write",  "Write PWM duty cycle to a pin");
  add_builtin("adc_read",   "Read ADC value from analog pin");
  add_builtin("get_info",   "Get device metadata");
#else
  // Full schema for capable boards (ESP32 etc.)
  auto add_builtin = [&](const char* name, const char* desc,
                         const char** required, uint8_t req_count,
                         JsonObject props) {
    JsonObject t = tools.add<JsonObject>();
    t["name"]        = name;
    t["description"] = desc;
    JsonObject schema = t["inputSchema"].to<JsonObject>();
    schema["type"] = "object";
    JsonArray req_arr = schema["required"].to<JsonArray>();
    for (uint8_t i = 0; i < req_count; i++) req_arr.add(required[i]);
    schema["properties"] = props;
  };
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "integer"; props["pin"]["description"] = "GPIO pin number";
    props["value"]["type"] = "boolean"; props["value"]["description"] = "true = HIGH, false = LOW";
    const char* req[] = {"pin", "value"};
    add_builtin("gpio_write", "Write HIGH or LOW to a digital output pin", req, 2, props);
  }
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "integer"; props["pin"]["description"] = "GPIO pin number";
    const char* req[] = {"pin"};
    add_builtin("gpio_read", "Read current level of a digital pin", req, 1, props);
  }
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"]  = "integer"; props["pin"]["description"]  = "GPIO pin number";
    props["duty"]["type"] = "integer"; props["duty"]["description"] = "Duty cycle 0-255";
    const char* req[] = {"pin", "duty"};
    add_builtin("pwm_write", "Write PWM signal to a pin", req, 2, props);
  }
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "integer"; props["pin"]["description"] = "GPIO pin number";
    const char* req[] = {"pin"};
    add_builtin("adc_read", "Read raw ADC value (0-4095) from an analog pin", req, 1, props);
  }
  {
    JsonObject t = tools.add<JsonObject>();
    t["name"] = "get_info"; t["description"] = "Get device metadata";
    t["inputSchema"]["type"] = "object";
    t["inputSchema"]["required"].to<JsonArray>();
    t["inputSchema"]["properties"].to<JsonObject>();
  }
#endif

  // User-registered tools
  for (uint8_t i = 0; i < _tool_count; i++) {
    JsonObject t = tools.add<JsonObject>();
    t["name"]        = _tools[i].name;
    t["description"] = _tools[i].description;
    t["inputSchema"]["type"] = "object";
    t["inputSchema"]["required"].to<JsonArray>();
    t["inputSchema"]["properties"].to<JsonObject>();
  }

  // Pin registry
  JsonArray pins = result["pins"].to<JsonArray>();
  for (uint8_t i = 0; i < _pin_count; i++) {
    JsonObject p = pins.add<JsonObject>();
    p["pin"]         = _pins[i].pin;
    p["name"]        = _pins[i].name;
    p["type"]        = _pin_type_str(_pins[i].type);
    p["description"] = _pins[i].description;
  }

  send_result(id, res);
}

void McpDevice::_handle_gpio_write(int id, JsonObject params) {
  if (!params["pin"].is<int>() || !params["value"].is<bool>()) {
    send_error(id, -32602, F("Bad params")); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || cfg->type != MCP_DIGITAL_OUTPUT) {
    send_error(id, -32602, F("Wrong pin type")); return;
  }
  bool val = params["value"].as<bool>();
  digitalWrite(cfg->pin, val ? HIGH : LOW);

  JsonDocument res;
  res["result"]["pin"]   = cfg->pin;
  res["result"]["name"]  = cfg->name;
  res["result"]["value"] = val;
  send_result(id, res);
}

void McpDevice::_handle_gpio_read(int id, JsonObject params) {
  if (!params["pin"].is<int>()) {
    send_error(id, -32602, F("Bad params")); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || (cfg->type != MCP_DIGITAL_INPUT && cfg->type != MCP_DIGITAL_OUTPUT)) {
    send_error(id, -32602, F("Wrong pin type")); return;
  }
  bool val = (bool)digitalRead(cfg->pin);

  JsonDocument res;
  res["result"]["pin"]   = cfg->pin;
  res["result"]["name"]  = cfg->name;
  res["result"]["value"] = val;
  send_result(id, res);
}

void McpDevice::_handle_pwm_write(int id, JsonObject params) {
  if (!params["pin"].is<int>() || !params["duty"].is<int>()) {
    send_error(id, -32602, F("Bad params")); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || cfg->type != MCP_PWM_OUTPUT) {
    send_error(id, -32602, F("Wrong pin type")); return;
  }
  int duty = params["duty"].as<int>();

  analogWrite(cfg->pin, duty);

  JsonDocument res;
  res["result"]["pin"]  = cfg->pin;
  res["result"]["name"] = cfg->name;
  res["result"]["duty"] = duty;
  send_result(id, res);
}

void McpDevice::_handle_adc_read(int id, JsonObject params) {
  if (!params["pin"].is<int>()) {
    send_error(id, -32602, F("Bad params")); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || cfg->type != MCP_ADC_INPUT) {
    send_error(id, -32602, F("Wrong pin type")); return;
  }
  int raw = analogRead(cfg->pin);

  JsonDocument res;
  res["result"]["pin"]   = cfg->pin;
  res["result"]["name"]  = cfg->name;
  res["result"]["value"] = raw;
#if defined(ARDUINO_ARCH_AVR)
  // AVR ADC is 10-bit (0–1023); integer mV avoids softfloat library (~1.5KB)
  res["result"]["mv"] = (uint32_t)raw * 3300 / 1023;
#else
  res["result"]["volts"] = (raw / 4095.0f) * 3.3f;
#endif
  send_result(id, res);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

McpDevice::PinEntry* McpDevice::_find_pin(uint8_t pin) {
  for (uint8_t i = 0; i < _pin_count; i++) {
    if (_pins[i].pin == pin) return &_pins[i];
  }
  return nullptr;
}

McpDevice::ToolEntry* McpDevice::_find_tool(const char* name) {
  for (uint8_t i = 0; i < _tool_count; i++) {
    if (strcmp(_tools[i].name, name) == 0) return &_tools[i];
  }
  return nullptr;
}

const char* McpDevice::_pin_type_str(McpPinType t) {
  switch (t) {
    case MCP_DIGITAL_OUTPUT: return "digital_output";
    case MCP_DIGITAL_INPUT:  return "digital_input";
    case MCP_PWM_OUTPUT:     return "pwm_output";
    case MCP_ADC_INPUT:      return "adc_input";
    default:                 return "unknown";
  }
}
