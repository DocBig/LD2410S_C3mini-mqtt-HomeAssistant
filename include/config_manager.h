#pragma once

#include <Arduino.h>

struct AppConfig {
  // WiFi
  char wifiSsid[64];
  char wifiPassword[64];
  bool wifiHidden;

  // MQTT
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[64];
  char mqttPassword[64];
  char mqttClientId[64];
  char mqttTopicPrefix[64];

  // Sensor
  uint8_t sensorRxPin;
  uint8_t sensorTxPin;
  uint32_t sensorBaud;

  // LD2410S detection parameters (applied via writeGenericParams on boot)
  uint8_t  sensorFarthestGate;   // 1-16, detection range (each gate = 0.7m)
  uint8_t  sensorNearestGate;    // 0-16, minimum detection distance
  uint16_t sensorUnmannedDelay;  // 10-120 seconds: hold time after last presence
  uint8_t  sensorResponseSpeed;  // 5=Normal, 10=Fast

  // Publish interval
  uint32_t publishIntervalMs;

  // OTA
  char otaPassword[64];

  // LED
  uint8_t ledPin;

  // Cached DHCP lease (automatically filled after each successful connection).
  // All zero means no cache — DHCP will be used.
  uint32_t cachedIp;
  uint32_t cachedGateway;
  uint32_t cachedSubnet;
  uint32_t cachedDns;
};

// Returns true if a configuration has been saved to NVS.
bool hasConfig();

// Loads config from NVS; missing keys fall back to config.h compile-time defaults.
AppConfig loadConfig();

// Persists all fields of cfg to NVS.
void saveConfig(const AppConfig &cfg);

// Erases the NVS namespace so the device enters AP mode on next boot.
void resetConfig();

// Clears the cached IP address fields in NVS (forces DHCP on next boot).
void clearIpCache();

// Saves the current DHCP lease (ip/gateway/subnet/dns) to NVS.
void saveIpCache(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns);
