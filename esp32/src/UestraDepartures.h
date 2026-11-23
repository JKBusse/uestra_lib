// Üstra/GVH-Abfahrten für den ESP32 (Arduino-Framework).
//
// Inoffizielle HAFAS-Schnittstelle von gvh.hafas.de - kann sich jederzeit ändern.
// Voraussetzungen: WLAN verbunden, Uhrzeit per NTP gesetzt (sonst scheitert die
// Zertifikatsprüfung) und die Zeitzone gesetzt, z. B.
//   configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
#pragma once

#include "UestraCore.h"

#if defined(ARDUINO)
#include <WiFiClientSecure.h>

namespace Uestra {

class Client {
 public:
  Client();

  // Standard: Wurzelzertifikat aus UestraCerts.h. Eigenes Zertifikat oder gar keine Prüfung:
  void setCACert(const char *pem);
  void setInsecure();  // KEINE Zertifikatsprüfung - nur zum Ausprobieren

  // Lokale Filter (der Server kennt keine). Beide wirken auf departures().
  void setLineFilter(const char *commaList);  // z. B. "6,11"
  void setDirectionFilter(const char *text);  // Teilwort im Fahrtziel, ohne Groß/Klein
  // So viele Abfahrten werden bei aktivem Filter vom Server geholt (Standard 60, max 200).
  // Mehr = höhere Trefferquote, aber mehr RAM und Datenverkehr.
  void setFetchLimit(uint16_t n);
  void setTimeout(uint32_t ms);  // Standard 15000

  // Haltestellen suchen (bundesweit!). count = Anzahl Treffer in out.
  Error searchStops(const char *name, Stop *out, size_t cap, size_t &count);

  // Nächste Abfahrten ab jetzt. stopId: Haltestelle ("de:03241:1807") oder Steig-ID
  // ("de:03241:11:1:12", nur eine Richtung). Reicht der Tag nicht für `cap` Abfahrten,
  // wird der Folgetag ab 00:00 nachgeladen (braucht gesetzte Uhrzeit).
  Error departures(const char *stopId, Departure *out, size_t cap, size_t &count);

  int lastHttpCode() const { return _httpCode; }
  const char *lastErrorText() const { return _errText; }

 private:
  Error post(const JsonDocument &request, const JsonDocument &filter, JsonDocument &response);

  WiFiClientSecure _tls;
  Filter _filter;
  uint16_t _fetchLimit = 60;
  uint32_t _timeoutMs = 15000;
  int _httpCode = 0;
  char _errText[64] = "";
};

}  // namespace Uestra
#endif  // ARDUINO
