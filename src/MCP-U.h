/**
 * MCP-U.h — MCP/U: The Unified Interface for AI-Ready Microcontrollers
 *
 * Transforms any Arduino-compatible MCU into an AI-ready device via
 * JSON-RPC 2.0 over any Arduino Stream (Serial, WiFiClient, BluetoothSerial).
 *
 * Usage:
 *   McpDevice mcp("my-device", "1.0.0");
 *   mcp.add_pin(2, "led", MCP_DIGITAL_OUTPUT, "Onboard LED");
 *   mcp.add_tool("beep", "Trigger a beep", beep_handler);
 *   mcp.begin(Serial, 115200);
 *   // In loop(): mcp.loop();
 *
 * By 2edge.co — LGPL-3.0 — https://2edge.co
 *
 * MIT License — https://github.com/your-repo/mcp-iot
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

#if defined(ARDUINO_ARCH_ESP32)
  #define MCP_PLATFORM_ESP32 1
#elif defined(ARDUINO_ARCH_AVR)
  #define MCP_PLATFORM_AVR 1
#else
  #define MCP_PLATFORM_GENERIC 1
#endif

// ---------------------------------------------------------------------------
// Limits (override via build flags if needed)
// ---------------------------------------------------------------------------

#if defined(MCP_PLATFORM_AVR)
  #ifndef MCP_MAX_PINS
  #define MCP_MAX_PINS  8
  #endif
  #ifndef MCP_MAX_TOOLS
  #define MCP_MAX_TOOLS 8
  #endif
  #ifndef MCP_SERIAL_BUFFER
  #define MCP_SERIAL_BUFFER 256
  #endif
  #ifndef MCP_MAX_BUFFERED_PINS
  #define MCP_MAX_BUFFERED_PINS 2
  #endif
  #ifndef MCP_MAX_BUFFER_SIZE
  #define MCP_MAX_BUFFER_SIZE 20
  #endif
  #ifndef MCP_DEFAULT_BUFFER_SIZE
  #define MCP_DEFAULT_BUFFER_SIZE 10
  #endif
#elif defined(MCP_PLATFORM_ESP32)
  #ifndef MCP_MAX_PINS
  #define MCP_MAX_PINS  16
  #endif
  #ifndef MCP_MAX_TOOLS
  #define MCP_MAX_TOOLS 24
  #endif
  #ifndef MCP_SERIAL_BUFFER
  #define MCP_SERIAL_BUFFER 512
  #endif
  #ifndef MCP_MAX_BUFFERED_PINS
  #define MCP_MAX_BUFFERED_PINS 8
  #endif
  #ifndef MCP_MAX_BUFFER_SIZE
  #define MCP_MAX_BUFFER_SIZE 300
  #endif
  #ifndef MCP_DEFAULT_BUFFER_SIZE
  #define MCP_DEFAULT_BUFFER_SIZE 120
  #endif
#else
  #ifndef MCP_MAX_PINS
  #define MCP_MAX_PINS  8
  #endif
  #ifndef MCP_MAX_TOOLS
  #define MCP_MAX_TOOLS 8
  #endif
  #ifndef MCP_SERIAL_BUFFER
  #define MCP_SERIAL_BUFFER 256
  #endif
  #ifndef MCP_MAX_BUFFERED_PINS
  #define MCP_MAX_BUFFERED_PINS 2
  #endif
  #ifndef MCP_MAX_BUFFER_SIZE
  #define MCP_MAX_BUFFER_SIZE 30
  #endif
  #ifndef MCP_DEFAULT_BUFFER_SIZE
  #define MCP_DEFAULT_BUFFER_SIZE 20
  #endif
#endif

// ---------------------------------------------------------------------------
// Pin types
// ---------------------------------------------------------------------------

enum McpPinType {
  MCP_DIGITAL_OUTPUT,
  MCP_DIGITAL_INPUT,
  MCP_PWM_OUTPUT,
  MCP_ADC_INPUT,
  MCP_ANALOG_INPUT = MCP_ADC_INPUT,  // Convenience alias
};

// ---------------------------------------------------------------------------
// Pin Options
// ---------------------------------------------------------------------------

struct McpPinOptions {
  bool enable_buffer;
  bool enable_summary;
  bool require_approval;
  bool readback_enabled;
  bool event_enabled;

  uint16_t buffer_size;
  uint16_t sample_interval_ms;

  float min_threshold;
  float max_threshold;

  int default_state;

  McpPinOptions()
    : enable_buffer(false),
      enable_summary(false),
      require_approval(false),
      readback_enabled(false),
      event_enabled(false),
      buffer_size(0),
      sample_interval_ms(1000),
      min_threshold(0),
      max_threshold(0),
      default_state(LOW) {}
};

// ---------------------------------------------------------------------------
// Helper functions for creating McpPinOptions
// ---------------------------------------------------------------------------

inline McpPinOptions McpBuffered(uint16_t bufferSize, uint16_t intervalMs) {
  McpPinOptions opt;
  opt.enable_buffer = true;
  opt.enable_summary = true;
  opt.buffer_size = bufferSize;
  opt.sample_interval_ms = intervalMs;
  return opt;
}

inline McpPinOptions McpSummaryOnly(uint16_t intervalMs) {
  McpPinOptions opt;
  opt.enable_buffer = false;
  opt.enable_summary = true;
  opt.sample_interval_ms = intervalMs;
  return opt;
}

inline McpPinOptions McpOutputSafe(bool approvalRequired) {
  McpPinOptions opt;
  opt.require_approval = approvalRequired;
  opt.readback_enabled = true;
  opt.default_state = LOW;
  return opt;
}

inline McpPinOptions McpThreshold(float minValue, float maxValue, uint16_t intervalMs) {
  McpPinOptions opt;
  opt.enable_summary = true;
  opt.event_enabled = true;
  opt.min_threshold = minValue;
  opt.max_threshold = maxValue;
  opt.sample_interval_ms = intervalMs;
  return opt;
}

// ---------------------------------------------------------------------------
// Rolling Statistics
// ---------------------------------------------------------------------------

struct McpPinStats {
  float latest;
  float min_value;
  float max_value;
  float sum;
  uint16_t count;

  McpPinStats()
    : latest(0),
      min_value(0),
      max_value(0),
      sum(0),
      count(0) {}

  void reset() {
    latest = 0;
    min_value = 0;
    max_value = 0;
    sum = 0;
    count = 0;
  }

  void add(float value) {
    latest = value;

    if (count == 0) {
      min_value = value;
      max_value = value;
    } else {
      if (value < min_value) min_value = value;
      if (value > max_value) max_value = value;
    }

    sum += value;

    if (count < 65535) {
      count++;
    }
  }

  float average() const {
    if (count == 0) return 0;
    return sum / count;
  }
};

// ---------------------------------------------------------------------------
// Ring Buffer
// ---------------------------------------------------------------------------

struct McpRingBuffer {
  float values[MCP_MAX_BUFFER_SIZE];
  uint16_t head;
  uint16_t count;
  uint16_t capacity;

  void init(uint16_t size) {
    if (size > MCP_MAX_BUFFER_SIZE) {
      size = MCP_MAX_BUFFER_SIZE;
    }
    capacity = size;
    head = 0;
    count = 0;
  }

  void add(float value) {
    if (capacity == 0) return;

    values[head] = value;
    head = (head + 1) % capacity;

    if (count < capacity) {
      count++;
    }
  }

  float get(uint16_t index) const {
    if (index >= count) return 0;
    if (count < capacity) {
      return values[index];
    }
    return values[(head + index) % capacity];
  }
};

// ---------------------------------------------------------------------------
// Tool handler signature
// id     — JSON-RPC request id (pass to send_error / send_result)
// params — JsonObject containing the call arguments
// ---------------------------------------------------------------------------

typedef void (*McpToolHandler)(int id, JsonObject params);

// ---------------------------------------------------------------------------
// McpDevice
// ---------------------------------------------------------------------------

class McpDevice {
public:
  /**
   * @param device_name  Unique device name sent during discovery
   * @param version      Firmware version string (e.g. "1.0.0")
   */
  McpDevice(const char* device_name, const char* version);

  /**
   * Register a hardware pin in the pin registry.
   * Built-in tools (gpio_write, gpio_read, etc.) validate against this list.
   * Must be called before begin().
   */
  void add_pin(uint8_t pin, const char* name, McpPinType type, const char* description);

  /**
   * Register a hardware pin with optional behavior configuration.
   * Must be called before begin().
   */
  void add_pin(uint8_t pin, const char* name, McpPinType type, const char* description, const McpPinOptions& options);

  /**
   * Register a custom RPC tool.
   * The handler receives the request id and params JsonObject.
   * Use send_result() or send_error() to respond.
   * Must be called before begin().
   */
  void add_tool(const char* name, const char* description, McpToolHandler handler);

  /**
   * Start the MCP device on any Arduino Stream.
   * Examples:
   *   mcp.begin(Serial, 115200);       // UART
   *   mcp.begin(wifi_client);          // WiFiClient (baud ignored)
   *   mcp.begin(bt);                   // BluetoothSerial (baud ignored)
   *
   * @param stream  Any Arduino Stream — Serial, WiFiClient, BluetoothSerial, etc.
   * @param baud    If > 0, calls stream.begin(baud) (only meaningful for HardwareSerial).
   */
  void begin(Stream& stream, unsigned long baud = 0);

  /**
   * Call from Arduino loop(). Reads and dispatches incoming JSON-RPC requests.
   */
  void loop();

  /**
   * Send a JSON-RPC 2.0 error response.
   * Standard codes: -32700 parse, -32600 invalid, -32601 not found, -32602 bad params.
   */
  void send_error(int id, int code, const char* message);
  void send_error(int id, int code, const __FlashStringHelper* message);

  /**
   * Send a JSON-RPC 2.0 success response.
   * Caller must have set doc["result"] before calling.
   */
  void send_result(int id, JsonDocument& doc);

private:
  // -------------------------------------------------------------------------
  // Internal types
  // -------------------------------------------------------------------------

  struct PinEntry {
    uint8_t       pin;
    const char*   name;
    McpPinType    type;
    const char*   description;
    McpPinOptions options;
    unsigned long last_sample_ms;
    uint8_t       buffer_index;
    bool          has_buffer;
  };

  struct ToolEntry {
    const char*    name;
    const char*    description;
    McpToolHandler handler;
  };

  // -------------------------------------------------------------------------
  // Data
  // -------------------------------------------------------------------------

  const char* _device_name;
  const char* _version;
  Stream*     _stream;

  PinEntry  _pins[MCP_MAX_PINS];
  uint8_t   _pin_count;

  ToolEntry _tools[MCP_MAX_TOOLS];
  uint8_t   _tool_count;

  McpPinStats   _stats[MCP_MAX_PINS];
  McpRingBuffer _buffer_pool[MCP_MAX_BUFFERED_PINS];
  uint8_t       _buffer_count;

  bool _has_summary_pin;
  bool _has_buffer_pin;
  bool _has_event_pin;

  char _buf[MCP_SERIAL_BUFFER];

  // -------------------------------------------------------------------------
  // Internal helpers
  // -------------------------------------------------------------------------

  void        _dispatch(char* buf);
  PinEntry*   _find_pin(uint8_t pin);
  PinEntry*   _find_pin_by_name(const char* name);
  ToolEntry*  _find_tool(const char* name);
  const char* _pin_type_str(McpPinType t);

  McpPinOptions _normalize_options(McpPinType type, const McpPinOptions& options);
  void _configure_hardware_pin(uint8_t pin, McpPinType type, const McpPinOptions& options);
  void _attach_buffer_if_needed(uint8_t pin_index, const McpPinOptions& options);
  void _process_sampling();
  float _read_pin_value(const PinEntry& p);
  bool _is_input_pin(McpPinType type);
  void _evaluate_event(const PinEntry& p, float value);

  // -------------------------------------------------------------------------
  // Built-in tool handlers
  // -------------------------------------------------------------------------

  void _handle_get_info(int id);
  void _handle_list_tools(int id);
  void _handle_gpio_write(int id, JsonObject params);
  void _handle_gpio_read(int id, JsonObject params);
  void _handle_pwm_write(int id, JsonObject params);
  void _handle_adc_read(int id, JsonObject params);
  void _handle_get_pin_summary(int id, JsonObject params);
  void _handle_get_pin_buffer(int id, JsonObject params);
  void _handle_get_pin_events(int id, JsonObject params);
};
