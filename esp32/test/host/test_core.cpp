// Host-Tests für UestraCore.h (laufen auf dem PC, ohne ESP32 und ohne Netzwerk).
#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <sstream>
#include <string>

#include "UestraCore.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                      \
  do {                                                                   \
    checks++;                                                            \
    if (!(cond)) {                                                       \
      failures++;                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                    \
  } while (0)
#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)

using namespace Uestra;

static std::string readFile(const char *path) {
  std::ifstream f(path);
  if (!f) { printf("Fixture fehlt: %s\n", path); exit(2); }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Synthetische Antwort mit den Sonderfällen, die live nicht verlässlich vorkommen.
static const char *kSynthetic = R"json({
 "err":"OK","svcResL":[{"err":"OK","res":{
  "common":{"prodL":[{"name":"6"},{"name":"RE70"},{"name":"61"}],
            "locL":[{"extId":"de:03241:31"},{"extId":"de:03241:31:5:7"}]},
  "jnyL":[
   {"date":"20261121","dirTxt":"Hannover/Nordhafen","prodX":0,
    "stbStop":{"dTimeS":"153700","dTimeR":"153740","dProdX":0,"locX":1}},
   {"date":"20261121","dirTxt":"Bielefeld Hbf","prodX":1,
    "stbStop":{"dTimeS":"154400","dTimeR":"155100","dProdX":1,"locX":1,
               "dPltfS":{"txt":"7"},"dPltfR":{"txt":"11A-C"}}},
   {"date":"20261121","dirTxt":"Messe/Ost(EXPO-Plaza)","prodX":0,
    "stbStop":{"dTimeS":"155000","dCncl":true,"dProdX":0,"locX":0}},
   {"date":"20261121","dirTxt":"Nachtlinie","prodX":2,
    "stbStop":{"dTimeS":"01002330","dProdX":2,"locX":0,"dPltfS":{"txt":"10"}}},
   {"date":"20261121","dirTxt":"ohne Zeit","prodX":0,"stbStop":{"dProdX":0,"locX":0}}
  ]}}]})json";

static void testTime() {
  int32_t s = 0;
  CHECK(parseHafasTime("153000", s) && s == 55800);
  CHECK(parseHafasTime("01002330", s) && s == 87810);  // DD=01, 00:23:30 = 86400 + 1410
  CHECK(!parseHafasTime("1530", s));
  CHECK(!parseHafasTime(nullptr, s));
  CHECK(!parseHafasTime("15300x", s));

  char b[8];
  formatClock(55800, b, sizeof b); CHECK_STR(b, "15:30");
  formatClock(86400 + 900, b, sizeof b); CHECK_STR(b, "00:15");  // über Mitternacht

  // Zeitzone Berlin: 2026-11-21 15:30 CET = 14:30 UTC
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  CHECK(toTimeT("20261121", 55800) == 1795271400);
  struct tm t;
  time_t sommer = toTimeT("20260701", 12 * 3600);
  localtime_r(&sommer, &t);
  CHECK(t.tm_hour == 12 && t.tm_isdst == 1);
  // 25:00 am 21.11. == 01:00 am 22.11.
  CHECK(toTimeT("20261121", 25 * 3600) == toTimeT("20261122", 3600));
  CHECK(toTimeT("kaputt", 0) == 0);
}

static void testParseSynthetic() {
  JsonDocument doc;
  CHECK(!deserializeJson(doc, kSynthetic));
  Departure out[10];
  size_t n = 0;
  char err[64];
  CHECK(parseBoard(doc, out, 10, n, Filter(), err, sizeof err) == Error::Ok);
  CHECK(n == 4);  // der Eintrag ohne dTimeS wird übersprungen

  CHECK_STR(out[0].line, "6");
  CHECK_STR(out[0].direction, "Hannover/Nordhafen");
  CHECK(out[0].hasRealtime());
  CHECK(out[0].delayMin() == 0);  // 15:37:00 -> 15:37:40: abgeschnitten, nicht +1
  CHECK(out[0].status() == Status::OnTime);
  CHECK_STR(out[0].stopId, "de:03241:31:5:7");
  CHECK_STR(out[0].date, "20261121");

  CHECK_STR(out[1].line, "RE70");
  CHECK(out[1].delayMin() == 7);
  CHECK(out[1].status() == Status::Delayed);
  CHECK_STR(out[1].platform, "11A-C");
  CHECK_STR(out[1].platformPlanned, "7");  // Gleisänderung

  CHECK(out[2].cancelled && out[2].status() == Status::Cancelled);
  CHECK(!out[2].hasRealtime());

  CHECK_STR(out[3].platform, "10");  // nur geplant (kein dPltfR)
  CHECK_STR(out[3].platformPlanned, "");
  CHECK(out[3].plannedSec == 87810);  // Tagesversatz: 00:23:30 am Folgetag
  CHECK(out[3].status() == Status::Scheduled);

  // cap wird eingehalten
  n = 0;
  CHECK(parseBoard(doc, out, 2, n, Filter()) == Error::Ok && n == 2);

  // append: count läuft weiter (Folgetag nachladen)
  n = 1;
  CHECK(parseBoard(doc, out, 10, n, Filter()) == Error::Ok && n == 5);
}

static void testFilter() {
  JsonDocument doc;
  deserializeJson(doc, kSynthetic);
  Departure out[10];
  size_t n;

  Filter f;
  copyStr(f.lines, sizeof f.lines, "6");
  n = 0; parseBoard(doc, out, 10, n, f);
  CHECK(n == 2);  // "6" trifft nicht "61" und nicht "RE70"
  CHECK_STR(out[0].line, "6");

  copyStr(f.lines, sizeof f.lines, " RE70 , 61 ");  // Leerzeichen, Mehrfachangabe, Groß/Klein
  n = 0; parseBoard(doc, out, 10, n, f);
  CHECK(n == 2);
  copyStr(f.lines, sizeof f.lines, "re70");
  n = 0; parseBoard(doc, out, 10, n, f);
  CHECK(n == 1);

  Filter g;
  copyStr(g.direction, sizeof g.direction, "NORDH");
  n = 0; parseBoard(doc, out, 10, n, g);
  CHECK(n == 1 && strcmp(out[0].direction, "Hannover/Nordhafen") == 0);

  CHECK(!Filter().active());
  CHECK(g.active());
}

static void testServerError() {
  JsonDocument doc;
  deserializeJson(doc, R"({"err":"OK","svcResL":[{"err":"FAIL","errTxt":"Internal Error"}]})");
  Departure out[2];
  size_t n = 0;
  char err[64];
  CHECK(parseBoard(doc, out, 2, n, Filter(), err, sizeof err) == Error::Server);
  CHECK_STR(err, "FAIL Internal Error");
  CHECK(n == 0);

  JsonDocument leer;
  deserializeJson(leer, "{}");
  CHECK(parseBoard(leer, out, 2, n, Filter()) == Error::Json);
}

static void testRequests() {
  JsonDocument doc;
  buildBoardRequest(doc, "de:03241:1807", 15);
  std::string s;
  serializeJson(doc, s);
  CHECK(s.find("\"meth\":\"StationBoard\"") != std::string::npos);
  CHECK(s.find("\"lid\":\"de:03241:1807\"") != std::string::npos);
  CHECK(s.find("\"maxJny\":15") != std::string::npos);
  CHECK(s.find("\"auth\":{\"type\":\"AID\",\"aid\":null}") != std::string::npos);
  CHECK(s.find("\"date\"") == std::string::npos);

  JsonDocument d2;
  buildBoardRequest(d2, "de:03241:1807", 15, "20261122", "000000");
  s.clear();
  serializeJson(d2, s);
  CHECK(s.find("\"date\":\"20261122\"") != std::string::npos);
  CHECK(s.find("\"time\":\"000000\"") != std::string::npos);

  // Anführungszeichen im Suchtext dürfen das JSON nicht zerbrechen
  JsonDocument d3;
  buildSearchRequest(d3, "Kr\"opcke", 5);
  s.clear();
  serializeJson(d3, s);
  JsonDocument back;
  CHECK(!deserializeJson(back, s));
  CHECK_STR(back["svcReqL"][0]["req"]["input"]["loc"]["name"] | "", "Kr\"opcke");
}

// Echte Serverantworten (am 23.11.2026 mitgeschnitten): prüft das tatsächliche Format samt Filter.
static void testRealFixtures() {
  for (const char *file : {"fixtures/board_stockholmer.json", "fixtures/board_hbf.json"}) {
    std::string raw = readFile(file);
    JsonDocument filter, full, slim;
    buildBoardFilter(filter);
    CHECK(!deserializeJson(full, raw));
    CHECK(!deserializeJson(slim, raw, DeserializationOption::Filter(filter)));

    Departure a[20], b[20];
    size_t na = 0, nb = 0;
    CHECK(parseBoard(full, a, 20, na, Filter()) == Error::Ok);
    CHECK(parseBoard(slim, b, 20, nb, Filter()) == Error::Ok);
    CHECK(na > 0 && na == nb);  // der Filter darf nichts verändern
    for (size_t i = 0; i < na && i < nb; i++) {
      CHECK_STR(a[i].line, b[i].line);
      CHECK_STR(a[i].direction, b[i].direction);
      CHECK_STR(a[i].platform, b[i].platform);
      CHECK_STR(a[i].stopId, b[i].stopId);
      CHECK(a[i].plannedSec == b[i].plannedSec && a[i].realtimeSec == b[i].realtimeSec);
      CHECK(a[i].line[0] && a[i].direction[0] && a[i].stopId[0]);
      CHECK(strncmp(a[i].stopId, "de:03241:", 9) == 0);
      CHECK(strlen(a[i].date) == 8);
    }
    printf("  %s: %zu Abfahrten, JSON ohne Filter %zu Bytes, mit ArduinoJson-Filter %zu Bytes\n",
           file, na, measureJson(full), measureJson(slim));
    CHECK(measureJson(slim) < measureJson(full));
  }

  std::string raw = readFile("fixtures/locmatch.json");
  JsonDocument filter, slim;
  buildSearchFilter(filter);
  CHECK(!deserializeJson(slim, raw, DeserializationOption::Filter(filter)));
  Stop stops[10];
  size_t n = 0;
  CHECK(parseSearch(slim, stops, 10, n) == Error::Ok);
  CHECK(n >= 1);
  CHECK_STR(stops[0].id, "de:03241:1807");
  CHECK(strstr(stops[0].name, "Stockholmer Allee") != nullptr);
}

int main() {
  testTime();
  testParseSynthetic();
  testFilter();
  testServerError();
  testRequests();
  testRealFixtures();
  printf("%d Prüfungen, %d fehlgeschlagen\n", checks, failures);
  return failures ? 1 : 0;
}
