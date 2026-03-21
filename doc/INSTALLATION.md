# Installation und Ersteinrichtung

Diese Anleitung führt durch die vollständige Inbetriebnahme: Verdrahtung, Firmware flashen und Erstkonfiguration.

---

## Was du brauchst

**Hardware:**
- ESP32-C3-Board, empfohlen: LOLIN C3 Mini
- HLK-LD2410S Radar-Sensor
- USB-Datenkabel (zum Flashen)
- Stromversorgung 3,3 V für den Sensor (oder direkt vom ESP-Board)

**Software:**
- Nur einen aktuellen Browser (Chrome oder Edge empfohlen) — mehr ist nicht nötig

---

## Schritt 1: Verdrahtung

Verbinde Sensor und ESP32-C3 wie folgt:

```text
LD2410S          ESP32-C3 (LOLIN C3 Mini)
──────────────────────────────────────────
VCC         →    3V3
GND         →    GND
OT1 / TX    →    GPIO 4   ← Sensor sendet Daten an den ESP
RX          →    GPIO 6   ← ESP sendet Befehle an den Sensor
```

Die Status-LED der Firmware liegt standardmäßig auf **GPIO 8**.

> **Hinweis zum LOLIN C3 Mini:** Dieses Board hat auf GPIO 7 eine interne RGB-LED, die nicht für die Sensor-Verdrahtung verwendet werden darf. Die Firmware nutzt deshalb GPIO 4 als Eingang vom Sensor und GPIO 8 für die Status-LED – diese Pins sind bereits als Standard eingestellt.

---

## Schritt 2: Firmware flashen

Die fertige Firmware-Datei liegt nach jedem Build im Projektordner unter:

```
bin/firmware.bin
```

Diese Datei enthält **Bootloader, Partitionstabelle und Applikation** in einem – sie wird direkt an Adresse `0x0` geflasht.

### Methode A: Browser (empfohlen, keine Software-Installation nötig)

## 🌐 Firmware flashen über ESPConnect

👉 https://thelastoutpostworkshop.github.io/ESPConnect/


1. ESP32 per USB verbinden

2. **„Verbinden“** klicken und Port auswählen

3. Links **„Flashtools“** auswählen

4. Unter **„Firmware flashen“**:

   * Datei: `firmware.bin`
   * Offset: `0x0`
   * Option **„Gesamten Flash vor dem Schreiben löschen“** anhaken (empfohlen)

5. **„Firmware flashen“** klicken

6. Gerät startet neu
   WLAN **LD2410S-Setup** erscheint

---

### Probleme?

Falls der Flash nicht startet:

* BOOT gedrückt halten
* RESET kurz drücken
* BOOT loslassen

---

### alternativ Methode B: Browser 

1. ESP32-C3 per USB mit dem Computer verbinden
2. Im Browser öffnen: **https://espressif.github.io/esptool-js/**
3. Auf **„Connect"** klicken und das ESP32-Gerät aus der Liste auswählen
4. Unter **„Flash Address"** den Wert `0x0` eintragen
5. Mit **„Choose File"** die Datei `bin/firmware.bin` auswählen
6. Auf **„Program"** klicken und warten bis „Leaving... Hard resetting..." erscheint

> Falls der ESP32 nicht erkannt wird: Beim Anschließen den BOOT-Button gedrückt halten, um den Flash-Modus zu aktivieren.

---

## Schritt 3: Erstkonfiguration

Nach dem Flashen startet das Gerät automatisch im Einrichtungsmodus.

### 3.1 Mit dem Gerät verbinden

1. Auf dem Handy oder Computer das WLAN **`LD2410S-Setup`** auswählen
   - Passwort: `ld2410s-setup`
2. Im Browser öffnen: **`http://192.168.4.1`**
   - Es erscheint das Einrichtungsportal

### 3.2 WLAN eintragen

| Feld | Beschreibung |
|---|---|
| WLAN-Netzwerk | Dein Heimnetzwerk aus der Liste auswählen oder manuell eintragen |
| Passwort | Passwort deines WLANs |
| Verstecktes Netzwerk | Nur aktivieren, wenn dein Router den Netzwerknamen nicht sichtbar aussendet |

### 3.3 MQTT eintragen

| Feld | Beschreibung | Beispiel |
|---|---|---|
| Host / IP | Adresse deines MQTT-Brokers (z. B. Home Assistant) | `192.168.1.10` |
| Port | Standardmäßig 1883 | `1883` |
| Benutzer | Optional – nur wenn dein Broker Zugangsdaten verlangt | |
| Passwort | Optional | |
| Client-ID | Eindeutiger Name für dieses Gerät | `ld2410s-wohnzimmer` |
| Topic-Prefix | Präfix für alle MQTT-Nachrichten dieses Geräts | `ld2410s/wohnzimmer` |

> **Mehrere Geräte:** Wenn du mehrere LD2410S-Sensoren betreibst, muss jedes Gerät eine eigene **Client-ID** und einen eigenen **Topic-Prefix** haben. Die Home-Assistant-Einträge werden automatisch nach MAC-Adresse getrennt.

### 3.4 Optionale Einstellungen

Diese Felder können leer gelassen werden – die Standardwerte funktionieren in den meisten Fällen.

| Feld | Beschreibung | Standard |
|---|---|---|
| Publish-Intervall (ms) | Wie oft Messwerte gesendet werden | 500 ms |
| OTA-Passwort | Schutz für Firmware-Updates über WLAN | (kein Passwort) |
| LED-Pin (GPIO) | GPIO der Status-LED | 8 |
| Sensor RX-Pin | GPIO, der mit OT1/TX des Sensors verbunden ist | 4 |
| Sensor TX-Pin | GPIO, der mit RX des Sensors verbunden ist | 6 |
| Baudrate | Übertragungsgeschwindigkeit zum Sensor | 115200 |

### 3.5 Speichern

1. Auf **„Speichern"** klicken
2. Das Gerät verbindet sich mit dem eingetragenen WLAN
3. Die bezogene IP-Adresse erscheint kurz auf der Seite
4. Das Gerät startet automatisch neu

> Falls eine Fehlermeldung erscheint (z. B. „Kein WLAN-Name eingetragen"), das betreffende Feld ausfüllen und erneut speichern.

---

## Schritt 4: Home Assistant

Nach dem Neustart verbindet sich das Gerät mit MQTT und erscheint automatisch in Home Assistant – ohne dass dort etwas manuell eingerichtet werden muss.

Unter **Einstellungen → Geräte & Dienste → MQTT** taucht das neue Gerät auf.

Eine fertige Dashboard-Vorlage liegt in:

- `doc/HOME_ASSISTANT_DASHBOARD.yaml`

Wie du diese einrichtest, steht in [`doc/HOME_ASSISTANT_DASHBOARD.md`](HOME_ASSISTANT_DASHBOARD.md).

---

## Verhalten im Betrieb

- **Status-LED an (GPIO 8):** WLAN und MQTT verbunden, Sensor aktiv
- **Status-LED aus:** Gerät verbindet sich gerade (normal kurz nach dem Start)

Beim ersten Start nach dem Flashen kann die Verbindung einige Sekunden dauern.

---

## Wiederherstellung bei Problemen

### WLAN-Zugangsdaten falsch

Falls das Gerät nicht ins WLAN kommt, öffnet es nach 3 Versuchen automatisch wieder das Einrichtungsportal. Einfach mit `LD2410S-Setup` verbinden und die Zugangsdaten korrigieren.

### BOOT-Button beim Einschalten gedrückt halten

Das Einrichtungsportal öffnet sich sofort, ohne die bestehende Konfiguration zu löschen. Hilfreich, um einzelne Einstellungen zu ändern.

### BOOT-Button im Betrieb 3 Sekunden gedrückt halten

- Die gesamte Konfiguration wird gelöscht
- Das Gerät startet neu und öffnet das Einrichtungsportal
- Anschließend wie bei der Ersteinrichtung vorgehen

---

