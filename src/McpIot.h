/**
 * McpIot.h — MCP/U: The Unified Interface for AI-Ready Microcontrollers
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
#include <Wire.h>

// ---------------------------------------------------------------------------
// Limits (override via build flags if needed)
// ---------------------------------------------------------------------------

#ifndef MCP_MAX_PINS
#define MCP_MAX_PINS  16
#endif

#ifndef MCP_MAX_TOOLS
#define MCP_MAX_TOOLS 24
#endif

#ifndef MCP_SERIAL_BUFFER
#define MCP_SERIAL_BUFFER 512
#endif

// ---------------------------------------------------------------------------
// Pin types
// ---------------------------------------------------------------------------

enum McpPinType {
  MCP_DIGITAL_OUTPUT,
  MCP_DIGITAL_INPUT,
  MCP_PWM_OUTPUT,
  MCP_ADC_INPUT,
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
   * Enable built-in I2C tools (i2c_scan, i2c_write_reg, i2c_read_reg).
   * Call before begin().
   *
   * @param sda  SDA pin (use -1 for board default)
   * @param scl  SCL pin (use -1 for board default)
   * @param freq I2C clock frequency in Hz (default 100000)
   */
  void begin_i2c(int sda = -1, int scl = -1, uint32_t freq = 100000);

  /**
   * Call from Arduino loop(). Reads and dispatches incoming JSON-RPC requests.
   */
  void loop();

  /**
   * Send a JSON-RPC 2.0 error response.
   * Standard codes: -32700 parse, -32600 invalid, -32601 not found, -32602 bad params.
   */
  void send_error(int id, int code, const char* message);

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
    uint8_t    pin;
    const char* name;
    McpPinType type;
    const char* description;
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

  bool      _i2c_enabled;
  TwoWire*  _wire;

  // -------------------------------------------------------------------------
  // Internal helpers
  // -------------------------------------------------------------------------

  void        _dispatch(const String& input);
  PinEntry*   _find_pin(uint8_t pin);
  ToolEntry*  _find_tool(const char* name);
  const char* _pin_type_str(McpPinType t);

  // -------------------------------------------------------------------------
  // Built-in tool handlers
  // -------------------------------------------------------------------------

  void _handle_get_info(int id);
  void _handle_list_tools(int id);
  void _handle_gpio_write(int id, JsonObject params);
  void _handle_gpio_read(int id, JsonObject params);
  void _handle_pwm_write(int id, JsonObject params);
  void _handle_adc_read(int id, JsonObject params);
  void _handle_i2c_scan(int id);
  void _handle_i2c_write_reg(int id, JsonObject params);
  void _handle_i2c_read_reg(int id, JsonObject params);
};
