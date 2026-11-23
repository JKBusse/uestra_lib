# UestraDepartures – ESP32-Bibliothek

Echtzeit-Abfahrten der Üstra/GVH (Hannover) für den ESP32 (Arduino-Framework), basierend auf
der inoffiziellen HAFAS-Schnittstelle von `gvh.hafas.de`. Das Gegenstück zum
Python-Tool im Hauptordner; die Schnittstelle ist in
[../docs/schnittstelle.md](../docs/schnittstelle.md) beschrieben.

> **Status:** Der Kern (Parser, Zeitrechnung, Anfragen) ist mit echten Serverantworten auf dem
> PC getestet, der Client-Code gegen Attrappen des Arduino-Cores syntaxgeprüft. **Auf echter
> Hardware oder mit dem echten ESP32-Core wurde noch nichts gebaut oder ausgeführt.**

## Was sie kann

- Nächste Abfahrten einer Haltestelle oder eines einzelnen Steigs (eine Richtung)
- Verspätung, Ausfall, Gleis (inkl. Gleisänderung), „in n Minuten“
- Lokale Filter nach Linie und Fahrtziel
- Haltestellensuche
- Nach Mitternacht wird der Folgetag automatisch nachgeladen

Nicht enthalten (gibt es im Python-Tool): Fahrtverlauf, Verbindungssuche, Störungsmeldungen.

## Installation

PlatformIO (`platformio.ini`):

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    symlink://../pfad/zu/üstra/esp32   ; oder die Bibliothek nach lib/ kopieren
```

Arduino IDE: den Ordner `esp32/` als `UestraDepartures` nach `Arduino/libraries/` kopieren und
ArduinoJson (≥ 7) über den Bibliotheksverwalter installieren.

## Benutzung

```cpp
#include <UestraDepartures.h>
#include <WiFi.h>

Uestra::Client uestra;

void setup() {
  WiFi.begin("SSID", "PASSWORT");
  while (WiFi.status() != WL_CONNECTED) delay(250);
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");   // Pflicht, s. u.
  while (time(nullptr) < 1700000000) delay(250);
}

void loop() {
  Uestra::Departure deps[8];
  size_t n = 0;
  if (uestra.departures("de:03241:1807", deps, 8, n) == Uestra::Error::Ok)
    for (size_t i = 0; i < n; i++)
      Serial.printf("%s -> %s  in %d min (%+d)\n", deps[i].line, deps[i].direction,
                    deps[i].minutesUntil(time(nullptr)), deps[i].delayMin());
  delay(30000);
}
```

Vollständige Beispiele: [BasicDepartures](examples/BasicDepartures/BasicDepartures.ino) und
[StopSearch](examples/StopSearch/StopSearch.ino) (Haltestellen-IDs finden).

### API

| | |
|---|---|
| `Client::departures(stopId, out, cap, count)` | nächste Abfahrten; `stopId` = Haltestelle (`de:03241:1807`) oder Steig (`de:03241:11:1:12`) |
| `Client::searchStops(name, out, cap, count)` | Haltestellen suchen (bundesweit) |
| `Client::setLineFilter("6,11")` / `setDirectionFilter("Hbf")` | lokale Filter; Linie exakt, Ziel als Teilwort |
| `Client::setFetchLimit(n)` | so viele Abfahrten bei aktivem Filter vom Server holen (Standard 60, max. 200) |
| `Client::setCACert(pem)` / `setInsecure()` | eigenes Zertifikat bzw. Prüfung abschalten |
| `Client::lastHttpCode()`, `lastErrorText()` | Details zum letzten Fehler |
| `Departure` | `line`, `direction`, `platform`, `platformPlanned`, `plannedSec`, `realtimeSec`, `cancelled`, `delayMin()`, `status()`, `minutesUntil(now)`, `plannedTime()` |

Alle Funktionen geben `Uestra::Error` zurück (`Ok`, `Network`, `Http`, `Json`, `Server`).

## Wichtig

- **Uhrzeit und Zeitzone müssen gesetzt sein.** Ohne NTP schlägt die Zertifikatsprüfung fehl,
  und `plannedTime()`/`minutesUntil()` rechnen in der Zeitzone des Geräts.
- **Zertifikat:** Eingebettet ist das Wurzelzertifikat *USERTrust RSA Certification Authority*
  (gültig bis 18.01.2038), gegen das die Kette von `gvh.hafas.de` geprüft wurde. Ändert der
  Betreiber die Zertifikatskette, schlägt die Verbindung fehl – dann mit `setCACert()` ein
  neues Zertifikat setzen oder zum Testen `setInsecure()` nutzen.
- **Speicher:** Antworten werden beim Lesen mit einem ArduinoJson-Filter auf die benötigten
  Felder reduziert (bei den Testantworten auf rund ein Viertel). Trotzdem steigt der RAM-Bedarf
  mit `setFetchLimit()`. Genaue Verbrauchswerte auf dem ESP32 habe ich nicht gemessen.
- **Nicht zu oft abfragen.** Eine inoffizielle Schnittstelle; ein Abruf alle 30 Sekunden oder
  seltener reicht für eine Anzeige völlig.
- Der Server liefert nur Abfahrten bis 24:00 des angefragten Tages; die Bibliothek lädt bei
  Bedarf bis zu zwei weitere Tage nach (nur mit gesetzter Uhrzeit).

## Tests

```sh
cd test/host
make test           # Kern gegen echte Serverantworten (lädt ArduinoJson einmalig)
make check-client   # Syntaxcheck des ESP32-Clients gegen Attrappen des Cores
```

Die Tests laufen ohne ESP32 und ohne Netzwerk (außer dem einmaligen Download von ArduinoJson).

## Lizenz

MIT, siehe [../LICENSE](../LICENSE). Inoffiziell, ohne Verbindung zu Üstra, GVH oder HAFAS.
