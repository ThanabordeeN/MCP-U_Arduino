/**
 * Example: Auto-Polled Sensor Buffer (McpPolling)
 *
 * Shows how to declare a custom tool with McpPolling so the connected
 * client calls it automatically on a schedule — no env-var config needed.
 *
 * Pattern:
 *   1. Accumulate samples into a circular buffer in loop() (non-blocking)
 *   2. Handler dumps the buffer on demand
 *   3. McpPolling(ms) tells the client the suggested call interval
 *   4. Client stores buffer values as time-series observations in memory DB
 *
 * Hardware (ESP32):
 *   GPIO 4  — capacitive touch pin
 *   GPIO 34 — ADC input (ADC1, WiFi-safe)
 *
 * Transport: Serial (USB-UART) at 115200 baud
 *
 * PlatformIO lib_deps:
 *   bblanchon/ArduinoJson @ ^7
 *   ThanabordeeN/MCP-U_Arduino @ ^1.2.0
 */

#include <Arduino.h>
#include <MCP-U.h>

McpDevice mcp("polled-demo", "1.0.0");

// ---------------------------------------------------------------------------
// Touch sensor circular buffer
// ---------------------------------------------------------------------------

#define TOUCH_PIN          4
#define TOUCH_BUF_SIZE    20
#define TOUCH_INTERVAL_MS 200
#define TOUCH_THRESHOLD   40

static int      touch_buf[TOUCH_BUF_SIZE];
static uint8_t  touch_head  = 0;
static uint8_t  touch_count = 0;
static uint32_t last_touch_ms = 0;

// ---------------------------------------------------------------------------
// Handler — dumps the circular buffer
// The three fields (type, resource, sample_interval_ms) trigger the client's
// buffer → SQLite pipeline. Observations land in mcp_u_observations.
// ---------------------------------------------------------------------------

void handle_read_touch(int id, JsonObject params) {
  JsonDocument res;
  res["result"]["type"]               = "buffer";
  res["result"]["resource"]           = "touch_gpio4";   // metric name in DB
  res["result"]["sample_interval_ms"] = TOUCH_INTERVAL_MS;

  uint8_t n = touch_count;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t idx = (touch_head - n + i + TOUCH_BUF_SIZE) % TOUCH_BUF_SIZE;
    res["result"]["values"][i]  = touch_buf[idx];
    res["result"]["touched"][i] = (touch_buf[idx] < TOUCH_THRESHOLD);
  }

  mcp.send_result(id, res);
}

// ---------------------------------------------------------------------------
// ADC buffer (built-in pin sampling — no custom handler needed)
// McpBuffered tells the framework to handle sampling automatically.
// ---------------------------------------------------------------------------

// Registered via mcp.add_pin(..., McpBuffered(20, 500)) — see setup().

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Built-in pin with automatic buffer sampling
  mcp.add_pin(34, "adc_raw", MCP_ADC_INPUT, "Raw ADC on GPIO34",
              McpBuffered(20, 500));   // 20 samples, every 500 ms

  // Custom polled tool — McpPolling(2000) means client calls every 2 s
  mcp.add_tool(
    "read_touch",
    "Read capacitive touch sensor on GPIO4. Returns buffer of raw values and "
    "touched (bool) flags. resource=touch_gpio4, sample_interval_ms=200.",
    handle_read_touch,
    McpPolling(2000)
  );

  mcp.begin(Serial, 115200);
}

void loop() {
  mcp.loop();   // process incoming RPC requests

  // Fill touch buffer non-blocking — never use delay() here
  if (millis() - last_touch_ms >= TOUCH_INTERVAL_MS) {
    last_touch_ms = millis();
    touch_buf[touch_head] = touchRead(TOUCH_PIN);
    touch_head = (touch_head + 1) % TOUCH_BUF_SIZE;
    if (touch_count < TOUCH_BUF_SIZE) touch_count++;
  }
}
