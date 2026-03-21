#pragma once

// Copy this file to config.h and fill in your values.

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

#define MQTT_HOST "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""
#define MQTT_CLIENT_ID "ld2410s-c3mini"
#define MQTT_TOPIC_PREFIX "sensors/ld2410s"

// LOLIN C3 Mini wiring:
// The onboard WS2812 RGB LED is on GPIO7.
// The sensor RX default is also GPIO7 — THIS IS A CONFLICT.
// To enable the LED: rewire sensor OT1/TX from GPIO7 to GPIO4,
// then update "Sensor RX Pin" to 4 via the AP portal.
//
// Recommended wiring (after rewiring):
//   ESP GPIO4  <- Sensor OT1/TX   (was GPIO7)
//   ESP GPIO6  -> Sensor RX
#define LD2410S_RX_PIN 4   // change to 4 after rewiring sensor OT1/TX to GPIO4
#define LD2410S_TX_PIN 6
#define LD2410S_BAUD 115200

// How often to publish JSON state, in milliseconds.
#define PUBLISH_INTERVAL_MS 500

// LD2410S sensor detection parameters (applied via writeGenericParams on boot)
#define SENSOR_FARTHEST_GATE   8       // 1-16, each gate = 0.7m → 8 gates = ~5.6m range
#define SENSOR_NEAREST_GATE    0       // 0-16, 0 = no minimum distance
#define SENSOR_UNMANNED_DELAY  15      // seconds (10-120): delay after last presence before reporting unoccupied
#define SENSOR_RESPONSE_SPEED  5       // 5=Normal, 10=Fast

// OTA update password (empty string = no password required)
#define OTA_PASSWORD ""

// WS2812 RGB LED pin (default GPIO8; change if your board uses a different pin)
#define LED_PIN_DEFAULT 8
