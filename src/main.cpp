#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "config.h"
#include "config_manager.h"
#include "ap_portal.h"
#include "LD2410S.h"

// Boot button on LOLIN C3 Mini
static const uint8_t  BOOT_BUTTON_PIN     = 9;
static const uint32_t BOOT_BUTTON_HOLD_MS = 3000;

// LOLIN C3 Mini onboard LED — pin is runtime-configurable via AP portal.
// Default is GPIO8 (simple blue LED, active HIGH). Change in the portal
// under "LED-Pin" if your board uses a different pin.

// WiFi connection: timeout per attempt and max retries before AP fallback
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;
static const uint8_t  WIFI_MAX_RETRIES        = 3;

// MQTT: max consecutive connection failures before opening AP portal
static const uint8_t  MQTT_MAX_RETRIES        = 10;

LD2410S              sensor;
WiFiClient           wifiClient;
PubSubClient         mqttClient(wifiClient);

static AppConfig     cfg;
unsigned long        lastPublish      = 0;
bool                 streamingStarted = false;
bool                 otaInProgress    = false;
bool                 lastPresence     = false;  // track state changes for immediate publish

static bool           autoThresholdActive    = false;
static unsigned long  autoThresholdStartMs   = 0;
static unsigned long  lastSensorFrameMs      = 0;
static uint8_t        lastSensorTargetState  = 0;
static uint16_t       lastSensorDistance     = 0;

// ---------------------------------------------------------------------------
// LED helpers (simple active-HIGH GPIO LED)
// ---------------------------------------------------------------------------

static void ledOn()  { digitalWrite(cfg.ledPin, HIGH); }
static void ledOff() { digitalWrite(cfg.ledPin, LOW);  }

static void ledOk()         { ledOn(); }
static void ledConnecting() { ledOff(); }
static void ledError()      { ledOn(); }   // solid on for errors
static void ledOta()        { ledOn(); }
static void ledPresence()   { /* handled inline with brief flash */ }

// Attempts to connect to WiFi once. Returns true on success, false on timeout.
static bool tryConnectWiFi(bool useStaticIp) {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);

  if (useStaticIp && cfg.cachedIp != 0) {
    WiFi.config(IPAddress(cfg.cachedIp),
                IPAddress(cfg.cachedGateway),
                IPAddress(cfg.cachedSubnet),
                IPAddress(cfg.cachedDns));
  }

  if (cfg.wifiHidden) {
    // For hidden SSIDs the router won't beacon its SSID, so we must scan all
    // channels with a directed probe. Setting WIFI_ALL_CHANNEL_SCAN ensures
    // the driver does a full active scan before associating.
    wifi_config_t conf{};
    strlcpy(reinterpret_cast<char *>(conf.sta.ssid),     cfg.wifiSsid,     sizeof(conf.sta.ssid));
    strlcpy(reinterpret_cast<char *>(conf.sta.password), cfg.wifiPassword, sizeof(conf.sta.password));
    conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    esp_wifi_set_config(WIFI_IF_STA, &conf);
    esp_wifi_connect();
  } else {
    if (strlen(cfg.wifiPassword) == 0) WiFi.begin(cfg.wifiSsid);
    else WiFi.begin(cfg.wifiSsid, cfg.wifiPassword);
  }

  const uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() > deadline) return false;
    // Fail immediately on definitive errors (wrong password, SSID not found).
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) return false;
    delay(500);
  }
  return true;
}

static void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("[WiFi] Connecting...");
  ledConnecting();

  // First attempt: use cached static IP for a faster reconnect.
  bool usedStaticIp = (cfg.cachedIp != 0);
  if (usedStaticIp && tryConnectWiFi(true)) {
    saveIpCache((uint32_t)WiFi.localIP(),
                (uint32_t)WiFi.gatewayIP(),
                (uint32_t)WiFi.subnetMask(),
                (uint32_t)WiFi.dnsIP());
    cfg.cachedIp      = (uint32_t)WiFi.localIP();
    cfg.cachedGateway = (uint32_t)WiFi.gatewayIP();
    cfg.cachedSubnet  = (uint32_t)WiFi.subnetMask();
    cfg.cachedDns     = (uint32_t)WiFi.dnsIP();
    Serial.printf("[WiFi] Connected (cached IP) → %s\n", WiFi.localIP().toString().c_str());
    return;
  }

  // Static IP failed (or no cache) — clear cache and fall back to DHCP.
  // Reset the static IP config by disconnecting; tryConnectWiFi(false) will
  // call WiFi.begin() without a prior WiFi.config(), letting the stack use DHCP.
  if (usedStaticIp) {
    clearIpCache();
    cfg.cachedIp = 0;
    WiFi.disconnect(true);
    delay(100);
  }

  uint8_t retries = 0;
  while (!tryConnectWiFi(false)) {
    retries++;
    if (retries >= WIFI_MAX_RETRIES) {
      // Cannot reach the configured network — open AP portal so the user can
      // correct the credentials without needing a serial connection.
      runAPPortal(cfg); // never returns; restarts after save
    }
  }

  // DHCP succeeded — cache the new lease.
  saveIpCache((uint32_t)WiFi.localIP(),
              (uint32_t)WiFi.gatewayIP(),
              (uint32_t)WiFi.subnetMask(),
              (uint32_t)WiFi.dnsIP());
  cfg.cachedIp      = (uint32_t)WiFi.localIP();
  cfg.cachedGateway = (uint32_t)WiFi.gatewayIP();
  cfg.cachedSubnet  = (uint32_t)WiFi.subnetMask();
  cfg.cachedDns     = (uint32_t)WiFi.dnsIP();
  Serial.printf("[WiFi] Connected (DHCP) → %s\n", WiFi.localIP().toString().c_str());
}

static String sensorParamStateTopic(const char *name);
static String sensorParamCommandTopic(const char *name);
static String sensorButtonCommandTopic(const char *name);
static void publishSensorParamStatesFromValues(uint8_t farthestGate,
                                               uint8_t nearestGate,
                                               uint16_t unmannedDelay,
                                               uint8_t responseSpeed);
static bool readAndPublishSensorParamStates();
static bool applySensorParams(uint8_t farthestGate,
                              uint8_t nearestGate,
                              uint16_t unmannedDelay,
                              uint8_t responseSpeed,
                              bool persistConfig,
                              const char *ackTopic = nullptr,
                              const char *successMessage = nullptr);

static void connectMqtt() {
  if (mqttClient.connected()) return;
  ledConnecting();

  uint8_t retries = 0;
  while (!mqttClient.connected()) {
    bool ok = strlen(cfg.mqttUser) == 0
                  ? mqttClient.connect(cfg.mqttClientId)
                  : mqttClient.connect(cfg.mqttClientId, cfg.mqttUser, cfg.mqttPassword);

    if (!ok) {
      Serial.printf("[MQTT] connect failed, state=%d (retry %u/%u)\n",
                    mqttClient.state(), retries + 1, MQTT_MAX_RETRIES);
      retries++;
      if (retries >= MQTT_MAX_RETRIES) {
        Serial.println("[MQTT] max retries reached — opening AP portal to fix settings");
        runAPPortal(cfg); // never returns; restarts after save
      }
      delay(1000);
    } else {
      Serial.printf("[MQTT] Connected to %s:%u as '%s'\n",
                    cfg.mqttHost, cfg.mqttPort, cfg.mqttClientId);
      // Subscribe to legacy command topic and HA discovery command topics.
      mqttClient.subscribe((String(cfg.mqttTopicPrefix) + "/cmd").c_str());
      mqttClient.subscribe(sensorParamCommandTopic("farthest_gate").c_str());
      mqttClient.subscribe(sensorParamCommandTopic("nearest_gate").c_str());
      mqttClient.subscribe(sensorParamCommandTopic("unmanned_delay").c_str());
      mqttClient.subscribe(sensorParamCommandTopic("response_speed").c_str());
      mqttClient.subscribe(sensorButtonCommandTopic("get_params").c_str());
      mqttClient.subscribe(sensorButtonCommandTopic("auto_threshold").c_str());
      mqttClient.subscribe(sensorButtonCommandTopic("sensor_reset").c_str());
      mqttClient.publish((String(cfg.mqttTopicPrefix) + "/status").c_str(), "online", true);
      ledOk();
    }
  }
}

static bool startStreamingSafely() {
  // Enter config mode cleanly before issuing any config commands.
  // FW 1.1.x may not ACK commands — log warnings but continue rather than aborting.
  if (!sensor.stopStreaming()) {
    Serial.println("[Sensor] stopStreaming failed — continuing anyway (FW 1.1.x?)");
  }

  LD2410S_GenericParams params{};
  if (!sensor.readGenericParams(params)) {
    Serial.println("[Sensor] readGenericParams failed — using cfg defaults");
    // Fall back to configured values so we can still write and start streaming
    params.farthestGate      = cfg.sensorFarthestGate;
    params.nearestGate       = cfg.sensorNearestGate;
    params.unmannedDelay_s   = cfg.sensorUnmannedDelay;
    params.responseSpeed     = cfg.sensorResponseSpeed;
    params.statusReportFreq   = 4.0f;
    params.distanceReportFreq = 4.0f;
  }

  // Apply configured detection parameters
  params.farthestGate      = cfg.sensorFarthestGate;
  params.nearestGate       = cfg.sensorNearestGate;
  params.unmannedDelay_s   = cfg.sensorUnmannedDelay;
  params.responseSpeed     = cfg.sensorResponseSpeed;
  // Ensure frequencies are set so the sensor sends standard frames.
  if (params.statusReportFreq < 0.5f)   params.statusReportFreq   = 1.0f;
  if (params.distanceReportFreq < 0.5f) params.distanceReportFreq = 1.0f;

  if (!sensor.writeGenericParams(params)) {
    Serial.println("[Sensor] writeGenericParams failed — continuing anyway (FW 1.1.x?)");
  } else {
    Serial.printf("[Sensor] Params applied — farGate=%u nearGate=%u delay=%us speed=%u freq=%.1fHz\n",
                  params.farthestGate, params.nearestGate,
                  params.unmannedDelay_s, params.responseSpeed, params.statusReportFreq);
  }

  // switchOutputMode changes minimal→standard frames (gate energy data).
  // Give the sensor extra time to settle after writeGenericParams endConfig.
  delay(500);
  bool modeOk = false;
  for (uint8_t attempt = 0; attempt < 3 && !modeOk; attempt++) {
    modeOk = sensor.switchOutputMode(true);
    if (!modeOk) {
      Serial.printf("[Sensor] switchOutputMode attempt %u/3 failed, retrying...\n", attempt + 1);
      delay(300);
    }
  }
  if (modeOk) {
    Serial.println("[Sensor] Standard mode attempted (sensor may ignore on FW 1.1.x)");
  } else {
    Serial.println("[Sensor] switchOutputMode unsupported — gate energy unavailable");
  }

  // The sensor is already streaming after writeGenericParams called endConfig().
  // Calling sensor.startStreaming() here is wrong: it would send CMD_READ_GENERIC
  // to the streaming sensor (ACK timeout), fall back to hardcoded defaults, and
  // overwrite our configured params. Instead, drain the UART RX buffer so the
  // parser starts clean on the first complete frame.
  delay(300);
  while (Serial1.available()) Serial1.read();
  Serial.println("[Sensor] Streaming active — UART buffer flushed");
  return true;
}

// ---------------------------------------------------------------------------
// MQTT command handler
// ---------------------------------------------------------------------------

static void onMqttMessage(const char *topic, byte *payload, unsigned int length) {
  if (length == 0 || length > 127) return;

  char msg[128];
  memcpy(msg, payload, length);
  msg[length] = '\0';
  Serial.printf("[MQTT cmd] topic=%s msg=%s\n", topic, msg);

  const String ackTopic = String(cfg.mqttTopicPrefix) + "/cmd/ack";
  const String cmdTopic = String(cfg.mqttTopicPrefix) + "/cmd";
  const String farTopic = sensorParamCommandTopic("farthest_gate");
  const String nearTopic = sensorParamCommandTopic("nearest_gate");
  const String delayTopic = sensorParamCommandTopic("unmanned_delay");
  const String speedTopic = sensorParamCommandTopic("response_speed");
  const String getParamsTopic = sensorButtonCommandTopic("get_params");
  const String autoThresholdTopic = sensorButtonCommandTopic("auto_threshold");
  const String sensorResetTopic = sensorButtonCommandTopic("sensor_reset");
  const String receivedTopic = String(topic);

  auto publishCurrentParams = [&]() {
    LD2410S_GenericParams p{};
    if (sensor.readGenericParams(p)) {
      Serial.printf("[Sensor] get_params → farGate=%u nearGate=%u delay=%us speed=%u\n",
                    p.farthestGate, p.nearestGate, p.unmannedDelay_s, p.responseSpeed);
      char buf[200];
      snprintf(buf, sizeof(buf),
               "{\"farthestGate\":%u,\"nearestGate\":%u,\"unmannedDelay\":%u,\"responseSpeed\":%u}",
               p.farthestGate, p.nearestGate, p.unmannedDelay_s, p.responseSpeed);
      mqttClient.publish((String(cfg.mqttTopicPrefix) + "/params").c_str(), buf, true);
      publishSensorParamStatesFromValues(p.farthestGate, p.nearestGate, p.unmannedDelay_s, p.responseSpeed);
      mqttClient.publish(ackTopic.c_str(), "params published");
    } else {
      Serial.println("[Sensor] get_params FAILED");
      mqttClient.publish(ackTopic.c_str(), "get_params FAILED");
    }
  };

  auto handleSingleParamUpdate = [&](const char *which, long value) {
    if (strcmp(which, "farthest_gate") == 0) {
      if (value < 1 || value > 16) {
        mqttClient.publish(ackTopic.c_str(), "farthest_gate out of range (1-16)");
        return;
      }
      applySensorParams((uint8_t)value, cfg.sensorNearestGate, cfg.sensorUnmannedDelay, cfg.sensorResponseSpeed,
                        true, ackTopic.c_str(), "farthest_gate updated and saved");
      return;
    }
    if (strcmp(which, "nearest_gate") == 0) {
      if (value < 0 || value > 16) {
        mqttClient.publish(ackTopic.c_str(), "nearest_gate out of range (0-16)");
        return;
      }
      applySensorParams(cfg.sensorFarthestGate, (uint8_t)value, cfg.sensorUnmannedDelay, cfg.sensorResponseSpeed,
                        true, ackTopic.c_str(), "nearest_gate updated and saved");
      return;
    }
    if (strcmp(which, "unmanned_delay") == 0) {
      if (value < 10 || value > 120) {
        mqttClient.publish(ackTopic.c_str(), "unmanned_delay out of range (10-120)");
        return;
      }
      applySensorParams(cfg.sensorFarthestGate, cfg.sensorNearestGate, (uint16_t)value, cfg.sensorResponseSpeed,
                        true, ackTopic.c_str(), "unmanned_delay updated and saved");
      return;
    }
    if (strcmp(which, "response_speed") == 0) {
      if (value < 5 || value > 10) {
        mqttClient.publish(ackTopic.c_str(), "response_speed out of range (5-10)");
        return;
      }
      applySensorParams(cfg.sensorFarthestGate, cfg.sensorNearestGate, cfg.sensorUnmannedDelay, (uint8_t)value,
                        true, ackTopic.c_str(), "response_speed updated and saved");
    }
  };

  if (receivedTopic == farTopic || receivedTopic == nearTopic || receivedTopic == delayTopic || receivedTopic == speedTopic) {
    char *endPtr = nullptr;
    long value = strtol(msg, &endPtr, 10);
    if (endPtr == msg) {
      mqttClient.publish(ackTopic.c_str(), "invalid numeric payload");
      return;
    }
    if (receivedTopic == farTopic) handleSingleParamUpdate("farthest_gate", value);
    else if (receivedTopic == nearTopic) handleSingleParamUpdate("nearest_gate", value);
    else if (receivedTopic == delayTopic) handleSingleParamUpdate("unmanned_delay", value);
    else if (receivedTopic == speedTopic) handleSingleParamUpdate("response_speed", value);
    return;
  }

  if (receivedTopic == getParamsTopic) {
    publishCurrentParams();
    return;
  }

  if (receivedTopic == autoThresholdTopic) {
    bool ok = sensor.startAutoThreshold(2, 1, 120);
    // Always track calibration — FW 1.1.x executes the command even without ACK
    autoThresholdActive  = true;
    autoThresholdStartMs = millis();
    mqttClient.publish(ackTopic.c_str(), ok ? "auto threshold started (120s)" : "auto threshold started (120s, no ACK)");
    return;
  }

  if (receivedTopic == sensorResetTopic) {
    bool ok = sensor.factoryReset();
    // Always restart streaming — FW 1.1.x executes factory reset without sending ACK (ok=false)
    mqttClient.publish(ackTopic.c_str(), ok ? "sensor factory reset OK" : "sensor factory reset (no ACK — FW 1.1.x)");
    delay(500);
    streamingStarted = false;
    return;
  }

  if (receivedTopic != cmdTopic) return;

  if (strcmp(msg, "restart") == 0) {
    mqttClient.publish(ackTopic.c_str(), "restarting");
    delay(200);
    ESP.restart();

  } else if (strcmp(msg, "reset_config") == 0) {
    mqttClient.publish(ackTopic.c_str(), "config reset - entering AP portal");
    delay(200);
    resetConfig();
    ESP.restart();

  } else if (strcmp(msg, "sensor_reset") == 0) {
    bool ok = sensor.factoryReset();
    // Always restart streaming — FW 1.1.x executes factory reset without sending ACK (ok=false)
    mqttClient.publish(ackTopic.c_str(), ok ? "sensor factory reset OK" : "sensor factory reset (no ACK — FW 1.1.x)");
    delay(500);
    streamingStarted = false;

  } else if (strcmp(msg, "auto_threshold") == 0) {
    bool ok = sensor.startAutoThreshold(2, 1, 120);
    // Always track calibration — FW 1.1.x executes the command even without ACK
    autoThresholdActive  = true;
    autoThresholdStartMs = millis();
    mqttClient.publish(ackTopic.c_str(), ok ? "auto threshold started (120s)" : "auto threshold started (120s, no ACK)");

  } else if (strcmp(msg, "get_params") == 0) {
    publishCurrentParams();

  } else if (strncmp(msg, "set_params ", 11) == 0) {
    uint8_t  farGate  = cfg.sensorFarthestGate;
    uint8_t  nearGate = cfg.sensorNearestGate;
    uint16_t delay_s  = cfg.sensorUnmannedDelay;
    uint8_t  speed    = cfg.sensorResponseSpeed;
    sscanf(msg + 11, "farGate=%hhu nearGate=%hhu delay=%hu speed=%hhu",
           &farGate, &nearGate, &delay_s, &speed);

    if (farGate < 1 || farGate > 16) {
      mqttClient.publish(ackTopic.c_str(), "farGate out of range (1-16)");
    } else if (nearGate > 16) {
      mqttClient.publish(ackTopic.c_str(), "nearGate out of range (0-16)");
    } else if (delay_s < 10 || delay_s > 120) {
      mqttClient.publish(ackTopic.c_str(), "delay out of range (10-120)");
    } else if (speed < 5 || speed > 10) {
      mqttClient.publish(ackTopic.c_str(), "speed out of range (5-10)");
    } else {
      applySensorParams(farGate, nearGate, delay_s, speed, true, ackTopic.c_str(), "params updated and saved");
    }

  } else if (strcmp(msg, "debug_on") == 0) {
    sensor.setDebug(true);
    mqttClient.publish(ackTopic.c_str(), "sensor debug enabled - check serial output");

  } else if (strcmp(msg, "debug_off") == 0) {
    sensor.setDebug(false);
    mqttClient.publish(ackTopic.c_str(), "sensor debug disabled");

  } else if (strcmp(msg, "std_mode") == 0) {
    bool ok = false;
    for (uint8_t i = 0; i < 3 && !ok; i++) { ok = sensor.switchOutputMode(true); delay(300); }
    mqttClient.publish(ackTopic.c_str(), ok ? "standard mode enabled" : "switchOutputMode FAILED (unsupported?)");

  } else {
    mqttClient.publish(ackTopic.c_str(), "unknown command");
  }
}

// ---------------------------------------------------------------------------
// MQTT publish helpers
// ---------------------------------------------------------------------------

static void publishSensorInfo() {
  uint16_t major = 0, minor = 0, patch = 0;
  if (!sensor.readFirmwareVersion(major, minor, patch)) {
    Serial.println("[Sensor] readFirmwareVersion failed");
    return;
  }
  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"sensor_fw\":\"%u.%u.%u\",\"chip\":\"ESP32-C3\",\"client_id\":\"%s\"}",
           major, minor, patch, cfg.mqttClientId);
  mqttClient.publish((String(cfg.mqttTopicPrefix) + "/info").c_str(), buf, true);
  Serial.printf("[Sensor] Firmware: %u.%u.%u\n", major, minor, patch);
}

static void publishParams() {
  LD2410S_GenericParams p{};
  if (!sensor.readGenericParams(p)) {
    Serial.println("[Sensor] readGenericParams failed — params not published");
    return;
  }
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"farthestGate\":%u,\"nearestGate\":%u,\"unmannedDelay\":%u,\"responseSpeed\":%u,\"statusFreq\":%.1f}",
           p.farthestGate, p.nearestGate, p.unmannedDelay_s, p.responseSpeed, p.statusReportFreq);
  mqttClient.publish((String(cfg.mqttTopicPrefix) + "/params").c_str(), buf, true);
  Serial.println("[Sensor] Params published");
}

static String sensorParamStateTopic(const char *name) {
  return String(cfg.mqttTopicPrefix) + "/sensor_param/" + name + "/state";
}

static String sensorParamCommandTopic(const char *name) {
  return String(cfg.mqttTopicPrefix) + "/sensor_param/" + name + "/set";
}

static String sensorButtonCommandTopic(const char *name) {
  return String(cfg.mqttTopicPrefix) + "/button/" + name + "/press";
}

static void publishSensorParamStatesFromValues(uint8_t farthestGate,
                                               uint8_t nearestGate,
                                               uint16_t unmannedDelay,
                                               uint8_t responseSpeed) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u", farthestGate);
  mqttClient.publish(sensorParamStateTopic("farthest_gate").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", nearestGate);
  mqttClient.publish(sensorParamStateTopic("nearest_gate").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", unmannedDelay);
  mqttClient.publish(sensorParamStateTopic("unmanned_delay").c_str(), buf, true);
  snprintf(buf, sizeof(buf), "%u", responseSpeed);
  mqttClient.publish(sensorParamStateTopic("response_speed").c_str(), buf, true);
}

static bool readAndPublishSensorParamStates() {
  LD2410S_GenericParams p{};
  if (!sensor.readGenericParams(p)) {
    Serial.println("[Sensor] readGenericParams failed — sensor param states not published");
    return false;
  }
  publishSensorParamStatesFromValues(p.farthestGate, p.nearestGate, p.unmannedDelay_s, p.responseSpeed);
  return true;
}

static bool applySensorParams(uint8_t farthestGate,
                              uint8_t nearestGate,
                              uint16_t unmannedDelay,
                              uint8_t responseSpeed,
                              bool persistConfig,
                              const char *ackTopic,
                              const char *successMessage) {
  LD2410S_GenericParams p{};
  if (!sensor.readGenericParams(p)) {
    if (ackTopic) mqttClient.publish(ackTopic, "readGenericParams FAILED");
    return false;
  }

  p.farthestGate    = farthestGate;
  p.nearestGate     = nearestGate;
  p.unmannedDelay_s = unmannedDelay;
  p.responseSpeed   = responseSpeed;

  if (!sensor.writeGenericParams(p)) {
    if (ackTopic) mqttClient.publish(ackTopic, "writeGenericParams FAILED");
    return false;
  }

  cfg.sensorFarthestGate  = farthestGate;
  cfg.sensorNearestGate   = nearestGate;
  cfg.sensorUnmannedDelay = unmannedDelay;
  cfg.sensorResponseSpeed = responseSpeed;
  if (persistConfig) saveConfig(cfg);

  publishParams();
  publishSensorParamStatesFromValues(farthestGate, nearestGate, unmannedDelay, responseSpeed);
  if (ackTopic && successMessage) mqttClient.publish(ackTopic, successMessage);
  return true;
}

static String macNoSeparators() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return mac;
}

static String sanitizeId(String value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_' || c == '-') {
      out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    } else {
      out += '_';
    }
  }
  while (out.indexOf("__") >= 0) out.replace("__", "_");
  if (out.length() == 0) out = "device";
  return out;
}

static void publishDiscovery() {
  Serial.println("[MQTT] Publishing Home Assistant discovery...");
  const String base              = String(cfg.mqttTopicPrefix);
  const String macPretty         = WiFi.macAddress();
  const String macId             = macNoSeparators();
  const String clientSlug        = sanitizeId(String(cfg.mqttClientId));
  const String deviceId          = "ld2410s_" + macId;
  const String discoveryNode     = clientSlug + "_" + macId;
  const String deviceName        = strlen(cfg.mqttClientId) > 0 ? String(cfg.mqttClientId)
                                                                 : ("LD2410S-" + macId);
  const String availabilityTopic = base + "/status";
  const String deviceJson        =
      "{\"name\":\"" + deviceName + "\","
      "\"identifiers\":[\"" + deviceId + "\"],"
      "\"manufacturer\":\"Hi-Link\"," 
      "\"model\":\"LD2410S\"," 
      "\"connections\":[[\"mac\",\"" + macPretty + "\"]]}";

  struct DiscoveryDef {
    const char *component;
    const char *objectId;
    const char *name;
    const char *stateTopic;
    const char *valueTemplate;
    const char *deviceClass;
    const char *unit;
    bool isBinarySensor;
  } defs[] = {
    {"binary_sensor", "presence",     "Presence",        "presence", nullptr,                         "occupancy",       nullptr, true},
    {"sensor",        "distance_cm",  "Target Distance", "state",    "{{ value_json.distance_cm }}", "distance",        "cm",    false},
    {"sensor",        "target_state", "Target State",    "state",    "{{ value_json.target_state }}", nullptr,            nullptr, false},
    {"sensor",        "sensor_fw",    "Sensor Firmware", "info",     "{{ value_json.sensor_fw }}",   nullptr,            nullptr, false},
    {"sensor",        "rssi",         "WiFi RSSI",       "state",    "{{ value_json.rssi }}",        "signal_strength", "dBm",   false},
    {"sensor",        "uptime_s",     "Uptime",          "state",    "{{ value_json.uptime_s }}",    "duration",        "s",     false},
    {"sensor",        "auto_threshold_pct", "Auto-Threshold Progress", "auto_threshold_progress", "{{ value | int }}", nullptr, "%", false},
  };

  for (const auto &d : defs) {
    String topic   = "homeassistant/" + String(d.component) + "/" + discoveryNode + "/" + d.objectId + "/config";
    String payload = "{";
    payload += "\"name\":\"" + String(d.name) + "\",";
    payload += "\"object_id\":\"" + deviceId + "_" + String(d.objectId) + "\",";
    payload += "\"unique_id\":\"" + deviceId + "_" + String(d.objectId) + "\",";
    payload += "\"state_topic\":\"" + base + "/" + String(d.stateTopic) + "\",";
    if (d.isBinarySensor) {
      payload += "\"payload_on\":\"ON\",";
      payload += "\"payload_off\":\"OFF\",";
    } else if (d.valueTemplate) {
      payload += "\"value_template\":\"" + String(d.valueTemplate) + "\",";
    }
    payload += "\"availability_topic\":\"" + availabilityTopic + "\",";
    payload += "\"payload_available\":\"online\",";
    payload += "\"payload_not_available\":\"offline\",";
    payload += "\"device\":" + deviceJson;
    if (d.deviceClass) payload += ",\"device_class\":\"" + String(d.deviceClass) + "\"";
    if (d.unit)        payload += ",\"unit_of_measurement\":\"" + String(d.unit) + "\"";
    payload += "}";
    bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), true);
    Serial.printf("[MQTT] Discovery %s -> %s (%s)\n", d.objectId, topic.c_str(), ok ? "ok" : "FAILED");
  }

  struct NumberDef {
    const char *objectId;
    const char *name;
    const char *paramName;
    int minValue;
    int maxValue;
    int step;
    const char *icon;
  } numberDefs[] = {
    {"farthest_gate",  "Farthest Gate",  "farthest_gate",  1,  16, 1, "mdi:arrow-expand-horizontal"},
    {"nearest_gate",   "Nearest Gate",   "nearest_gate",   0,  16, 1, "mdi:arrow-collapse-horizontal"},
    {"unmanned_delay", "Unmanned Delay", "unmanned_delay", 10, 120, 1, "mdi:timer-outline"},
    {"response_speed", "Response Speed", "response_speed", 5,  10, 1, "mdi:speedometer"},
  };

  for (const auto &d : numberDefs) {
    String topic   = "homeassistant/number/" + discoveryNode + "/" + d.objectId + "/config";
    String payload = "{";
    payload += "\"name\":\"" + String(d.name) + "\",";
    payload += "\"object_id\":\"" + deviceId + "_" + String(d.objectId) + "\",";
    payload += "\"unique_id\":\"" + deviceId + "_" + String(d.objectId) + "\",";
    payload += "\"state_topic\":\"" + sensorParamStateTopic(d.paramName) + "\",";
    payload += "\"command_topic\":\"" + sensorParamCommandTopic(d.paramName) + "\",";
    payload += "\"command_template\":\"{{ value | int }}\",";
    payload += "\"retain\":false,";
    payload += "\"mode\":\"box\",";
    payload += "\"min\":" + String(d.minValue) + ",";
    payload += "\"max\":" + String(d.maxValue) + ",";
    payload += "\"step\":" + String(d.step) + ",";
    payload += "\"availability_topic\":\"" + availabilityTopic + "\",";
    payload += "\"payload_available\":\"online\",";
    payload += "\"payload_not_available\":\"offline\",";
    payload += "\"icon\":\"" + String(d.icon) + "\",";
    payload += "\"device\":" + deviceJson;
    payload += "}";
    bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), true);
    Serial.printf("[MQTT] Discovery %s -> %s (%s)\n", d.objectId, topic.c_str(), ok ? "ok" : "FAILED");
  }

  struct ButtonDef {
    const char *objectId;
    const char *name;
    const char *commandName;
    const char *icon;
  } buttonDefs[] = {
    {"get_params",     "Get Params",     "get_params",     "mdi:download-network"},
    {"auto_threshold", "Auto Threshold", "auto_threshold", "mdi:tune-vertical"},
    {"sensor_reset",   "Sensor Reset",   "sensor_reset",   "mdi:restart-alert"},
  };

  for (const auto &d : buttonDefs) {
    String topic   = "homeassistant/button/" + discoveryNode + "/" + d.objectId + "/config";
    String payload = "{";
    payload += "\"name\":\"" + String(d.name) + "\",";
    payload += "\"object_id\":\"" + deviceId + "_" + String(d.objectId) + "\",";
    payload += "\"unique_id\":\"" + deviceId + "_" + String(d.objectId) + "\",";
    payload += "\"command_topic\":\"" + sensorButtonCommandTopic(d.commandName) + "\",";
    payload += "\"payload_press\":\"PRESS\",";
    payload += "\"availability_topic\":\"" + availabilityTopic + "\",";
    payload += "\"payload_available\":\"online\",";
    payload += "\"payload_not_available\":\"offline\",";
    payload += "\"icon\":\"" + String(d.icon) + "\",";
    payload += "\"device\":" + deviceJson;
    payload += "}";
    bool ok = mqttClient.publish(topic.c_str(), payload.c_str(), true);
    Serial.printf("[MQTT] Discovery %s -> %s (%s)\n", d.objectId, topic.c_str(), ok ? "ok" : "FAILED");
  }
}

static void publishState() {
  const bool presence = sensor.presenceDetected();
  const int  rssi     = WiFi.RSSI();
  const unsigned long uptime = millis() / 1000;

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"presence\":%s,\"target_state\":%u,\"distance_cm\":%u,\"rssi\":%d,\"uptime_s\":%lu}",
           presence ? "true" : "false",
           sensor.targetState(), sensor.distance_cm(), rssi, uptime);

  Serial.printf("[Sensor] presence=%s state=%u dist=%ucm\n",
                presence ? "YES" : "no",
                sensor.targetState(), sensor.distance_cm());

  bool ok1 = mqttClient.publish((String(cfg.mqttTopicPrefix) + "/state").c_str(), payload, true);
  bool ok2 = mqttClient.publish((String(cfg.mqttTopicPrefix) + "/presence").c_str(),
                                 presence ? "ON" : "OFF", true);
  if (!ok1 || !ok2) {
    Serial.println("[MQTT] publish failed (buffer full or disconnected?)");
    ledError();
  } else if (presence) {
    ledOn();
  } else {
    ledOff();
  }
}

// ---------------------------------------------------------------------------
// Boot button helper
// ---------------------------------------------------------------------------

// Returns true if the boot button has been held for BOOT_BUTTON_HOLD_MS.
static bool bootButtonHeld() {
  if (digitalRead(BOOT_BUTTON_PIN) != LOW) return false;
  uint32_t held = 0;
  while (digitalRead(BOOT_BUTTON_PIN) == LOW && held < BOOT_BUTTON_HOLD_MS) {
    delay(50);
    held += 50;
  }
  return held >= BOOT_BUTTON_HOLD_MS;
}

static void handleBootButtonRuntimeReset() {
  static bool wasPressed = false;
  static bool resetTriggered = false;
  static uint32_t pressStartedAt = 0;

  const bool pressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);

  if (!pressed) {
    if (wasPressed && !resetTriggered) {
      Serial.println("[BTN] Boot button released before 3s — no reset");
    }
    wasPressed = false;
    resetTriggered = false;
    pressStartedAt = 0;
    return;
  }

  if (!wasPressed) {
    wasPressed = true;
    resetTriggered = false;
    pressStartedAt = millis();
    Serial.println("[BTN] Boot button pressed — hold 3s to reset config and enter AP mode");
    return;
  }

  if (!resetTriggered && (millis() - pressStartedAt >= BOOT_BUTTON_HOLD_MS)) {
    resetTriggered = true;
    Serial.println("[BTN] Boot button held for 3s — clearing config and rebooting into AP mode");
    ledError();
    resetConfig();
    delay(250);
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------
// OTA setup
// ---------------------------------------------------------------------------

static void setupOta() {
  ArduinoOTA.setHostname(cfg.mqttClientId);
  if (strlen(cfg.otaPassword) > 0) {
    ArduinoOTA.setPassword(cfg.otaPassword);
  }
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    ledOta();
    Serial.println("[OTA] Starting update...");
  });
  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    Serial.println("\n[OTA] Done — rebooting");
    ledOk();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] %u%%\r", progress * 100 / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    ledError();
    Serial.printf("[OTA] Error[%u]\n", error);
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] Ready — hostname: %s\n", cfg.mqttClientId);
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Always load config to populate compile-time defaults for the AP portal.
  // If no saved config exists, loadConfig() returns defaults from config.h.
  // The NVS NOT_FOUND log lines on first boot are cosmetic — defaults still apply.
  cfg = loadConfig();

  // Initialise LED pin as output; LED off during startup until WiFi connects.
  pinMode(cfg.ledPin, OUTPUT);
  ledOff();

  // Warn if the LED pin conflicts with the sensor RX pin.
  if (cfg.sensorRxPin == cfg.ledPin) {
    Serial.printf("[LED] WARNING: LED pin GPIO%u conflicts with sensor RX pin!\n"
                  "[LED] Change LED-Pin or Sensor RX Pin in the AP portal.\n",
                  cfg.ledPin);
  } else {
    Serial.printf("[LED] Pin: GPIO%u\n", cfg.ledPin);
  }

  // Enter AP portal if no config saved or boot button held on startup.
  if (!hasConfig() || bootButtonHeld()) {
    runAPPortal(cfg); // never returns — restarts after save
  }

  connectWiFi();

  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(10);      // default 15s → 10s
  mqttClient.setSocketTimeout(5);   // default 15s → 5s faster reconnect detection
  mqttClient.setServer(cfg.mqttHost, cfg.mqttPort);
  mqttClient.setCallback(onMqttMessage);
  connectMqtt();

  setupOta();

  Serial.printf("[Setup] WiFi IP: %s, MQTT: %s:%u\n",
                WiFi.localIP().toString().c_str(), cfg.mqttHost, cfg.mqttPort);

  // For LOLIN C3 Mini, RX pin is the ESP pin connected to sensor OT1/TX.
  if (!sensor.begin(Serial1, cfg.sensorRxPin, cfg.sensorTxPin, cfg.sensorBaud)) {
    Serial.println("[Sensor] begin() FAILED — check RX/TX pins and baud rate");
    ledError();
    while (true) {
      delay(1000);
    }
  }

  delay(200);
  publishSensorInfo();    // read firmware version before streaming starts
  streamingStarted  = startStreamingSafely();
  lastSensorFrameMs = millis();
  publishDiscovery();
  publishParams();
  readAndPublishSensorParamStates();
  mqttClient.publish((String(cfg.mqttTopicPrefix) + "/status").c_str(), "online", true);
  ledOk();
}

void loop() {
  ArduinoOTA.handle();
  if (otaInProgress) return; // suspend normal operation during OTA

  handleBootButtonRuntimeReset();

  connectWiFi();
  connectMqtt();
  mqttClient.loop();

  sensor.update();

  // --- Auto-threshold: monitor progress, restart streaming on completion ---
  if (autoThresholdActive) {
    const uint16_t      atProgress = sensor.autoThresholdProgress();
    const unsigned long atElapsed  = millis() - autoThresholdStartMs;
    static unsigned long lastProgressPub = 0;
    if (millis() - lastProgressPub >= 2000) {
      lastProgressPub = millis();
      char pbuf[8];
      snprintf(pbuf, sizeof(pbuf), "%u", atProgress / 100);
      mqttClient.publish((String(cfg.mqttTopicPrefix) + "/auto_threshold_progress").c_str(), pbuf);
    }
    if (atProgress >= 10000 || atElapsed > 130000UL) {
      autoThresholdActive = false;
      streamingStarted    = false;
      lastSensorFrameMs   = millis();
      mqttClient.publish((String(cfg.mqttTopicPrefix) + "/auto_threshold_progress").c_str(), "100");
      Serial.println("[Sensor] Auto threshold complete — restarting streaming");
    }
  }

  // --- Sensor watchdog: reset streaming if data has been silent too long ---
  if (streamingStarted && !autoThresholdActive) {
    const uint8_t  curState    = sensor.targetState();
    const uint16_t curDist     = sensor.distance_cm();
    const bool     curPresence = sensor.presenceDetected();
    if (curState != lastSensorTargetState || curDist != lastSensorDistance || curPresence != lastPresence) {
      lastSensorFrameMs    = millis();
      lastSensorTargetState = curState;
      lastSensorDistance    = curDist;
    }
    const unsigned long watchdogMs = max(300000UL, (unsigned long)cfg.sensorUnmannedDelay * 2000UL);
    if (millis() - lastSensorFrameMs > watchdogMs) {
      Serial.printf("[Sensor] Watchdog triggered (no activity for %lus) — restarting streaming\n",
                    (millis() - lastSensorFrameMs) / 1000UL);
      streamingStarted  = false;
      lastSensorFrameMs = millis();
    }
  }

  if (!streamingStarted) {
    streamingStarted = startStreamingSafely();
    if (streamingStarted) lastSensorFrameMs = millis();
  }

  const unsigned long now      = millis();
  const bool          presence = sensor.presenceDetected();

  // Publish immediately on presence state change for low latency.
  const bool changed = (presence != lastPresence);
  if (changed) {
    lastPresence = presence;
    lastPublish  = now;
    publishState();
  } else if (now - lastPublish >= cfg.publishIntervalMs) {
    lastPublish = now;
    publishState();
  }
}
