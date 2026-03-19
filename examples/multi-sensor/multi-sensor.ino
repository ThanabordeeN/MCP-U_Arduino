/**
 * Example: Multi-Sensor Device
 * LED + Buzzer (digital outputs) + two analog sensors + a custom tool.
 * Transport: Serial at 115200 baud.
 */

#include <MCP-U.h>

McpDevice mcp("esp32-sensors", "1.0.0");

// Custom tool: read temperature from a DS18B20 (simplified example)
void handle_read_temp(int id, JsonObject params) {
  // Replace with real sensor reading in production
  float fake_temp = 25.4f;

  JsonDocument res;
  res["result"]["celsius"]    = fake_temp;
  res["result"]["fahrenheit"] = (fake_temp * 9.0f / 5.0f) + 32.0f;
  mcp.send_result(id, res);
}

void setup() {
  mcp.add_pin(2,  "led",      MCP_DIGITAL_OUTPUT, "Status LED");
  mcp.add_pin(5,  "buzzer",   MCP_DIGITAL_OUTPUT, "Alert Buzzer");
  mcp.add_pin(34, "light",    MCP_ADC_INPUT,      "Light Sensor (LDR)");
  mcp.add_pin(35, "moisture", MCP_ADC_INPUT,      "Soil Moisture Sensor");

  mcp.add_tool("read_temp", "Read temperature from DS18B20 sensor", handle_read_temp);

  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
