/**
 * WiFiTCP — MCP-U example
 *
 * Exposes GPIO, PWM, and ADC pins over WiFi TCP (port 3000).
 * Connect with the mcpu-client or any JSON-RPC 2.0 TCP client.
 *
 * Hardware (ESP32):
 *   GPIO 2  — built-in LED (digital output)
 *   GPIO 5  — buzzer      (digital output)
 *   GPIO 19 — PWM LED     (PWM output)
 *   GPIO 34 — light sensor (ADC input)
 *
 * mcpu-client connection:
 *   DEVICES=mydevice:192.168.1.x:3000:tcp npx mcpu-client
 */

#include <WiFi.h>
#include <MCP-U.h>

// ---------------------------------------------------------------------------
// Configuration — change these to match your network
// ---------------------------------------------------------------------------

static const char* WIFI_SSID     = "YOUR_SSID";
static const char* WIFI_PASSWORD = "YOUR_PASSWORD";
static const uint16_t TCP_PORT   = 3000;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

McpDevice  mcp("esp32-wifi", "1.0.0");
WiFiServer server(TCP_PORT);
WiFiClient client;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Register pins before begin()
  mcp.add_pin(2,  "builtin_led",  MCP_DIGITAL_OUTPUT, "Built-in LED (GPIO 2)");
  mcp.add_pin(5,  "buzzer",       MCP_DIGITAL_OUTPUT, "Piezo buzzer (GPIO 5)");
  mcp.add_pin(19, "led_pwm",      MCP_PWM_OUTPUT,     "PWM LED (GPIO 19)");
  mcp.add_pin(34, "light_sensor", MCP_ADC_INPUT,      "Light sensor (GPIO 34)");

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected — IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.print("TCP server listening on port ");
  Serial.println(TCP_PORT);
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
  // Accept new client if none connected
  if (!client || !client.connected()) {
    WiFiClient incoming = server.accept();
    if (incoming) {
      client = incoming;
      Serial.print("Client connected: ");
      Serial.println(client.remoteIP());
      mcp.begin(client);
    }
  }

  mcp.loop();
}
