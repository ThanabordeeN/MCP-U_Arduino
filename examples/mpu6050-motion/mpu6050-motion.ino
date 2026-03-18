/**
 * MCP/U Example — MPU6050 Gyroscope / Accelerometer
 * By 2edge.co — LGPL-3.0
 *
 * Exposes gyroscope angles and raw accelerometer data as MCP tools.
 *
 * Hardware:
 *   MPU6050 → SDA=21, SCL=22 (I2C address 0x68)
 *
 * Dependencies (platformio.ini):
 *   lib_deps =
 *     bblanchon/ArduinoJson @ ^7
 *     electroniccats/MPU6050 @ ^1.3.0
 */

#include <McpIot.h>
#include <MPU6050_light.h>

McpDevice mcp("motion-node", "1.0.0");
MPU6050 mpu(Wire);

void handle_read_angles(int id, JsonObject params) {
  mpu.update();
  JsonDocument res;
  res["result"]["angle_x"] = mpu.getAngleX();  // degrees
  res["result"]["angle_y"] = mpu.getAngleY();
  res["result"]["angle_z"] = mpu.getAngleZ();
  mcp.send_result(id, res);
}

void handle_read_accel(int id, JsonObject params) {
  mpu.update();
  JsonDocument res;
  res["result"]["accel_x"] = mpu.getAccX();  // g
  res["result"]["accel_y"] = mpu.getAccY();
  res["result"]["accel_z"] = mpu.getAccZ();
  mcp.send_result(id, res);
}

void setup() {
  Wire.begin(21, 22);

  byte status = mpu.begin();
  if (status != 0) {
    while (true) { delay(1000); }
  }

  mpu.calcOffsets();  // calibrate — keep device still for ~1s

  mcp.add_tool("read_angles", "Read gyroscope angles in degrees (x, y, z)", handle_read_angles);
  mcp.add_tool("read_accel",  "Read accelerometer in g-force (x, y, z)",    handle_read_accel);
  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();
}
