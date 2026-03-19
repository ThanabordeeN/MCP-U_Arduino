/**
 * MCP/U Example — BME280 Sensor
 * By 2edge.co — LGPL-3.0
 *
 * Exposes temperature, humidity, and pressure as a single MCP tool.
 *
 * Hardware:
 *   BME280 → SDA=21, SCL=22 (I2C address 0x76 or 0x77)
 *
 * Dependencies (platformio.ini):
 *   lib_deps =
 *     bblanchon/ArduinoJson @ ^7
 *     adafruit/Adafruit BME280 Library @ ^2.2.4
 *     adafruit/Adafruit Unified Sensor @ ^1.1.14
 */

#include <MCP-U.h>
#include <Adafruit_BME280.h>

McpDevice mcp("weather-node", "1.0.0");
Adafruit_BME280 bme;

void handle_read_bme280(int id, JsonObject params) {
  JsonDocument res;
  res["result"]["temperature"] = bme.readTemperature();        // °C
  res["result"]["humidity"]    = bme.readHumidity();           // %
  res["result"]["pressure"]    = bme.readPressure() / 100.0F;  // hPa
  mcp.send_result(id, res);
}

void setup() {
  Wire.begin(21, 22);

  if (!bme.begin(0x76)) {
    // Halt — no point starting MCP without sensor
    while (true) { delay(1000); }
  }

  mcp.add_tool("read_bme280", "Read temperature (°C), humidity (%), pressure (hPa)", handle_read_bme280);
  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
