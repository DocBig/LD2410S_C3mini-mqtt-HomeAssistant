# Einstellungen und Parameter

Alle Einstellungen werden einmalig im Einrichtungsportal vorgenommen. Sensorparameter können danach direkt in Home Assistant angepasst werden.

---

## WLAN

| Einstellung | Beschreibung |
|---|---|
| WLAN-Netzwerk | Name deines Heimnetzwerks (SSID) |
| Passwort | Passwort des WLANs; bei offenen Netzen leer lassen |
| Verstecktes Netzwerk | Aktivieren, wenn dein Router den Netzwerknamen nicht aussendet |

---

## MQTT

| Einstellung | Beschreibung |
|---|---|
| Host / IP | IP-Adresse oder Hostname des MQTT-Brokers |
| Port | Standardmäßig `1883` |
| Benutzer / Passwort | Optional – nur wenn der Broker Zugangsdaten verlangt |
| Client-ID | Eindeutiger Name für dieses Gerät im MQTT-Broker |
| Topic-Prefix | Präfix für alle MQTT-Nachrichten dieses Geräts |

### Empfehlung bei mehreren Geräten

Für jedes Gerät müssen **Client-ID** und **Topic-Prefix** eindeutig sein, damit sich die Geräte nicht gegenseitig überschreiben.

Beispiele:

| Gerät | Client-ID | Topic-Prefix |
|---|---|---|
| Wohnzimmer | `ld2410s-wohnzimmer` | `ld2410s/wohnzimmer` |
| Flur | `ld2410s-flur` | `ld2410s/flur` |
| Büro | `ld2410s-buero` | `ld2410s/buero` |

---

## Sensor-Anschluss

Diese Werte entsprechen dem empfohlenen Verdrahtungsschema und müssen nur geändert werden, wenn abweichend verkabelt wurde.

| Einstellung | Beschreibung | Standard |
|---|---|---|
| RX-Pin | GPIO am ESP, verbunden mit OT1/TX des Sensors | 4 |
| TX-Pin | GPIO am ESP, verbunden mit RX des Sensors | 6 |
| Baudrate | Übertragungsgeschwindigkeit zum Sensor | 115200 |

---

## Sonstige Einstellungen

| Einstellung | Beschreibung | Standard |
|---|---|---|
| Publish-Intervall (ms) | Wie oft Messwerte gesendet werden | 500 ms |
| OTA-Passwort | Optionaler Schutz für Firmware-Updates über WLAN | (kein Passwort) |
| LED-Pin | GPIO der Status-LED | 8 |

---

## Sensorparameter (einstellbar in Home Assistant)

Diese Werte steuern das Verhalten des Radar-Sensors und können nach der Inbetriebnahme direkt in Home Assistant angepasst werden. Änderungen werden sofort übernommen und bleiben auch nach einem Neustart erhalten.

| Parameter | Bedeutung | Bereich | Standard |
|---|---|---|---|
| Farthest Gate | Maximale Erkennungsreichweite | 1–16 | 8 |
| Nearest Gate | Mindestabstand für die Erkennung | 0–16 | 0 |
| Unmanned Delay | Wartezeit bis „keine Anwesenheit" gemeldet wird | 10–120 s | 15 s |
| Response Speed | Reaktionsgeschwindigkeit | 5 oder 10 | 5 |

**Gate-Werte in Metern:**

Jeder Gate-Schritt entspricht ca. 0,7 Metern. Die tatsächliche Reichweite ergibt sich aus:

| Gate-Wert | Reichweite |
|---|---|
| 1 | ca. 0,7 m |
| 4 | ca. 2,8 m |
| 8 | ca. 5,6 m |
| 12 | ca. 8,4 m |
| 16 | ca. 11,2 m |

**Nearest Gate:** Wert `0` bedeutet, dass auch Personen direkt vor dem Sensor erkannt werden. Ein höherer Wert blendet alles unterhalb dieses Abstands aus.

**Unmanned Delay:** Wie lange der Sensor noch „Anwesenheit" meldet, nachdem keine Person mehr erkannt wird. Sinnvolle Werte: 15–30 s für schnelle Reaktion, 60–120 s wenn kurzes Verlassen des Raums ignoriert werden soll.

**Response Speed:** `5` = normaler Betrieb (empfohlen), `10` = schnellere Reaktion (kann zu Fehldetektionen führen).

### Schaltfläche „Get Params"

Liest die aktuell im Sensor gespeicherten Werte aus und aktualisiert die Anzeige in Home Assistant. Nützlich um zu prüfen, ob die eingestellten Werte korrekt übernommen wurden.

---

## MQTT-Befehle (für Fortgeschrittene)

Neben Home Assistant können Befehle auch direkt per MQTT gesendet werden.

**Topic:** `<prefix>/cmd`  
**Antwort:** `<prefix>/cmd/ack`

| Befehl | Wirkung |
|---|---|
| `restart` | Gerät neu starten |
| `reset_config` | Konfiguration löschen und Einrichtungsportal öffnen |
| `get_params` | Aktuelle Sensorparameter abrufen und auf `<prefix>/params` veröffentlichen |
| `set_params farGate=8 nearGate=0 delay=15 speed=5` | Sensorparameter setzen (alle Werte optional) |
| `auto_threshold` | Automatische Kalibrierung des Sensors starten (dauert ca. 120 s) |
| `sensor_reset` | Sensor auf Werkseinstellungen zurücksetzen |

---

## MQTT-Topics Übersicht

| Topic | Inhalt |
|---|---|
| `<prefix>/state` | JSON mit `presence`, `target_state`, `distance_cm`, `rssi`, `uptime_s` |
| `<prefix>/presence` | `ON` oder `OFF` |
| `<prefix>/info` | JSON mit `sensor_fw`, `chip`, `client_id` |
| `<prefix>/params` | JSON mit aktuellen Sensorparametern |
| `<prefix>/status` | `online` oder `offline` |
| `<prefix>/sensor_param/<name>/state` | Aktueller Wert eines Sensorparameters |
| `<prefix>/sensor_param/<name>/set` | Sensorparameter setzen (von Home Assistant) |
| `<prefix>/button/<name>/press` | Button-Aktion auslösen (von Home Assistant) |
| `<prefix>/cmd` | Befehle senden |
| `<prefix>/cmd/ack` | Antwort auf Befehle |

---

## Zurücksetzen

| Aktion | Wirkung |
|---|---|
| BOOT beim Einschalten halten | Einrichtungsportal öffnen (Konfiguration bleibt erhalten) |
| BOOT im Betrieb 3 s halten | Konfiguration löschen, Einrichtungsportal öffnen |
| MQTT-Befehl `reset_config` | Wie BOOT im Betrieb 3 s halten |
