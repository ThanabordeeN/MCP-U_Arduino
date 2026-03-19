/**
 * MCP/U Example — SSD1306 OLED Display
 * By 2edge.co — LGPL-3.0
 *
 * Lets Claude write text directly to a 128×64 OLED screen.
 *
 * Hardware:
 *   SSD1306 → SDA=21, SCL=22 (I2C address 0x3C)
 *
 * Dependencies (platformio.ini):
 *   lib_deps =
 *     bblanchon/ArduinoJson @ ^7
 *     adafruit/Adafruit SSD1306 @ ^2.5.9
 *     adafruit/Adafruit GFX Library @ ^1.11.9
 */

#include <MCP-U.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

McpDevice mcp("display-node", "1.0.0");
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

void handle_show_text(int id, JsonObject params) {
  if (!params["text"].is<const char*>()) {
    mcp.send_error(id, -32602, "Required: text (string)");
    return;
  }

  const char* text = params["text"].as<const char*>();
  int size         = params["size"] | 1;  // font size, default 1

  display.clearDisplay();
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(text);
  display.display();

  JsonDocument res;
  res["result"]["ok"] = true;
  mcp.send_result(id, res);
}

void handle_clear(int id, JsonObject params) {
  display.clearDisplay();
  display.display();

  JsonDocument res;
  res["result"]["ok"] = true;
  mcp.send_result(id, res);
}

void setup() {
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) { delay(1000); }
  }

  display.clearDisplay();
  display.display();

  mcp.add_tool("show_text", "Display text on OLED (params: text, size=1)", handle_show_text);
  mcp.add_tool("clear_display", "Clear the OLED screen", handle_clear);
  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
