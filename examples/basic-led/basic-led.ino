/**
 * Example: Basic LED Control
 * Minimal firmware — LED on GPIO 2, controlled via MCP-IoT over Serial.
 */

#include <MCP-U.h>

McpDevice mcp("esp32-led", "1.0.0");

void setup() {
  mcp.add_pin(2, "led", MCP_DIGITAL_OUTPUT, "Onboard LED");
  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
