#include "ap_portal.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <vector>
#include <algorithm>

static const char *AP_SSID = "LD2410S-Setup";
static const char *AP_PASSWORD = "ld2410s-setup";
static const IPAddress AP_IP(192,168, 4, 1);
static const uint8_t DNS_PORT = 53;

static DNSServer dnsServer;
static WebServer webServer(80);

// ---------------------------------------------------------------------------
// HTML helpers
// ---------------------------------------------------------------------------

static String htmlField(const char *label, const char *name, const char *value,
                         const char *type = "text", int maxlen = 63) {
  String s = "<label>";
  s += label;
  s += "<input type='";
  s += type;
  s += "' name='";
  s += name;
  s += "' value='";
  s += value;
  s += "' maxlength='";
  s += maxlen;
  s += "'></label>";
  return s;
}

static String htmlNumberField(const char *label, const char *name, uint32_t value,
                               uint32_t minVal = 0, uint32_t maxVal = 65535) {
  String s = "<label>";
  s += label;
  s += "<input type='number' name='";
  s += name;
  s += "' value='";
  s += value;
  s += "' min='";
  s += minVal;
  s += "' max='";
  s += maxVal;
  s += "'></label>";
  return s;
}

static String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

struct WifiNetworkInfo {
  String ssid;
  int32_t rssi;
  wifi_auth_mode_t authMode;
};

static std::vector<WifiNetworkInfo> scanWifiNetworks() {
  std::vector<WifiNetworkInfo> results;

  // Use already-completed async scan results (started in runAPPortal).
  int count = WiFi.scanComplete();
  if (count < 0) {
    // Scan not done yet — return empty list (caller should show loading state).
    return results;
  }

  for (int i = 0; i < count; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;

    bool duplicate = false;
    for (const auto &existing : results) {
      if (existing.ssid == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    results.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i)});
  }
  WiFi.scanDelete();

  std::sort(results.begin(), results.end(), [](const WifiNetworkInfo &a, const WifiNetworkInfo &b) {
    return a.rssi > b.rssi;
  });
  return results;
}

static String buildWifiOptions(const AppConfig &cfg) {
  String s;
  const auto networks = scanWifiNetworks();

  s += F("<label><span>Gefundene Netzwerke</span><select id='wifiNetworkSelect' onchange='applySelectedNetwork(this.value)'>");
  s += F("<option value=''>-- Netzwerk ausw&auml;hlen --</option>");

  for (const auto &network : networks) {
    const bool selected = network.ssid == String(cfg.wifiSsid);
    s += F("<option value='");
    s += htmlEscape(network.ssid);
    s += F("'");
    if (selected) s += F(" selected");
    s += F(">");
    s += htmlEscape(network.ssid);
    s += F(" (");
    s += network.rssi;
    s += F(" dBm, ");
    s += (network.authMode == WIFI_AUTH_OPEN ? F("offen") : F("gesichert"));
    s += F(")</option>");
  }

  s += F("</select></label>");
  s += F("<p class='note'>Auswahl &uuml;bernimmt die SSID automatisch. F&uuml;r versteckte Netze SSID manuell eintragen und &quot;Verstecktes Netzwerk&quot; aktivieren.</p>");
  return s;
}

static String buildPage(const AppConfig &cfg, const char *message = nullptr, const char *errorMessage = nullptr, const char *extraScript = nullptr) {
  String html = R"rawhtml(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LD2410S Konfiguration</title>
<style>
  *{box-sizing:border-box}
  body{font-family:sans-serif;max-width:820px;margin:1.5rem auto;padding:0 1rem;background:#f0f2f5;color:#333}
  h1{font-size:1.3rem;margin:0 0 1rem}
  h2{font-size:.85rem;font-weight:700;text-transform:uppercase;letter-spacing:.04em;
     color:#0078d4;margin:0 0 .7rem;padding-bottom:.35rem;border-bottom:2px solid #0078d4}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:1rem;margin-bottom:1rem}
  .card{background:#fff;border-radius:8px;padding:1rem;box-shadow:0 1px 4px rgba(0,0,0,.1)}
  .full{grid-column:1/-1}
  label{display:flex;flex-direction:column;gap:.2rem;margin-bottom:.6rem;font-size:.82rem;color:#555}
  label.cb{flex-direction:row;align-items:center;gap:.5rem;color:#444}
  label span{font-weight:500}
  input,select{padding:.35rem .45rem;border:1px solid #ccc;border-radius:4px;
               font-size:.88rem;width:100%;background:#fafafa}
  input:focus,select:focus{outline:none;border-color:#0078d4;background:#fff}
  input[type=checkbox]{width:1rem;height:1rem;padding:0;border:none;flex-shrink:0}
  input[disabled]{background:#f0f0f0;color:#888;cursor:default}
  .note{font-size:.75rem;color:#999;margin:-.3rem 0 .6rem;line-height:1.4}
  .ipinfo{font-size:.82rem;color:#555;background:#f7f7f7;border-radius:4px;
          padding:.4rem .6rem;margin-bottom:.4rem}
  button{width:100%;padding:.75rem;background:#0078d4;color:#fff;border:none;
         border-radius:6px;font-size:1rem;font-weight:600;cursor:pointer;margin-top:.5rem}
  button:hover{background:#005fa3}
  .msg,.err{padding:.6rem .8rem;border-radius:6px;margin-bottom:1rem;font-size:.9rem}
  .msg{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
  .err{background:#f8d7da;color:#842029;border:1px solid #f5c2c7}
  @media(max-width:600px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<h1>&#x1F4F6; LD2410S Setup</h1>
)rawhtml";

  if (message) {
    html += "<div class='msg'>";
    html += message;
    html += "</div>";
  }
  if (errorMessage) {
    html += "<div class='err'>";
    html += errorMessage;
    html += "</div>";
  }

  html += "<form method='POST' action='/save'>";
  html += "<div class='grid'>";

  // ── Left column: WLAN ────────────────────────────────────────────────────
  html += "<div class='card'>";
  html += "<h2>WLAN</h2>";
  html += buildWifiOptions(cfg);
  html += htmlField("SSID", "wifiSsid", cfg.wifiSsid);
  html += htmlField("Passwort", "wifiPassword", cfg.wifiPassword, "password");
  html += String("<label class='cb'><input type='checkbox' name='wifiHidden' value='1'") +
          (cfg.wifiHidden ? " checked" : "") + "> Verstecktes Netzwerk</label>";
  html += "</div>";

  // ── Right column: MQTT ───────────────────────────────────────────────────
  html += "<div class='card'>";
  html += "<h2>MQTT</h2>";
  html += htmlField("Host / IP", "mqttHost", cfg.mqttHost);
  html += htmlNumberField("Port", "mqttPort", cfg.mqttPort, 1, 65535);
  html += htmlField("Benutzer (optional)", "mqttUser", cfg.mqttUser);
  html += htmlField("Passwort (optional)", "mqttPassword", cfg.mqttPassword, "password");
  html += htmlField("Client-ID", "mqttClientId", cfg.mqttClientId);
  html += htmlField("Topic-Prefix", "mqttTopicPrefix", cfg.mqttTopicPrefix);
  html += "</div>";

  // ── Left column: Sensor-Pins ─────────────────────────────────────────────
  html += "<div class='card'>";
  html += "<h2>Sensor-Pins</h2>";
  html += htmlNumberField("RX-Pin (ESP GPIO)", "sensorRxPin", cfg.sensorRxPin, 0, 21);
  html += htmlNumberField("TX-Pin (ESP GPIO)", "sensorTxPin", cfg.sensorTxPin, 0, 21);
  html += htmlNumberField("Baud-Rate", "sensorBaud", cfg.sensorBaud, 9600, 921600);
  html += "</div>";


  // ── Full-width: Sonstiges ─────────────────────────────────────────────────
  html += "<div class='card full'>";
  html += "<h2>Sonstiges</h2>";
  html += "<div class='grid' style='gap:.6rem;margin:0'>";
  html += "<p class='note' style='grid-column:1/-1'>Sensor-Parameter werden nach der Einrichtung über Home Assistant / MQTT Auto Discovery eingestellt.</p>";
  html += htmlNumberField("Publish-Intervall (ms)", "publishIntervalMs", cfg.publishIntervalMs, 100, 60000);
  html += htmlField("OTA-Passwort (leer = keins)", "otaPassword", cfg.otaPassword, "password");
  html += htmlNumberField("LED-Pin (GPIO)", "ledPin", cfg.ledPin, 0, 21);
  html += "</div>";
  html += "</div>";

  // ── Full-width: Gespeicherte IP ──────────────────────────────────────────
  html += "<div class='card full'>";
  html += "<h2>Gespeicherte IP</h2>";
  if (cfg.cachedIp != 0) {
    html += "<div class='ipinfo'>";
    html += IPAddress(cfg.cachedIp).toString();
    html += " &nbsp;|&nbsp; GW: ";
    html += IPAddress(cfg.cachedGateway).toString();
    html += "</div>";
    html += "<p class='note'>Wird beim n&auml;chsten Start als statische IP genutzt "
            "&mdash; wird nach DHCP-Fallback automatisch aktualisiert.</p>";
  } else {
    html += "<p class='note'>Noch keine IP gecacht &ndash; "
            "wird nach dem ersten erfolgreichen Verbindungsaufbau gespeichert.</p>";
  }
  html += "</div>";

  html += "</div>"; // .grid

  html += "<button type='submit'>&#x1F4BE; Speichern &amp; Neustart</button>";
  html += "</form>";
  html += R"rawhtml(<script>
function applySelectedNetwork(value){
  if(!value) return;
  var ssidInput=document.querySelector("input[name='wifiSsid']");
  var hiddenInput=document.querySelector("input[name='wifiHidden']");
  if(ssidInput) ssidInput.value=value;
  if(hiddenInput) hiddenInput.checked=false;
}
</script>)rawhtml";
  if (extraScript) {
    html += extraScript;
  }
  html += "</body></html>";
  return html;
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------

static AppConfig *s_cfg = nullptr;

static void handleRoot() {
  int scanState = WiFi.scanComplete();
  if (scanState == WIFI_SCAN_RUNNING) {
    // Show a brief loading page that auto-refreshes every 2 seconds.
    webServer.send(200, "text/html; charset=utf-8",
      F("<!DOCTYPE html><html lang='de'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='2'>"
        "<title>LD2410S Setup</title>"
        "<style>body{font-family:sans-serif;text-align:center;padding:3rem;background:#f0f2f5}"
        "p{font-size:1.1rem;color:#555}.spin{font-size:2rem;animation:r 1s linear infinite;display:inline-block}"
        "@keyframes r{to{transform:rotate(360deg)}}</style></head>"
        "<body><p class='spin'>&#x1F50D;</p>"
        "<p><strong>Scanne WLAN-Netzwerke &hellip;</strong></p>"
        "<p style='font-size:.85rem;color:#999'>Seite wird automatisch aktualisiert.</p>"
        "</body></html>"));
    return;
  }

  // Scan complete — build the full config page, then start a fresh async scan
  // so results are up to date when the user reloads.
  webServer.send(200, "text/html; charset=utf-8", buildPage(*s_cfg));
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true); // restart async scan for next page load
}

static void handleRestart() {
  webServer.send(200, "text/plain; charset=utf-8", "Restarting");
  delay(250);
  ESP.restart();
}

static void handleSave() {
  AppConfig newCfg = *s_cfg;
  char previousSsid[sizeof(newCfg.wifiSsid)];
  strlcpy(previousSsid, s_cfg->wifiSsid, sizeof(previousSsid));

  if (webServer.hasArg("wifiSsid"))
    strlcpy(newCfg.wifiSsid, webServer.arg("wifiSsid").c_str(), sizeof(newCfg.wifiSsid));
  if (webServer.hasArg("wifiPassword"))
    strlcpy(newCfg.wifiPassword, webServer.arg("wifiPassword").c_str(), sizeof(newCfg.wifiPassword));
  newCfg.wifiHidden = webServer.hasArg("wifiHidden") && webServer.arg("wifiHidden") == "1";
  if (webServer.hasArg("mqttHost"))
    strlcpy(newCfg.mqttHost, webServer.arg("mqttHost").c_str(), sizeof(newCfg.mqttHost));
  if (webServer.hasArg("mqttPort"))
    newCfg.mqttPort = (uint16_t)webServer.arg("mqttPort").toInt();
  if (webServer.hasArg("mqttUser"))
    strlcpy(newCfg.mqttUser, webServer.arg("mqttUser").c_str(), sizeof(newCfg.mqttUser));
  if (webServer.hasArg("mqttPassword"))
    strlcpy(newCfg.mqttPassword, webServer.arg("mqttPassword").c_str(), sizeof(newCfg.mqttPassword));
  if (webServer.hasArg("mqttClientId"))
    strlcpy(newCfg.mqttClientId, webServer.arg("mqttClientId").c_str(), sizeof(newCfg.mqttClientId));
  if (webServer.hasArg("mqttTopicPrefix"))
    strlcpy(newCfg.mqttTopicPrefix, webServer.arg("mqttTopicPrefix").c_str(), sizeof(newCfg.mqttTopicPrefix));
  if (webServer.hasArg("sensorRxPin"))
    newCfg.sensorRxPin = (uint8_t)webServer.arg("sensorRxPin").toInt();
  if (webServer.hasArg("sensorTxPin"))
    newCfg.sensorTxPin = (uint8_t)webServer.arg("sensorTxPin").toInt();
  if (webServer.hasArg("sensorBaud"))
    newCfg.sensorBaud = (uint32_t)webServer.arg("sensorBaud").toInt();
  if (webServer.hasArg("publishIntervalMs"))
    newCfg.publishIntervalMs = (uint32_t)webServer.arg("publishIntervalMs").toInt();
  if (webServer.hasArg("otaPassword"))
    strlcpy(newCfg.otaPassword, webServer.arg("otaPassword").c_str(), sizeof(newCfg.otaPassword));
  if (webServer.hasArg("ledPin"))
    newCfg.ledPin = (uint8_t)webServer.arg("ledPin").toInt();

  String error;
  if (strlen(newCfg.wifiSsid) == 0) error = F("Bitte eine WLAN-SSID eintragen.");
  else if (strlen(newCfg.mqttHost) == 0) error = F("Bitte einen MQTT-Host eintragen.");
  else if (strlen(newCfg.mqttClientId) == 0) error = F("Bitte eine Client-ID eintragen.");
  else if (strlen(newCfg.mqttTopicPrefix) == 0) error = F("Bitte ein MQTT-Topic-Prefix eintragen.");

  if (error.length()) {
    webServer.send(400, "text/html", buildPage(newCfg, nullptr, error.c_str()));
    return;
  }

  if (strcmp(previousSsid, newCfg.wifiSsid) != 0) {
    newCfg.cachedIp = 0;
    newCfg.cachedGateway = 0;
    newCfg.cachedSubnet = 0;
    newCfg.cachedDns = 0;
    clearIpCache();
  }

  saveConfig(newCfg);
  *s_cfg = newCfg;

  const uint32_t connectTimeoutMs = 15000;
  String message;
  String extraScript;

  WiFi.disconnect(false, true);
  delay(200);
  WiFi.mode(WIFI_AP_STA);
  if (strlen(newCfg.wifiPassword) == 0) WiFi.begin(newCfg.wifiSsid);
  else WiFi.begin(newCfg.wifiSsid, newCfg.wifiPassword);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < connectTimeoutMs) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    message = F("WLAN-Anmeldung erfolgreich. IP-Adresse: ");
    message += ip.toString();
    message += F(" &middot; Neustart wird eingeleitet &hellip;");
    Serial.printf("[AP] WiFi preview connected, IP=%s\n", ip.toString().c_str());
  } else {
    message = F("Konfiguration gespeichert. Innerhalb von 15 Sekunden konnte noch keine WLAN-IP ermittelt werden. Neustart wird trotzdem eingeleitet.");
    Serial.println("[AP] WiFi preview connection timed out before restart");
  }

  extraScript = F("<script>setTimeout(function(){fetch('/restart',{method:'GET',cache:'no-store'}).catch(function(){});},1200);</script>");
  webServer.send(200, "text/html; charset=utf-8", buildPage(newCfg, message.c_str(), nullptr, extraScript.c_str()));
}

// Captive portal: redirect all unknown requests to the AP root page.
static void handleCaptive() {
  String location = "http://";
  location += AP_IP.toString();
  location += "/";
  webServer.sendHeader("Location", location, true);
  webServer.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void runAPPortal(const AppConfig &currentCfg) {
  s_cfg = const_cast<AppConfig *>(&currentCfg);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  // Start async WiFi scan immediately so results are ready when the first
  // browser request arrives (avoids blocking the HTTP handler for ~3 s).
  WiFi.scanNetworks(true, true); // async=true, showHidden=true

  // DNS: answer all queries with our AP IP → captive portal
  dnsServer.start(DNS_PORT, "*", AP_IP);

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/restart", HTTP_GET, handleRestart);

  // Common captive portal detection endpoints
  webServer.on("/generate_204", handleCaptive);           // Android
  webServer.on("/hotspot-detect.html", handleCaptive);    // Apple
  webServer.on("/ncsi.txt", handleCaptive);               // Windows
  webServer.on("/connecttest.txt", handleCaptive);        // Windows
  webServer.onNotFound(handleCaptive);

  webServer.begin();

  // Block here — we never return from AP mode; the save handler restarts.
  while (true) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    delay(2);
  }
}
