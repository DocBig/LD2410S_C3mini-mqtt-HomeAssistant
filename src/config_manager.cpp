#include "config_manager.h"
#include "config.h"
#include <Preferences.h>

static const char *NVS_NS = "ld2410s";
static const char *KEY_SAVED = "saved";

bool hasConfig() {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, true)) return false;  // namespace absent = no config
  bool saved = prefs.getBool(KEY_SAVED, false);
  prefs.end();
  return saved;
}

AppConfig loadConfig() {
  AppConfig cfg;
  Preferences prefs;
  prefs.begin(NVS_NS, false);  // read-write creates namespace on first boot

  strlcpy(cfg.wifiSsid,       prefs.getString("wifiSsid",       WIFI_SSID).c_str(),           sizeof(cfg.wifiSsid));
  strlcpy(cfg.wifiPassword,   prefs.getString("wifiPassword",   WIFI_PASSWORD).c_str(),       sizeof(cfg.wifiPassword));
  cfg.wifiHidden =            prefs.getBool("wifiHidden",       false);
  strlcpy(cfg.mqttHost,       prefs.getString("mqttHost",       MQTT_HOST).c_str(),           sizeof(cfg.mqttHost));
  cfg.mqttPort =              prefs.getUShort("mqttPort",       MQTT_PORT);
  strlcpy(cfg.mqttUser,       prefs.getString("mqttUser",       MQTT_USER).c_str(),           sizeof(cfg.mqttUser));
  strlcpy(cfg.mqttPassword,   prefs.getString("mqttPassword",   MQTT_PASSWORD).c_str(),       sizeof(cfg.mqttPassword));
  strlcpy(cfg.mqttClientId,   prefs.getString("mqttClientId",   MQTT_CLIENT_ID).c_str(),      sizeof(cfg.mqttClientId));
  strlcpy(cfg.mqttTopicPrefix,prefs.getString("mqttTopicPfx",  MQTT_TOPIC_PREFIX).c_str(),   sizeof(cfg.mqttTopicPrefix));
  cfg.sensorRxPin =           prefs.getUChar("sensorRxPin",    LD2410S_RX_PIN);
  cfg.sensorTxPin =           prefs.getUChar("sensorTxPin",    LD2410S_TX_PIN);
  cfg.sensorBaud =            prefs.getULong("sensorBaud",     LD2410S_BAUD);
  cfg.sensorFarthestGate =    prefs.getUChar("sensorFarGate",  SENSOR_FARTHEST_GATE);
  cfg.sensorNearestGate =     prefs.getUChar("sensorNearGate", SENSOR_NEAREST_GATE);
  cfg.sensorUnmannedDelay =   prefs.getUShort("sensorDelay",   SENSOR_UNMANNED_DELAY);
  cfg.sensorResponseSpeed =   prefs.getUChar("sensorSpeed",    SENSOR_RESPONSE_SPEED);
  cfg.publishIntervalMs =     prefs.getULong("publishMs",      PUBLISH_INTERVAL_MS);
  strlcpy(cfg.otaPassword,    prefs.getString("otaPassword",   OTA_PASSWORD).c_str(),   sizeof(cfg.otaPassword));
  cfg.ledPin =                prefs.getUChar("ledPin",         LED_PIN_DEFAULT);
  cfg.cachedIp =              prefs.getULong("cachedIp",       0);
  cfg.cachedGateway =         prefs.getULong("cachedGW",       0);
  cfg.cachedSubnet =          prefs.getULong("cachedSN",       0);
  cfg.cachedDns =             prefs.getULong("cachedDNS",      0);

  prefs.end();
  return cfg;
}

void saveConfig(const AppConfig &cfg) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);

  prefs.putString("wifiSsid",      cfg.wifiSsid);
  prefs.putString("wifiPassword",  cfg.wifiPassword);
  prefs.putBool("wifiHidden",      cfg.wifiHidden);
  prefs.putString("mqttHost",      cfg.mqttHost);
  prefs.putUShort("mqttPort",      cfg.mqttPort);
  prefs.putString("mqttUser",      cfg.mqttUser);
  prefs.putString("mqttPassword",  cfg.mqttPassword);
  prefs.putString("mqttClientId",  cfg.mqttClientId);
  prefs.putString("mqttTopicPfx",  cfg.mqttTopicPrefix);
  prefs.putUChar("sensorRxPin",    cfg.sensorRxPin);
  prefs.putUChar("sensorTxPin",    cfg.sensorTxPin);
  prefs.putULong("sensorBaud",     cfg.sensorBaud);
  prefs.putUChar("sensorFarGate",  cfg.sensorFarthestGate);
  prefs.putUChar("sensorNearGate", cfg.sensorNearestGate);
  prefs.putUShort("sensorDelay",   cfg.sensorUnmannedDelay);
  prefs.putUChar("sensorSpeed",    cfg.sensorResponseSpeed);
  prefs.putULong("publishMs",      cfg.publishIntervalMs);
  prefs.putString("otaPassword",   cfg.otaPassword);
  prefs.putUChar("ledPin",         cfg.ledPin);
  prefs.putBool(KEY_SAVED,         true);

  prefs.end();
}

void resetConfig() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
}

void clearIpCache() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putULong("cachedIp",  0);
  prefs.putULong("cachedGW",  0);
  prefs.putULong("cachedSN",  0);
  prefs.putULong("cachedDNS", 0);
  prefs.end();
}

void saveIpCache(uint32_t ip, uint32_t gateway, uint32_t subnet, uint32_t dns) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putULong("cachedIp",  ip);
  prefs.putULong("cachedGW",  gateway);
  prefs.putULong("cachedSN",  subnet);
  prefs.putULong("cachedDNS", dns);
  prefs.end();
}
