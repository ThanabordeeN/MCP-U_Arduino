/**
 * BasicSerial — McpIot example
 *
 * Exposes GPIO, PWM, and ADC pins over USB-Serial (115200 baud).
 * Connect with the mcpu-client or any JSON-RPC 2.0 client.
 *
 * Hardware (ESP32):
 *   GPIO 2  — built-in LED (digital output)
 *   GPIO 5  — buzzer      (digital output)
 *   GPIO 19 — PWM LED     (PWM output)
 *   GPIO 34 — light sensor (ADC input)
 */

#include <MCP-U.h>

McpDevice mcp("my-device", "1.0.0");

void setup() {
  mcp.add_pin(2,  "builtin_led",  MCP_DIGITAL_OUTPUT, "Built-in LED (GPIO 2)");
  mcp.add_pin(5,  "buzzer",       MCP_DIGITAL_OUTPUT, "Piezo buzzer (GPIO 5)");
  mcp.add_pin(19, "led_pwm",      MCP_PWM_OUTPUT,     "PWM LED (GPIO 19)");
  mcp.add_pin(34, "light_sensor", MCP_ADC_INPUT,      "Light sensor (GPIO 34)");

  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
