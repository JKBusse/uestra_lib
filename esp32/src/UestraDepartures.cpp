#include "UestraDepartures.h"

#if defined(ARDUINO)
#include <HTTPClient.h>

#include "UestraCerts.h"

namespace Uestra {

static constexpr uint8_t kMaxDays = 3;  // so viele Tage werden höchstens nachgeladen

Client::Client() { _tls.setCACert(kRootCA); }

void Client::setCACert(const char *pem) { _tls.setCACert(pem); }
void Client::setInsecure() { _tls.setInsecure(); }
void Client::setLineFilter(const char *s) { copyStr(_filter.lines, sizeof _filter.lines, s); }
void Client::setDirectionFilter(const char *s) { copyStr(_filter.direction, sizeof _filter.direction, s); }
void Client::setFetchLimit(uint16_t n) { _fetchLimit = n > 200 ? 200 : (n ? n : 1); }
void Client::setTimeout(uint32_t ms) { _timeoutMs = ms; }

Error Client::post(const JsonDocument &request, const JsonDocument &filter, JsonDocument &response) {
  String body;
  serializeJson(request, body);

  HTTPClient http;
  http.setReuse(false);
  http.useHTTP10(true);  // kein "chunked": ArduinoJson liest den Stream direkt
  http.setTimeout(_timeoutMs);
  _httpCode = 0;
  if (!http.begin(_tls, kEndpoint)) return Error::Network;
  http.addHeader("Content-Type", "application/json");  // sonst HTTP 415
  http.addHeader("User-Agent", "UestraDepartures-ESP32");

  _httpCode = http.POST(body);
  if (_httpCode <= 0) {
    http.end();
    return Error::Network;
  }
  if (_httpCode != HTTP_CODE_OK) {
    http.end();
    return Error::Http;
  }
  DeserializationError de =
      deserializeJson(response, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  _tls.stop();
  return de ? Error::Json : Error::Ok;
}

Error Client::searchStops(const char *name, Stop *out, size_t cap, size_t &count) {
  count = 0;
  JsonDocument req, filter, res;
  buildSearchRequest(req, name, cap > 20 ? 20 : (uint8_t)cap);
  buildSearchFilter(filter);
  Error e = post(req, filter, res);
  if (e != Error::Ok) return e;
  return parseSearch(res, out, cap, count, _errText, sizeof _errText);
}

Error Client::departures(const char *stopId, Departure *out, size_t cap, size_t &count) {
  count = 0;
  JsonDocument filter;
  buildBoardFilter(filter);
  // Ohne Filter reichen genau so viele Abfahrten, wie gewünscht sind.
  uint16_t fetch = _filter.active() ? _fetchLimit : (uint16_t)(cap > 200 ? 200 : cap);
  if (fetch < cap && _filter.active()) fetch = cap > 200 ? 200 : (uint16_t)cap;

  time_t now = time(nullptr);
  bool timeKnown = now > 1700000000;  // Uhr gesetzt?

  for (uint8_t day = 0; day < kMaxDays && count < cap; day++) {
    JsonDocument req, res;
    if (day == 0) {
      buildBoardRequest(req, stopId, fetch);
    } else {
      if (!timeKnown) break;  // Folgetag ohne Uhrzeit nicht berechenbar
      struct tm t;
      localtime_r(&now, &t);
      t.tm_mday += day;
      t.tm_hour = t.tm_min = t.tm_sec = 0;
      t.tm_isdst = -1;
      mktime(&t);
      char date[9];
      strftime(date, sizeof date, "%Y%m%d", &t);
      buildBoardRequest(req, stopId, fetch, date, "000000");  // time muss 6-stellig sein
    }
    Error e = post(req, filter, res);
    if (e != Error::Ok) return count ? Error::Ok : e;  // schon Treffer: lieber diese zeigen
    e = parseBoard(res, out, cap, count, _filter, _errText, sizeof _errText);
    if (e != Error::Ok) return count ? Error::Ok : e;
  }
  return Error::Ok;
}

}  // namespace Uestra
#endif  // ARDUINO
