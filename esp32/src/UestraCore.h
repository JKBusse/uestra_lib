// Plattformunabhängiger Kern: Datentypen, Zeit-Hilfen, Anfragen bauen, Antworten parsen.
// Braucht nur ArduinoJson (>= 7) und die C-Standardbibliothek, läuft also auch auf dem PC
// (siehe test/host). Die Netzwerkschicht für den ESP32 steht in UestraDepartures.h.
#pragma once

#include <ArduinoJson.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace Uestra {

constexpr const char *kEndpoint = "https://gvh.hafas.de/hamm";
constexpr const char *kProtocolVersion = "1.62";

enum class Error : int8_t {
  Ok = 0,
  Network = -1,   // keine Verbindung / Timeout
  Http = -2,      // HTTP-Status != 200 (siehe Client::lastHttpCode())
  Json = -3,      // Antwort ist kein gültiges JSON (oder zu groß für den Speicher)
  Server = -4,    // HAFAS meldet einen Fehler (Text: Client::lastErrorText())
};

inline const char *errorName(Error e) {
  switch (e) {
    case Error::Ok: return "ok";
    case Error::Network: return "network";
    case Error::Http: return "http";
    case Error::Json: return "json";
    case Error::Server: return "server";
  }
  return "?";
}

// ----------------------------------------------------------------------------- Zeiten

// HAFAS-Zeit "HHMMSS" oder "DDHHMMSS" (DD = Tagesversatz) -> Sekunden ab 00:00 des Datums.
inline bool parseHafasTime(const char *t, int32_t &sec) {
  if (!t) return false;
  size_t n = strlen(t);
  if (n != 6 && n != 8) return false;
  for (size_t i = 0; i < n; i++)
    if (t[i] < '0' || t[i] > '9') return false;
  auto two = [&](size_t i) { return (t[i] - '0') * 10 + (t[i + 1] - '0'); };
  int days = 0;
  size_t o = 0;
  if (n == 8) { days = two(0); o = 2; }
  sec = days * 86400 + two(o) * 3600 + two(o + 2) * 60 + two(o + 4);
  return true;
}

// "YYYYMMDD" + Sekunden ab 00:00 -> time_t. Rechnet in der Zeitzone der Geräte-Uhr, dafür
// vorher configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ...) aufrufen.
inline time_t toTimeT(const char *date, int32_t sec) {
  if (!date || strlen(date) != 8) return 0;
  char y[5] = {date[0], date[1], date[2], date[3], 0};
  char m[3] = {date[4], date[5], 0};
  char d[3] = {date[6], date[7], 0};
  struct tm t;
  memset(&t, 0, sizeof t);
  t.tm_year = atoi(y) - 1900;
  t.tm_mon = atoi(m) - 1;
  t.tm_mday = atoi(d);
  t.tm_sec = sec;  // mktime normalisiert Werte >= 60 (auch über Mitternacht)
  t.tm_isdst = -1;
  return mktime(&t);
}

// Sekunden ab 00:00 -> "HH:MM" (buf mindestens 6 Zeichen).
inline void formatClock(int32_t sec, char *buf, size_t len) {
  if (len < 6) { if (len) buf[0] = 0; return; }
  int h = (sec / 3600) % 24, m = (sec / 60) % 60;
  buf[0] = '0' + h / 10; buf[1] = '0' + h % 10; buf[2] = ':';
  buf[3] = '0' + m / 10; buf[4] = '0' + m % 10; buf[5] = 0;
}

// ----------------------------------------------------------------------------- Typen

struct Stop {
  char id[32];    // z. B. "de:03241:1807"
  char name[56];  // z. B. "Hannover, Stockholmer Allee"
};

enum class Status : uint8_t { Scheduled, OnTime, Delayed, Cancelled };

struct Departure {
  char line[12];             // "6", "RE70", "S4"
  char direction[40];        // Fahrtziel
  char platform[12];         // Gleis (aktuell), leer bei Stadtbahn/Bus
  char platformPlanned[12];  // ursprüngliches Gleis, nur gesetzt wenn geändert
  char stopId[32];           // Steig-/Mast-ID der Abfahrt
  char date[9];              // "YYYYMMDD" des Fahrplantags
  int32_t plannedSec;        // Plan-Abfahrt, Sekunden ab 00:00 von `date` (kann > 86400 sein)
  int32_t realtimeSec;       // Echtzeit-Abfahrt wie plannedSec, -1 = keine Echtzeitdaten
  bool cancelled;

  bool hasRealtime() const { return realtimeSec >= 0; }
  // Verspätung in Minuten (auf Minuten abgeschnitten, damit 03:37:40 nicht als +1 zählt).
  int delayMin() const { return hasRealtime() ? realtimeSec / 60 - plannedSec / 60 : 0; }
  Status status() const {
    if (cancelled) return Status::Cancelled;
    if (!hasRealtime()) return Status::Scheduled;
    return delayMin() == 0 ? Status::OnTime : Status::Delayed;
  }
  time_t plannedTime() const { return toTimeT(date, plannedSec); }
  time_t actualTime() const { return toTimeT(date, hasRealtime() ? realtimeSec : plannedSec); }
  // Minuten bis zur (Echtzeit-)Abfahrt; negativ wenn sie schon war.
  int minutesUntil(time_t now) const {
    long d = (long)(actualTime() - now);
    return (int)(d >= 0 ? d / 60 : -((-d + 59) / 60));
  }
};

// ----------------------------------------------------------------------------- Filter

// Der Server ignoriert Linien-/Richtungsfilter, deshalb wird lokal gefiltert.
struct Filter {
  char lines[48];      // kommagetrennt, z. B. "6,11"; leer = alle
  char direction[32];  // Teilwort im Fahrtziel; leer = alle

  Filter() { lines[0] = 0; direction[0] = 0; }
  bool active() const { return lines[0] || direction[0]; }
  bool matches(const char *line, const char *dir) const;
};

inline void copyStr(char *dst, size_t size, const char *src) {
  if (!size) return;
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= size) n = size - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

inline bool equalsIgnoreCase(const char *a, size_t alen, const char *b, size_t blen) {
  if (alen != blen) return false;
  for (size_t i = 0; i < alen; i++)
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}

inline bool containsIgnoreCase(const char *hay, const char *needle) {
  size_t hn = strlen(hay), nn = strlen(needle);
  if (!nn) return true;
  for (size_t i = 0; i + nn <= hn; i++)
    if (equalsIgnoreCase(hay + i, nn, needle, nn)) return true;
  return false;
}

inline bool Filter::matches(const char *line, const char *dir) const {
  if (lines[0]) {
    bool ok = false;
    size_t ll = strlen(line);
    const char *p = lines;
    while (*p && !ok) {
      const char *e = strchr(p, ',');
      size_t n = e ? (size_t)(e - p) : strlen(p);
      const char *s = p;
      while (n && *s == ' ') { s++; n--; }
      while (n && s[n - 1] == ' ') n--;
      if (n && equalsIgnoreCase(line, ll, s, n)) ok = true;  // exakte Linie ("6" != "61")
      p = e ? e + 1 : p + strlen(p);
    }
    if (!ok) return false;
  }
  return !direction[0] || containsIgnoreCase(dir, direction);
}

// ----------------------------------------------------------------------------- Anfragen

// Rahmen jeder Anfrage. Die AID prüft der Server derzeit nicht, der auth-Block muss aber da sein.
inline JsonObject beginRequest(JsonDocument &doc, const char *meth) {
  doc["ver"] = kProtocolVersion;
  doc["lang"] = "deu";
  JsonObject auth = doc["auth"].to<JsonObject>();
  auth["type"] = "AID";
  auth["aid"] = nullptr;
  JsonObject client = doc["client"].to<JsonObject>();
  client["id"] = "HAFAS";
  client["type"] = "WEB";
  client["name"] = "webapp";
  client["l"] = "vs_webapp";
  JsonObject svc = doc["svcReqL"][0].to<JsonObject>();
  svc["meth"] = meth;
  return svc["req"].to<JsonObject>();
}

// date "YYYYMMDD" und time "HHMMSS" (6-stellig!) sind optional; ohne beides = jetzt.
inline void buildBoardRequest(JsonDocument &doc, const char *stopId, uint16_t maxJny,
                              const char *date = nullptr, const char *time = nullptr) {
  JsonObject req = beginRequest(doc, "StationBoard");
  req["stbLoc"]["lid"] = stopId;
  req["type"] = "DEP";
  req["sort"] = "PT";
  req["maxJny"] = maxJny;
  if (date && time) {
    req["date"] = date;
    req["time"] = time;
  }
}

inline void buildSearchRequest(JsonDocument &doc, const char *name, uint8_t maxLoc) {
  JsonObject req = beginRequest(doc, "LocMatch");
  JsonObject in = req["input"].to<JsonObject>();
  in["loc"]["name"] = name;
  in["loc"]["type"] = "S";
  in["field"] = "S";
  in["maxLoc"] = maxLoc;
}

// ArduinoJson-Filter: nur die Felder behalten, die wir brauchen. Hält die Antwort klein.
inline void buildBoardFilter(JsonDocument &f) {
  f["err"] = true;
  JsonObject svc = f["svcResL"][0].to<JsonObject>();
  svc["err"] = true;
  svc["errTxt"] = true;
  JsonObject res = svc["res"].to<JsonObject>();
  res["common"]["prodL"][0]["name"] = true;
  res["common"]["locL"][0]["extId"] = true;
  JsonObject j = res["jnyL"][0].to<JsonObject>();
  j["date"] = true;
  j["dirTxt"] = true;
  j["prodX"] = true;
  JsonObject s = j["stbStop"].to<JsonObject>();
  s["dTimeS"] = true;
  s["dTimeR"] = true;
  s["dCncl"] = true;
  s["dProdX"] = true;
  s["locX"] = true;
  s["dPltfS"]["txt"] = true;
  s["dPltfR"]["txt"] = true;
}

inline void buildSearchFilter(JsonDocument &f) {
  f["err"] = true;
  JsonObject svc = f["svcResL"][0].to<JsonObject>();
  svc["err"] = true;
  svc["errTxt"] = true;
  JsonObject loc = svc["res"]["match"]["locL"][0].to<JsonObject>();
  loc["name"] = true;
  loc["extId"] = true;
  loc["lid"] = true;
  loc["type"] = true;
}

// ----------------------------------------------------------------------------- Antworten

// Prüft die HAFAS-Fehlerfelder. Bei Server-Fehlern steht der Text in errBuf.
inline Error checkEnvelope(JsonVariantConst root, char *errBuf, size_t errLen) {
  if (errBuf && errLen) errBuf[0] = 0;
  const char *top = root["err"] | "";
  JsonVariantConst svc = root["svcResL"][0];
  if (svc.isNull()) return Error::Json;
  const char *e = svc["err"] | "";
  if (strcmp(top, "OK") != 0 || strcmp(e, "OK") != 0) {
    if (errBuf) {
      const char *txt = svc["errTxt"] | "";
      copyStr(errBuf, errLen, *e ? e : (*top ? top : "?"));
      if (*txt) {
        size_t n = strlen(errBuf);
        if (n + 2 < errLen) { errBuf[n] = ' '; copyStr(errBuf + n + 1, errLen - n - 1, txt); }
      }
    }
    return Error::Server;
  }
  return Error::Ok;
}

// Hängt passende Abfahrten an out[count...] an (höchstens bis cap), `count` wird erhöht.
inline Error parseBoard(JsonVariantConst root, Departure *out, size_t cap, size_t &count,
                        const Filter &filter, char *errBuf = nullptr, size_t errLen = 0) {
  Error err = checkEnvelope(root, errBuf, errLen);
  if (err != Error::Ok) return err;
  JsonVariantConst res = root["svcResL"][0]["res"];
  JsonArrayConst prods = res["common"]["prodL"];
  JsonArrayConst locs = res["common"]["locL"];
  for (JsonObjectConst j : res["jnyL"].as<JsonArrayConst>()) {
    if (count >= cap) break;
    JsonObjectConst s = j["stbStop"];
    int32_t plan;
    if (!parseHafasTime(s["dTimeS"] | (const char *)nullptr, plan)) continue;
    int px = s["dProdX"] | (j["prodX"] | 0);
    const char *line = prods[px]["name"] | "?";
    const char *dir = j["dirTxt"] | "";
    if (!filter.matches(line, dir)) continue;

    Departure &d = out[count];
    memset(&d, 0, sizeof d);
    copyStr(d.line, sizeof d.line, line);
    copyStr(d.direction, sizeof d.direction, dir);
    copyStr(d.date, sizeof d.date, j["date"] | "");
    copyStr(d.stopId, sizeof d.stopId, locs[s["locX"] | 0]["extId"] | "");
    d.plannedSec = plan;
    d.realtimeSec = -1;
    int32_t rt;
    if (parseHafasTime(s["dTimeR"] | (const char *)nullptr, rt)) d.realtimeSec = rt;
    d.cancelled = s["dCncl"] | false;
    const char *pr = s["dPltfR"]["txt"] | "";
    const char *ps = s["dPltfS"]["txt"] | "";
    copyStr(d.platform, sizeof d.platform, *pr ? pr : ps);
    if (*pr && *ps && strcmp(pr, ps) != 0) copyStr(d.platformPlanned, sizeof d.platformPlanned, ps);
    count++;
  }
  return Error::Ok;
}

inline Error parseSearch(JsonVariantConst root, Stop *out, size_t cap, size_t &count,
                         char *errBuf = nullptr, size_t errLen = 0) {
  Error err = checkEnvelope(root, errBuf, errLen);
  if (err != Error::Ok) return err;
  count = 0;
  for (JsonObjectConst l : root["svcResL"][0]["res"]["match"]["locL"].as<JsonArrayConst>()) {
    if (count >= cap) break;
    if (strcmp(l["type"] | "", "S") != 0) continue;  // nur Haltestellen, keine Adressen/POIs
    copyStr(out[count].id, sizeof out[count].id, l["extId"] | (l["lid"] | ""));
    copyStr(out[count].name, sizeof out[count].name, l["name"] | "");
    count++;
  }
  return Error::Ok;
}

}  // namespace Uestra
