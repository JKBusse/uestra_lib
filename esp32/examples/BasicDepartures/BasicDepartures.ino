// Zeigt alle 30 Sekunden die nächsten Abfahrten einer Haltestelle auf dem Serial Monitor.
#include <UestraDepartures.h>
#include <WiFi.h>

const char *WIFI_SSID = "DEIN-WLAN";
const char *WIFI_PASS = "DEIN-PASSWORT";

// Haltestellen-ID (mit dem Beispiel StopSearch oder `uestra_abfahrten.py --suche` finden).
// Eine Steig-ID wie "de:03241:11:1:12" zeigt nur eine Richtung.
const char *STOP_ID = "de:03241:1807";  // Hannover, Stockholmer Allee

Uestra::Client uestra;

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  // Uhrzeit + Zeitzone: Pflicht, sonst scheitert die Zertifikatsprüfung.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  while (time(nullptr) < 1700000000) delay(250);

  // Optional: nur bestimmte Linien / Fahrtziele
  // uestra.setLineFilter("6,11");
  // uestra.setDirectionFilter("Hauptbahnhof");
}

void loop() {
  Uestra::Departure deps[8];
  size_t n = 0;
  Uestra::Error err = uestra.departures(STOP_ID, deps, 8, n);
  if (err != Uestra::Error::Ok) {
    Serial.printf("Fehler: %s (HTTP %d) %s\n", Uestra::errorName(err), uestra.lastHttpCode(),
                  uestra.lastErrorText());
  } else {
    time_t now = time(nullptr);
    for (size_t i = 0; i < n; i++) {
      const Uestra::Departure &d = deps[i];
      char hm[6];
      Uestra::formatClock(d.plannedSec, hm, sizeof hm);
      const char *status = "";
      char buf[24] = "";
      switch (d.status()) {
        case Uestra::Status::Cancelled: status = "FÄLLT AUS"; break;
        case Uestra::Status::Scheduled: status = "(Fahrplan)"; break;
        case Uestra::Status::OnTime: status = "pünktlich"; break;
        case Uestra::Status::Delayed:
          snprintf(buf, sizeof buf, "%+d min", d.delayMin());
          status = buf;
          break;
      }
      Serial.printf("%s  in %2d min  %-4s %-28s %s%s%s\n", hm, d.minutesUntil(now), d.line,
                    d.direction, status, d.platform[0] ? "  Gl. " : "", d.platform);
    }
    Serial.println();
  }
  delay(30000);  // nicht öfter abfragen - es ist eine inoffizielle Schnittstelle
}
