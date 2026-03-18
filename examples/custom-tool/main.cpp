/**
 * Example: Custom Tool Registration
 * Shows how to add custom RPC tools beyond the built-in GPIO set.
 * Demonstrates reading params and sending structured results.
 */

#include <McpIot.h>

McpDevice mcp("esp32-custom", "1.0.0");

// Tool: blink LED N times
void handle_blink(int id, JsonObject params) {
  int times = params["times"].as<int>();
  if (times <= 0 || times > 20) {
    mcp.send_error(id, -32602, "times must be between 1 and 20");
    return;
  }

  for (int i = 0; i < times; i++) {
    digitalWrite(2, HIGH);
    delay(200);
    digitalWrite(2, LOW);
    delay(200);
  }

  JsonDocument res;
  res["result"]["blinked"] = times;
  mcp.send_result(id, res);
}

// Tool: play tone on buzzer for N milliseconds
void handle_beep(int id, JsonObject params) {
  int freq_hz = params["freq"].is<int>() ? params["freq"].as<int>() : 1000;
  int duration_ms = params["duration"].is<int>() ? params["duration"].as<int>() : 500;

  if (duration_ms > 5000) {
    mcp.send_error(id, -32602, "duration must be <= 5000ms");
    return;
  }

  tone(5, freq_hz, duration_ms);

  JsonDocument res;
  res["result"]["freq"]     = freq_hz;
  res["result"]["duration"] = duration_ms;
  mcp.send_result(id, res);
}

void setup() {
  mcp.add_pin(2, "led",    MCP_DIGITAL_OUTPUT, "LED (GPIO 2)");
  mcp.add_pin(5, "buzzer", MCP_DIGITAL_OUTPUT, "Buzzer (GPIO 5)");

  mcp.add_tool("blink", "Blink the LED N times (1–20)", handle_blink);
  mcp.add_tool("beep",  "Play a tone on the buzzer",    handle_beep);

  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
