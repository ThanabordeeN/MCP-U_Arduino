/**
 * MCP/U Example — LCD 16x4 (I2C)
 * By 2edge.co — LGPL-3.0
 *
 * Lets Claude write text to a 16-column × 4-row I2C LCD.
 *
 * Hardware:
 *   LCD 16x4 with I2C backpack → SDA=21, SCL=22
 *   Default I2C address: 0x27 (some modules use 0x3F)
 *
 * Dependencies (platformio.ini):
 *   lib_deps =
 *     bblanchon/ArduinoJson @ ^7
 *     marcoschwartz/LiquidCrystal_I2C @ ^1.1.4
 */

#include <MCP-U.h>
#include <LiquidCrystal_I2C.h>

McpDevice mcp("lcd-node", "1.0.0");
LiquidCrystal_I2C lcd(0x27, 16, 4);

void handle_show_text(int id, JsonObject params) {
  if (!params["text"].is<const char*>()) {
    mcp.send_error(id, -32602, "Required: text (string)");
    return;
  }

  int row = params["row"] | 0;  // 0–3, default 0
  int col = params["col"] | 0;  // 0–15, default 0

  if (row < 0 || row > 3 || col < 0 || col > 15) {
    mcp.send_error(id, -32602, "row must be 0-3, col must be 0-15");
    return;
  }

  lcd.setCursor(col, row);
  lcd.print(params["text"].as<const char*>());

  JsonDocument res;
  res["result"]["ok"] = true;
  mcp.send_result(id, res);
}

void handle_clear(int id, JsonObject params) {
  lcd.clear();

  JsonDocument res;
  res["result"]["ok"] = true;
  mcp.send_result(id, res);
}

void handle_set_backlight(int id, JsonObject params) {
  if (!params["on"].is<bool>()) {
    mcp.send_error(id, -32602, "Required: on (boolean)");
    return;
  }

  if (params["on"].as<bool>()) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }

  JsonDocument res;
  res["result"]["ok"] = true;
  mcp.send_result(id, res);
}

void setup() {
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  mcp.add_tool("show_text",      "Write text to LCD (params: text, row=0, col=0)", handle_show_text);
  mcp.add_tool("clear_display",  "Clear all LCD rows",                              handle_clear);
  mcp.add_tool("set_backlight",  "Turn backlight on or off (params: on)",           handle_set_backlight);
  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
