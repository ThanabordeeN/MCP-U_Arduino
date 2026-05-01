/**
 * MCP-U.cpp — MCP/U: The Unified Interface for AI-Ready Microcontrollers
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
    _tool_count(0),
    _buffer_count(0),
    _has_summary_pin(false),
    _has_buffer_pin(false),
    _has_event_pin(false) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void McpDevice::add_pin(uint8_t pin, const char* name, McpPinType type, const char* description) {
  McpPinOptions defaultOptions;
  add_pin(pin, name, type, description, defaultOptions);
}

void McpDevice::add_pin(uint8_t pin, const char* name, McpPinType type, const char* description, const McpPinOptions& options) {
  if (_pin_count >= MCP_MAX_PINS) return;

  McpPinOptions normalized = _normalize_options(type, options);

  PinEntry& p = _pins[_pin_count];
  p.pin = pin;
  p.name = name;
  p.type = type;
  p.description = description;
  p.options = normalized;
  p.last_sample_ms = 0;
  p.has_buffer = false;
  p.buffer_index = 255;

  _configure_hardware_pin(pin, type, normalized);
  _attach_buffer_if_needed(_pin_count, normalized);

  if (normalized.enable_summary) _has_summary_pin = true;
  if (normalized.enable_buffer)  _has_buffer_pin = true;
  if (normalized.event_enabled)  _has_event_pin = true;

  _pin_count++;
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
    _configure_hardware_pin(_pins[i].pin, _pins[i].type, _pins[i].options);
  }
}

void McpDevice::loop() {
  _process_sampling();

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
// Option normalisation
// ---------------------------------------------------------------------------

McpPinOptions McpDevice::_normalize_options(McpPinType type, const McpPinOptions& options) {
  McpPinOptions normalized = options;

  if (normalized.sample_interval_ms == 0) {
    normalized.sample_interval_ms = 1000;
  }

#if defined(MCP_PLATFORM_AVR)
  // On AVR, if buffer request exceeds platform limit, fall back to summary-only
  if (normalized.enable_buffer && normalized.buffer_size > MCP_MAX_BUFFER_SIZE) {
    normalized.enable_buffer = false;
  }
#endif

  if (normalized.buffer_size > MCP_MAX_BUFFER_SIZE) {
    normalized.buffer_size = MCP_MAX_BUFFER_SIZE;
  }

  if (type == MCP_DIGITAL_OUTPUT || type == MCP_PWM_OUTPUT) {
    normalized.enable_buffer = false;
    normalized.enable_summary = false;
    normalized.event_enabled = false;
  }

  return normalized;
}

// ---------------------------------------------------------------------------
// Hardware pin configuration
// ---------------------------------------------------------------------------

void McpDevice::_configure_hardware_pin(uint8_t pin, McpPinType type, const McpPinOptions& options) {
  if (type == MCP_DIGITAL_OUTPUT) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, options.default_state);
  } else if (type == MCP_DIGITAL_INPUT) {
    pinMode(pin, INPUT);
  } else if (type == MCP_ADC_INPUT) {
    // ADC pins need no pinMode
  } else if (type == MCP_PWM_OUTPUT) {
    pinMode(pin, OUTPUT);
  }
}

// ---------------------------------------------------------------------------
// Buffer pool management
// ---------------------------------------------------------------------------

void McpDevice::_attach_buffer_if_needed(uint8_t pin_index, const McpPinOptions& options) {
  if (!options.enable_buffer || _buffer_count >= MCP_MAX_BUFFERED_PINS) {
    _pins[pin_index].has_buffer = false;
    _pins[pin_index].buffer_index = 255;
    return;
  }

  uint16_t size = options.buffer_size;
  if (size == 0) size = MCP_DEFAULT_BUFFER_SIZE;
  if (size > MCP_MAX_BUFFER_SIZE) size = MCP_MAX_BUFFER_SIZE;

  _buffer_pool[_buffer_count].init(size);
  _pins[pin_index].has_buffer = true;
  _pins[pin_index].buffer_index = _buffer_count;
  _buffer_count++;
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

bool McpDevice::_is_input_pin(McpPinType type) {
  return (type == MCP_DIGITAL_INPUT || type == MCP_ADC_INPUT);
}

float McpDevice::_read_pin_value(const PinEntry& p) {
  if (p.type == MCP_ADC_INPUT) {
    return (float)analogRead(p.pin);
  }
  if (p.type == MCP_DIGITAL_INPUT) {
    return (float)digitalRead(p.pin);
  }
  return 0.0f;
}

void McpDevice::_process_sampling() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < _pin_count; i++) {
    PinEntry& p = _pins[i];

    if (!_is_input_pin(p.type)) continue;

    if (!p.options.enable_summary && !p.options.enable_buffer && !p.options.event_enabled) {
      continue;
    }

    if (now - p.last_sample_ms < p.options.sample_interval_ms) {
      continue;
    }

    p.last_sample_ms = now;

    float value = _read_pin_value(p);
    _stats[i].add(value);

    if (p.has_buffer) {
      _buffer_pool[p.buffer_index].add(value);
    }

    if (p.options.event_enabled) {
      _evaluate_event(p, value);
    }
  }
}

void McpDevice::_evaluate_event(const PinEntry& p, float value) {
  // Phase 1-2: lightweight event placeholder.
  // Actual event queuing is deferred to Phase 4 to keep RAM minimal.
  // get_pin_events reports current threshold state on demand.
  (void)p;
  (void)value;
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
  if      (strcmp(method, "get_info")        == 0) { _handle_get_info(id);              return; }
  else if (strcmp(method, "list_tools")      == 0) { _handle_list_tools(id);            return; }
  else if (strcmp(method, "gpio_write")      == 0) { _handle_gpio_write(id, params);    return; }
  else if (strcmp(method, "gpio_read")       == 0) { _handle_gpio_read(id, params);     return; }
  else if (strcmp(method, "pwm_write")       == 0) { _handle_pwm_write(id, params);     return; }
  else if (strcmp(method, "adc_read")        == 0) { _handle_adc_read(id, params);      return; }
  else if (strcmp(method, "get_pin_summary") == 0) { _handle_get_pin_summary(id, params); return; }
  else if (strcmp(method, "get_pin_buffer")  == 0) { _handle_get_pin_buffer(id, params);  return; }
  else if (strcmp(method, "get_pin_events")  == 0) { _handle_get_pin_events(id, params);  return; }

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
#if defined(MCP_PLATFORM_ESP32)
  res["result"]["memory_profile"] = F("rich");
#elif defined(MCP_PLATFORM_AVR)
  res["result"]["memory_profile"] = F("low");
#else
  res["result"]["memory_profile"] = F("conservative");
#endif
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
  if (_has_summary_pin) add_builtin("get_pin_summary", "Get rolling statistics for a pin");
  if (_has_buffer_pin)  add_builtin("get_pin_buffer",  "Get recent samples from a buffered pin");
  if (_has_event_pin)   add_builtin("get_pin_events",  "Get threshold events for a pin");
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
  if (_has_summary_pin) {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "string"; props["pin"]["description"] = "Pin name";
    const char* req[] = {"pin"};
    add_builtin("get_pin_summary", "Get rolling statistics for a pin", req, 1, props);
  }
  if (_has_buffer_pin) {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "string"; props["pin"]["description"] = "Pin name";
    props["limit"]["type"] = "integer"; props["limit"]["description"] = "Max samples to return";
    const char* req[] = {"pin"};
    add_builtin("get_pin_buffer", "Get recent samples from a buffered pin", req, 1, props);
  }
  if (_has_event_pin) {
    JsonDocument tmp;
    JsonObject props = tmp.to<JsonObject>();
    props["pin"]["type"] = "string"; props["pin"]["description"] = "Pin name";
    const char* req[] = {"pin"};
    add_builtin("get_pin_events", "Get threshold events for a pin", req, 1, props);
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
#if !defined(ARDUINO_ARCH_AVR)
    JsonObject caps = p["capabilities"].to<JsonObject>();
    caps["summary"] = _pins[i].options.enable_summary;
    caps["buffer"]  = _pins[i].has_buffer;
    caps["events"]  = _pins[i].options.event_enabled;
    if (_pins[i].options.enable_summary || _pins[i].options.enable_buffer || _pins[i].options.event_enabled) {
      JsonObject sampling = p["sampling"].to<JsonObject>();
      sampling["interval_ms"] = _pins[i].options.sample_interval_ms;
      if (_pins[i].has_buffer) {
        sampling["buffer_size"] = _buffer_pool[_pins[i].buffer_index].capacity;
      }
    }
#endif
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
#if defined(MCP_PLATFORM_AVR)
  // AVR ADC is 10-bit (0–1023); integer mV avoids softfloat library (~1.5KB)
  res["result"]["mv"] = (uint32_t)raw * 3300 / 1023;
#elif defined(MCP_PLATFORM_ESP32)
  res["result"]["volts"] = (raw / 4095.0f) * 3.3f;
#endif
  send_result(id, res);
}

// ---------------------------------------------------------------------------
// New tool handlers: pin summary / buffer / events
// ---------------------------------------------------------------------------

void McpDevice::_handle_get_pin_summary(int id, JsonObject params) {
  PinEntry* cfg = nullptr;
  if (params["pin"].is<const char*>()) {
    cfg = _find_pin_by_name(params["pin"].as<const char*>());
  } else if (params["pin"].is<int>()) {
    cfg = _find_pin(params["pin"].as<int>());
  }

  if (!cfg) {
    send_error(id, -32602, F("Pin not found")); return;
  }

  if (!cfg->options.enable_summary) {
    send_error(id, -32602, F("Summary not enabled for this pin")); return;
  }

  uint8_t idx = 255;
  for (uint8_t i = 0; i < _pin_count; i++) {
    if (&_pins[i] == cfg) { idx = i; break; }
  }
  if (idx == 255) {
    send_error(id, -32602, F("Pin not found")); return;
  }

  JsonDocument res;
  res["result"]["pin"]     = cfg->pin;
  res["result"]["name"]    = cfg->name;
  res["result"]["latest"]  = _stats[idx].latest;
  res["result"]["min"]     = _stats[idx].min_value;
  res["result"]["max"]     = _stats[idx].max_value;
  res["result"]["avg"]     = _stats[idx].average();
  res["result"]["samples"] = _stats[idx].count;
  send_result(id, res);
}

void McpDevice::_handle_get_pin_buffer(int id, JsonObject params) {
  PinEntry* cfg = nullptr;
  if (params["pin"].is<const char*>()) {
    cfg = _find_pin_by_name(params["pin"].as<const char*>());
  } else if (params["pin"].is<int>()) {
    cfg = _find_pin(params["pin"].as<int>());
  }

  if (!cfg) {
    send_error(id, -32602, F("Pin not found")); return;
  }

  JsonDocument res;
  res["result"]["pin"] = cfg->name;

  if (!cfg->has_buffer) {
    res["result"]["buffer_available"] = false;
    res["result"]["reason"] = F("Buffer disabled on this platform or pin. Use get_pin_summary instead.");
    send_result(id, res);
    return;
  }

  McpRingBuffer& buf = _buffer_pool[cfg->buffer_index];
  uint16_t limit = buf.count;
#if defined(MCP_PLATFORM_AVR)
  if (limit > MCP_DEFAULT_BUFFER_SIZE) {
    limit = MCP_DEFAULT_BUFFER_SIZE;
  }
#endif
  if (params["limit"].is<int>()) {
    limit = params["limit"].as<int>();
    if (limit > buf.count) limit = buf.count;
  }

  res["result"]["count"] = buf.count;
  JsonArray values = res["result"]["values"].to<JsonArray>();
  for (uint16_t i = 0; i < limit; i++) {
    values.add(buf.get(i));
  }
  send_result(id, res);
}

void McpDevice::_handle_get_pin_events(int id, JsonObject params) {
  PinEntry* cfg = nullptr;
  if (params["pin"].is<const char*>()) {
    cfg = _find_pin_by_name(params["pin"].as<const char*>());
  } else if (params["pin"].is<int>()) {
    cfg = _find_pin(params["pin"].as<int>());
  }

  if (!cfg) {
    send_error(id, -32602, F("Pin not found")); return;
  }

  uint8_t idx = 255;
  for (uint8_t i = 0; i < _pin_count; i++) {
    if (&_pins[i] == cfg) { idx = i; break; }
  }
  if (idx == 255) {
    send_error(id, -32602, F("Pin not found")); return;
  }

  JsonDocument res;
  res["result"]["pin"] = cfg->name;
  JsonArray events = res["result"]["events"].to<JsonArray>();

  if (cfg->options.event_enabled) {
    float val = _stats[idx].latest;
    if (val > cfg->options.max_threshold) {
      JsonObject e = events.add<JsonObject>();
      e["type"] = "threshold_high";
      e["value"] = val;
      e["threshold"] = cfg->options.max_threshold;
    }
    if (val < cfg->options.min_threshold) {
      JsonObject e = events.add<JsonObject>();
      e["type"] = "threshold_low";
      e["value"] = val;
      e["threshold"] = cfg->options.min_threshold;
    }
  }

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

McpDevice::PinEntry* McpDevice::_find_pin_by_name(const char* name) {
  for (uint8_t i = 0; i < _pin_count; i++) {
    if (strcmp(_pins[i].name, name) == 0) return &_pins[i];
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
