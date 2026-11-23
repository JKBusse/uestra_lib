// Sucht Haltestellen-IDs. Name im Serial Monitor eingeben (Zeilenende "Newline").
#include <UestraDepartures.h>
#include <WiFi.h>

const char *WIFI_SSID = "DEIN-WLAN";
const char *WIFI_PASS = "DEIN-PASSWORT";

Uestra::Client uestra;

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
  while (time(nullptr) < 1700000000) delay(250);
  Serial.println("Haltestellenname eingeben:");
}

void loop() {
  if (!Serial.available()) return;
  String name = Serial.readStringUntil('\n');
  name.trim();
  if (!name.length()) return;

  Uestra::Stop stops[10];
  size_t n = 0;
  Uestra::Error err = uestra.searchStops(name.c_str(), stops, 10, n);
  if (err != Uestra::Error::Ok) {
    Serial.printf("Fehler: %s (HTTP %d) %s\n", Uestra::errorName(err), uestra.lastHttpCode(),
                  uestra.lastErrorText());
    return;
  }
  for (size_t i = 0; i < n; i++) Serial.printf("%-22s %s\n", stops[i].id, stops[i].name);
  if (!n) Serial.println("Keine Treffer.");
}
