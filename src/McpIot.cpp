/**
 * McpIot.cpp — MCP/U: The Unified Interface for AI-Ready Microcontrollers
 * By 2edge.co — LGPL-3.0 — https://2edge.co
 */

#include "McpIot.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

McpDevice::McpDevice(const char* device_name, const char* version)
  : _device_name(device_name),
    _version(version),
    _stream(nullptr),
    _pin_count(0),
    _tool_count(0),
    _i2c_enabled(false),
    _wire(&Wire) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void McpDevice::add_pin(uint8_t pin, const char* name, McpPinType type, const char* description) {
  if (_pin_count >= MCP_MAX_PINS) return;
  _pins[_pin_count++] = { pin, name, type, description };
}

void McpDevice::begin_i2c(int sda, int scl, uint32_t freq) {
  _i2c_enabled = true;
#if defined(ESP32) || defined(ESP8266)
  if (sda >= 0 && scl >= 0) {
    _wire->begin(sda, scl);
  } else {
    _wire->begin();
  }
#else
  _wire->begin();  // AVR/RP2040: pins fixed by hardware
#endif
  _wire->setClock(freq);
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
  String input = _stream->readStringUntil('\n');
  input.trim();
  if (input.length() > 0) _dispatch(input);
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void McpDevice::send_error(int id, int code, const char* message) {
  JsonDocument res;
  res["jsonrpc"]          = "2.0";
  res["id"]               = id;
  res["error"]["code"]    = code;
  res["error"]["message"] = message;
  serializeJson(res, *_stream);
  _stream->println();
}

void McpDevice::send_result(int id, JsonDocument& doc) {
  doc["jsonrpc"] = "2.0";
  doc["id"]      = id;
  serializeJson(doc, *_stream);
  _stream->println();
}

// ---------------------------------------------------------------------------
// RPC Dispatcher
// ---------------------------------------------------------------------------

void McpDevice::_dispatch(const String& input) {
  JsonDocument req;
  DeserializationError err = deserializeJson(req, input);

  if (err) { send_error(0, -32700, "Parse error"); return; }

  // JSON-RPC 2.0: validate version
  if (req["jsonrpc"] != "2.0") { send_error(0, -32600, "Invalid request: jsonrpc must be '2.0'"); return; }

  // Notifications (no id) are silently ignored
  if (!req["id"].is<int>()) return;

  int         id     = req["id"].as<int>();
  const char* method = req["method"];

  if (!method) { send_error(id, -32600, "Invalid request: missing method"); return; }

  JsonObject params = req["params"].as<JsonObject>();

  // Built-in methods
  if      (strcmp(method, "get_info")   == 0) { _handle_get_info(id);              return; }
  else if (strcmp(method, "list_tools") == 0) { _handle_list_tools(id);            return; }
  else if (strcmp(method, "gpio_write") == 0) { _handle_gpio_write(id, params);    return; }
  else if (strcmp(method, "gpio_read")  == 0) { _handle_gpio_read(id, params);     return; }
  else if (strcmp(method, "pwm_write")  == 0) { _handle_pwm_write(id, params);     return; }
  else if (strcmp(method, "adc_read")     == 0) { _handle_adc_read(id, params);      return; }
  else if (strcmp(method, "i2c_scan")     == 0) { _handle_i2c_scan(id);              return; }
  else if (strcmp(method, "i2c_write_reg")== 0) { _handle_i2c_write_reg(id, params); return; }
  else if (strcmp(method, "i2c_read_reg") == 0) { _handle_i2c_read_reg(id, params);  return; }

  // User-registered tools
  ToolEntry* tool = _find_tool(method);
  if (tool) { tool->handler(id, params); return; }

  send_error(id, -32601, "Method not found");
}

// ---------------------------------------------------------------------------
// Built-in handlers
// ---------------------------------------------------------------------------

void McpDevice::_handle_get_info(int id) {
  JsonDocument res;
  res["result"]["device"]    = _device_name;
  res["result"]["version"]   = _version;
  res["result"]["platform"]  = "arduino";
  res["result"]["pin_count"] = _pin_count;
  send_result(id, res);
}

void McpDevice::_handle_list_tools(int id) {
  JsonDocument res;
  JsonObject   result = res["result"].to<JsonObject>();

  result["device"]  = _device_name;
  result["version"] = _version;

  JsonArray tools = result["tools"].to<JsonArray>();

  // Helper lambda to add a built-in tool with full schema
  auto add_builtin = [&](const char* name, const char* desc,
                         std::initializer_list<const char*> required,
                         JsonObject props) {
    JsonObject t = tools.add<JsonObject>();
    t["name"]        = name;
    t["description"] = desc;
    JsonObject schema = t["inputSchema"].to<JsonObject>();
    schema["type"] = "object";
    JsonArray req_arr = schema["required"].to<JsonArray>();
    for (auto r : required) req_arr.add(r);
    schema["properties"] = props;
  };

  // gpio_write
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "integer"; props["pin"]["description"] = "GPIO pin number";
    props["value"]["type"] = "boolean"; props["value"]["description"] = "true = HIGH, false = LOW";
    add_builtin("gpio_write", "Write HIGH or LOW to a digital output pin", {"pin", "value"}, props);
  }
  // gpio_read
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "integer"; props["pin"]["description"] = "GPIO pin number";
    add_builtin("gpio_read", "Read current level of a digital pin", {"pin"}, props);
  }
  // pwm_write
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"]  = "integer"; props["pin"]["description"]  = "GPIO pin number";
    props["duty"]["type"] = "integer"; props["duty"]["description"] = "Duty cycle 0–255";
    add_builtin("pwm_write", "Write PWM signal to a pin", {"pin", "duty"}, props);
  }
  // adc_read
  {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "integer"; props["pin"]["description"] = "GPIO pin number";
    add_builtin("adc_read", "Read raw ADC value (0–4095) from an analog pin", {"pin"}, props);
  }
  // get_info (no params)
  {
    JsonObject t = tools.add<JsonObject>();
    t["name"] = "get_info"; t["description"] = "Get device metadata";
    t["inputSchema"]["type"] = "object";
    t["inputSchema"]["required"].to<JsonArray>();
    t["inputSchema"]["properties"].to<JsonObject>();
  }

  // I2C tools (only if begin_i2c() was called)
  if (_i2c_enabled) {
    // i2c_scan
    {
      JsonObject t = tools.add<JsonObject>();
      t["name"] = "i2c_scan";
      t["description"] = "Scan I2C bus and return addresses of all responding devices";
      t["inputSchema"]["type"] = "object";
      t["inputSchema"]["required"].to<JsonArray>();
      t["inputSchema"]["properties"].to<JsonObject>();
    }
    // i2c_write_reg
    {
      JsonDocument tmp;
      JsonObject props = tmp.to<JsonObject>();
      props["address"]["type"] = "integer"; props["address"]["description"] = "I2C device address (e.g. 0x3C = 60)";
      props["reg"]["type"]     = "integer"; props["reg"]["description"]     = "Register address to write";
      props["value"]["type"]   = "integer"; props["value"]["description"]   = "Byte value to write (0–255)";
      add_builtin("i2c_write_reg", "Write a byte to an I2C device register", {"address", "reg", "value"}, props);
    }
    // i2c_read_reg
    {
      JsonDocument tmp;
      JsonObject props = tmp.to<JsonObject>();
      props["address"]["type"] = "integer"; props["address"]["description"] = "I2C device address";
      props["reg"]["type"]     = "integer"; props["reg"]["description"]     = "Register address to read from";
      props["length"]["type"]  = "integer"; props["length"]["description"]  = "Number of bytes to read (1–32)";
      add_builtin("i2c_read_reg", "Read bytes from an I2C device register", {"address", "reg", "length"}, props);
    }
  }

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
    send_error(id, -32602, "Required: pin (integer), value (boolean)"); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || cfg->type != MCP_DIGITAL_OUTPUT) {
    send_error(id, -32602, "Pin not registered as digital_output"); return;
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
    send_error(id, -32602, "Required: pin (integer)"); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || (cfg->type != MCP_DIGITAL_INPUT && cfg->type != MCP_DIGITAL_OUTPUT)) {
    send_error(id, -32602, "Pin not registered as digital_input or digital_output"); return;
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
    send_error(id, -32602, "Required: pin (integer), duty (integer 0-255)"); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || cfg->type != MCP_PWM_OUTPUT) {
    send_error(id, -32602, "Pin not registered as pwm_output"); return;
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
    send_error(id, -32602, "Required: pin (integer)"); return;
  }
  PinEntry* cfg = _find_pin(params["pin"].as<int>());
  if (!cfg || cfg->type != MCP_ADC_INPUT) {
    send_error(id, -32602, "Pin not registered as adc_input"); return;
  }
  int raw = analogRead(cfg->pin);

  JsonDocument res;
  res["result"]["pin"]   = cfg->pin;
  res["result"]["name"]  = cfg->name;
  res["result"]["value"] = raw;
  res["result"]["volts"] = (raw / 4095.0f) * 3.3f;
  send_result(id, res);
}

void McpDevice::_handle_i2c_scan(int id) {
  if (!_i2c_enabled) { send_error(id, -32601, "I2C not enabled — call begin_i2c() first"); return; }

  JsonDocument res;
  JsonArray devices = res["result"]["devices"].to<JsonArray>();

  for (uint8_t addr = 1; addr < 127; addr++) {
    _wire->beginTransmission(addr);
    if (_wire->endTransmission() == 0) devices.add(addr);
  }

  res["result"]["count"] = devices.size();
  send_result(id, res);
}

void McpDevice::_handle_i2c_write_reg(int id, JsonObject params) {
  if (!_i2c_enabled) { send_error(id, -32601, "I2C not enabled — call begin_i2c() first"); return; }
  if (!params["address"].is<int>() || !params["reg"].is<int>() || !params["value"].is<int>()) {
    send_error(id, -32602, "Required: address (integer), reg (integer), value (integer)"); return;
  }

  uint8_t addr  = params["address"].as<uint8_t>();
  uint8_t reg   = params["reg"].as<uint8_t>();
  uint8_t value = params["value"].as<uint8_t>();

  _wire->beginTransmission(addr);
  _wire->write(reg);
  _wire->write(value);
  uint8_t err = _wire->endTransmission();

  if (err != 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "I2C write failed: error %d (addr=0x%02X)", err, addr);
    send_error(id, -32603, msg); return;
  }

  JsonDocument res;
  res["result"]["address"] = addr;
  res["result"]["reg"]     = reg;
  res["result"]["value"]   = value;
  send_result(id, res);
}

void McpDevice::_handle_i2c_read_reg(int id, JsonObject params) {
  if (!_i2c_enabled) { send_error(id, -32601, "I2C not enabled — call begin_i2c() first"); return; }
  if (!params["address"].is<int>() || !params["reg"].is<int>() || !params["length"].is<int>()) {
    send_error(id, -32602, "Required: address (integer), reg (integer), length (integer)"); return;
  }

  uint8_t addr   = params["address"].as<uint8_t>();
  uint8_t reg    = params["reg"].as<uint8_t>();
  uint8_t length = params["length"].as<uint8_t>();
  if (length == 0 || length > 32) { send_error(id, -32602, "length must be 1–32"); return; }

  // Point to register
  _wire->beginTransmission(addr);
  _wire->write(reg);
  uint8_t err = _wire->endTransmission(false); // repeated start
  if (err != 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "I2C address error %d (addr=0x%02X)", err, addr);
    send_error(id, -32603, msg); return;
  }

  uint8_t received = _wire->requestFrom(addr, length);
  if (received == 0) {
    send_error(id, -32603, "I2C read: no bytes received"); return;
  }

  JsonDocument res;
  JsonArray data = res["result"]["data"].to<JsonArray>();
  char hex_buf[32 * 3 + 1]; // max 32 bytes, each printed as "XX "
  uint8_t idx = 0;

  while (_wire->available()) {
    uint8_t b = _wire->read();
    data.add(b);
    idx += snprintf(hex_buf + idx, sizeof(hex_buf) - idx, "%02X ", b);
  }
  if (idx > 0) hex_buf[idx - 1] = '\0'; // trim trailing space

  res["result"]["address"] = addr;
  res["result"]["reg"]     = reg;
  res["result"]["hex"]     = hex_buf;
  res["result"]["length"]  = received;
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
