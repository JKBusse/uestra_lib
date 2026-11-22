# Die HAFAS-Schnittstelle von gvh.hafas.de

Stand der Untersuchung: November 2026. Alles hier wurde gegen den Live-Server getestet,
nichts davon ist offiziell dokumentiert.

## Grundlagen

- Endpunkt: `POST https://gvh.hafas.de/hamm`, JSON-Body, `Content-Type: application/json`
  (sonst HTTP 415).
- Body: `{"ver": "1.62", "lang": "deu", "auth": {...}, "client": {...}, "svcReqL": [{"meth": ..., "req": {...}}]}`
- Fehler stehen in `err` (global) und `svcResL[0].err` (pro Methode). HTTP 500 heißt meist:
  Body fehlerhaft oder Pflichtfeld fehlt. `FAIL / Internal Error` heißt: Body gültig, aber
  die Methode lehnt die Parameter ab.

### Pflicht und Nicht-Pflicht

| Feld | Ergebnis |
|---|---|
| `Content-Type` fehlt oder `text/plain` | HTTP 415 |
| `ver` oder `lang` fehlt | HTTP 500 |
| `auth`-Block fehlt oder `{}` | HTTP 500 |
| `auth.aid` beliebig, `null` oder fehlend | OK – die AID wird **nicht geprüft** |
| `client` fehlt oder `{}` | HTTP 500 |
| `client` nur `{"id": "HAFAS", "type": "WEB"}` | OK – `name`, `l`, `v` werden nicht ausgewertet |
| `ver` zwischen 1.20 und 1.62 | OK (2.0 → 500) |
| `Origin`, `Referer`, `User-Agent` | egal, auch fremde Werte |
| Query-String (`hciMethod`, `aid`, …) | nicht nötig |
| `formatted`, Request-`id`, `sort`, `type` | nicht nötig |

Die Web-App selbst hat in ihrer Config `hciAuth: {type: "AID", aid: null}`. Eine AID steht
in keiner öffentlichen Datei. Sollte HAFAS die Prüfung einschalten, kommt ein AUTH-Fehler.

Keine Drosselung bei 25 schnellen Aufrufen hintereinander festgestellt.

## Methoden

### LocMatch – Haltestellensuche
`{"input": {"loc": {"name": "Kröpcke", "type": "S"}, "field": "S", "maxLoc": 5}}`
Die Suche ist **bundesweit** und unscharf. Treffer: `match.locL[]` mit `extId`/`lid`
(`de:03241:1807`), `name`, `crd` (Koordinaten in 1e-6 Grad).

### StationBoard – Abfahrtstafel
`{"stbLoc": {"lid": "de:03241:1807"}, "type": "DEP", "sort": "PT", "maxJny": 50}`

- Nur `lid` zählt, `extId` allein führt zu `FAIL`.
- **Nur Abfahrten bis 24:00 des angefragten Tages.** `maxJny` begrenzt nur die Anzahl,
  `dur` wird ignoriert. Für Folgetage `date` (`YYYYMMDD`) und `time` (**6-stellig**
  `HHMMSS`, 4-stellig → `FAIL`) setzen. Vergangene Tage funktionieren.
- Der Fahrplan ist bis etwa Ende März 2027 veröffentlicht (`ServerInfo.fpE` meldet
  fälschlich 21.11.2027).
- **Steig-/Mast-IDs:** `de:03241:11:1:12` statt `de:03241:11` liefert nur die Abfahrten
  dieses Steigs, also eine Richtung. Die IDs stehen in `common.locL[].extId`.
- **Serverseitige Filter funktionieren nicht:** `dirLoc` (in mehreren Schreibweisen),
  `jnyFltrL` vom Typ `LINE`, `stbFltrEquiv` werden stillschweigend ignoriert. Der
  Produktfilter `{"type": "PROD", ...}` filtert, ist aber hier überflüssig (der frühere
  Wert 831 änderte nichts).
- Antwort: `jnyL[]` mit `stbStop` (`dTimeS` Plan, `dTimeR` Echtzeit, `dCncl`, `dPltfS`/
  `dPltfR` Gleis, `dPlatfCh` Gleisänderung, `locX`, `dProdX`), `dirTxt`, `date`, `jid`;
  Verweise (`prodX`, `locX`) gehen in `common.prodL` / `common.locL`.
- `dTimeR` fehlt, wenn keine Echtzeit vorliegt (dann gilt der Fahrplan). Ausfall:
  `dCncl: true`, kein `dTimeR`.
- Zeitformat `HHMMSS`, bei Tageswechsel `DDHHMMSS` (DD = Tagesversatz).
- Gleise gibt es nur bei Zügen, nicht bei Stadtbahn und Bus.
- `msgL`/`himL` waren bei Abfahrten immer leer.

### JourneyDetails – Fahrtverlauf
`{"jid": "<jid aus der Abfahrt>", "date": "20261121", "getPolyline": false}`
Liefert `journey.stopL[]` mit allen Halten (Plan, Echtzeit, Gleis).

### TripSearch – Verbindungssuche
`{"depLocL": [{"lid": ...}], "arrLocL": [{"lid": ...}], "numF": 3, "outDate": "YYYYMMDD", "outTime": "HHMMSS"}`
Ohne Datum/Zeit: jetzt. Antwort: `outConL[]` mit `dep`, `arr`, `dur` (`HHMMSS`), `chg`
(Umstiege), `secL[]` (Abschnitte `JNY`, `TRSF`, `WALK`). Fußwege stehen in `gis`
(`dist`, `durS`). Meldungen zur Strecke (Rolltreppen/Aufzüge) stehen in `common.himL`,
referenziert über `secL[].jny.msgL[].himX`.

### HimSearch – netzweite Störungsmeldungen
`{"himFltrL": [{"type": "PROD", "mode": "INC", "value": "255"}]}`

- **Ohne PROD-Filter nur `FAIL`.** Funktionierende Werte: `255` (101 Meldungen),
  `32` Bus, `16` S-Bahn, `8` Regionalzug. `1023`, `511` und `256` (Stadtbahn) werden
  abgelehnt. Meldungen zu Stadtbahnlinien sind trotzdem in `255` enthalten.
- `dateB`/`timeB` (6-stellig) filtert auf zu diesem Zeitpunkt gültige Meldungen, auch in
  der Zukunft. `maxNum` wird ignoriert.
- Meldung: `head`, `text`, `sDate`/`sTime`, `eDate`/`eTime`, `lModDate`, `hid`,
  Koordinate in `common.himMsgEdgeL`. **Keine Linien- oder Haltestellen-IDs**, eine
  Zuordnung geht nur über Text im `head`.
- Die Rolltreppen-/Aufzugsmeldungen (`UESTRA-B-…`) kommen nur über `TripSearch`.
- `HIMCAT` filtert (`2` → 71 statt 101 Meldungen), die Bedeutung der Werte ist unbekannt.
  Unbekannte Filtertypen werden ohne Fehler ignoriert.

### Weitere Methoden
`ServerInfo`, `LocGeoPos` (Haltestellen im Umkreis, Koordinaten in 1e-6 Grad) und
`JourneyGeoPos` (Live-Positionen von Fahrzeugen in einem Rechteck, ~100 Fahrzeuge um
Kröpcke) werden akzeptiert. Sie sind im Tool nicht eingebaut.

## Produktklassen (`cls` / `jnyFltrL`)

Bitmaske. In Hannover kommen vor: `256` Stadtbahn, `32` Bus, `16` S-Bahn, `8` R-Bahn/
Regionalzug, `1`/`2` Fernverkehr (ICE, IC, FlixTrain).
