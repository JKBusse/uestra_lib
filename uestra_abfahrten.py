#!/usr/bin/env python3
"""Echtzeit-Abfahrten, Fahrtverlauf und Verbindungen der Üstra/GVH über gvh.hafas.de.

Inoffizielle Schnittstelle (dieselbe, die die Web-Auskunft nutzt) - kann sich ändern.
Nur Standardbibliothek, keine Installation nötig.

Abfahrten:
    python3 uestra_abfahrten.py                         # Stockholmer Allee, jetzt
    python3 uestra_abfahrten.py de:03241:1807 30        # Haltestelle (ID), Anzahl
    python3 uestra_abfahrten.py "Kröpcke"               # Haltestelle per Name
    python3 uestra_abfahrten.py Kröpcke --linie 6,11    # nur diese Linien
    python3 uestra_abfahrten.py Kröpcke --richtung Hbf  # Ziel enthält Text (Teilwort)
Steige (nur eine Richtung):
    python3 uestra_abfahrten.py Kröpcke --steige        # Steig-IDs mit Linien/Zielen
    python3 uestra_abfahrten.py de:03241:11:1:12        # Abfahrten nur von diesem Steig
Fahrtverlauf (alle folgenden Halte inkl. Echtzeit):
    python3 uestra_abfahrten.py Kröpcke --linie 6 --fahrt     # nächste Fahrt
    python3 uestra_abfahrten.py Kröpcke --linie 6 --fahrt 3   # die 3. in der Liste
Verbindungen:
    python3 uestra_abfahrten.py --von "Stockholmer Allee" --nach "Hauptbahnhof"
    python3 uestra_abfahrten.py --von Kröpcke --nach Flughafen --ab 18:30 -n 5
Sonstiges:
    python3 uestra_abfahrten.py --suche Stockholmer     # Haltestellen-IDs suchen
    python3 uestra_abfahrten.py --file antwort.json     # gespeicherte Antwort auswerten
"""
import argparse
import json
import sys
import urllib.error
import urllib.request
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

ENDPOINT = "https://gvh.hafas.de/hamm"
VER = "1.62"
# Der Server prüft die AID derzeit nicht (die Web-App selbst hat in ihrer Config aid:null),
# der auth-Block muss aber vorhanden sein, sonst HTTP 500. Falls HAFAS das irgendwann
# erzwingt, kommt ein AUTH-Fehler, den _check() ausgibt.
AUTH = {"type": "AID", "aid": None}
# Vom Server nur auf id/type geprüft; name/l entsprechen der Config der Web-App.
CLIENT = {"id": "HAFAS", "type": "WEB", "name": "webapp", "l": "vs_webapp"}

TZ = ZoneInfo("Europe/Berlin")
DEFAULT_HALT = "de:03241:1807"  # Stockholmer Allee
MAX_TAGE = 3  # so viele Tage werden höchstens nachgeladen, um die gewünschte Anzahl zu füllen


# --------------------------------------------------------------------------- Transport

def _call(meth: str, req_data: dict) -> dict:
    body = {
        "ver": VER,
        "lang": "deu",
        "auth": AUTH,
        "client": CLIENT,
        "svcReqL": [{"meth": meth, "req": req_data}],
    }
    req = urllib.request.Request(
        ENDPOINT,
        data=json.dumps(body).encode(),
        headers={
            "Content-Type": "application/json",  # sonst HTTP 415
            "User-Agent": "Mozilla/5.0 (privater Abfahrtsmonitor)",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        raise SystemExit(f"HTTP {e.code}: {e.read().decode(errors='replace')[:500]}")
    except urllib.error.URLError as e:
        raise SystemExit(f"Netzwerkfehler: {e.reason}")


def _check(data: dict) -> None:
    if data.get("err") != "OK" or data["svcResL"][0].get("err") != "OK":
        svc = (data.get("svcResL") or [{}])[0]
        raise SystemExit(f"HAFAS-Fehler: {data.get('err')} / {svc.get('err')} {svc.get('errTxt', '')}")


def _res(data: dict) -> dict:
    _check(data)
    return data["svcResL"][0]["res"]


# --------------------------------------------------------------------------- Haltestellen

def search(name: str, n: int = 10) -> list[dict]:
    """Haltestellensuche (LocMatch), wie das Suchfeld der Web-App. Bundesweit!"""
    data = _call("LocMatch", {
        "input": {"loc": {"name": name, "type": "S"}, "field": "S", "maxLoc": n},
    })
    return parse_search(data)


def parse_search(data: dict) -> list[dict]:
    _check(data)
    locs = data["svcResL"][0]["res"]["match"].get("locL", [])
    return [{"name": l["name"], "id": l.get("extId") or l["lid"]} for l in locs if l.get("type") == "S"]


def resolve(ziel: str) -> str:
    """Haltestellen-ID oder Name -> ID (bei Namen der erste Treffer)."""
    if ziel.startswith("de:"):
        return ziel
    treffer = search(ziel, 1)
    if not treffer:
        raise SystemExit(f"Keine Haltestelle gefunden: {ziel}")
    return treffer[0]["id"]


# --------------------------------------------------------------------------- Zeiten

def parse_time(date: str, t: str) -> datetime:
    """HAFAS-Zeit: HHMMSS oder DDHHMMSS (DD = Tagesversatz)."""
    days = 0
    if len(t) == 8:
        days, t = int(t[:2]), t[2:]
    base = datetime.strptime(date, "%Y%m%d").replace(tzinfo=TZ)
    return base + timedelta(days=days, hours=int(t[:2]), minutes=int(t[2:4]), seconds=int(t[4:6]))


def parse_dur(t: str) -> int:
    """Dauer HHMMSS oder DDHHMMSS -> Minuten."""
    days = 0
    if len(t) == 8:
        days, t = int(t[:2]), t[2:]
    return days * 1440 + int(t[:2]) * 60 + int(t[2:4])


def _delay(plan: datetime, rt: datetime | None) -> int | None:
    # auf Minuten abschneiden, damit z. B. 03:37:40 nicht als "+1 → 03:37" erscheint
    return int((rt.replace(second=0) - plan.replace(second=0)).total_seconds() // 60) if rt else None


def _platform(s: dict, p: str) -> tuple[str | None, str | None]:
    """Steig/Gleis (aktuell, ursprünglich falls geändert). p = 'd' (Abfahrt) oder 'a' (Ankunft)."""
    plan = (s.get(f"{p}PltfS") or {}).get("txt")
    ist = (s.get(f"{p}PltfR") or {}).get("txt")
    return (ist or plan), (plan if ist and plan and ist != plan else None)


def _stop(date: str, s: dict, p: str = "d") -> dict:
    """Haltedaten (Zeit, Echtzeit, Ausfall, Steig) aus einem HAFAS-Stop-Objekt."""
    plan = parse_time(date, s[f"{p}TimeS"])
    rt = parse_time(date, s[f"{p}TimeR"]) if f"{p}TimeR" in s else None
    steig, steig_alt = _platform(s, p)
    return {"plan": plan, "echtzeit": rt, "verspaetung_min": _delay(plan, rt),
            "ausfall": bool(s.get(f"{p}Cncl")), "steig": steig, "steig_alt": steig_alt}


def _status(x: dict) -> str:
    if x["ausfall"]:
        return "FÄLLT AUS"
    if x["echtzeit"] is None:
        return "(Fahrplan)"
    if x["verspaetung_min"] == 0:
        return "pünktlich"
    return f"{x['verspaetung_min']:+d} min → {x['echtzeit']:%H:%M}"


def _zeit(x: dict) -> str:
    """Kurzform für Verbindungen: '16:12' oder '16:14 (+2)'."""
    if x["ausfall"]:
        return f"{x['plan']:%H:%M} (fällt aus)"
    d = x["verspaetung_min"]
    return f"{x['plan']:%H:%M}" + (f" ({d:+d})" if d else "")


def _steig_text(x: dict) -> str:
    if not x["steig"]:
        return ""
    return f"Gl. {x['steig']}" + (f" (statt {x['steig_alt']})" if x["steig_alt"] else "")


# --------------------------------------------------------------------------- Abfahrten

def fetch(lid: str, max_jny: int, when: datetime | None = None) -> dict:
    """StationBoard-Anfrage (Abfahrtstafel). Der Server liefert nur Abfahrten bis 24:00 des angefragten Tages."""
    req = {"stbLoc": {"lid": lid}, "type": "DEP", "sort": "PT", "maxJny": max_jny}
    if when:
        req["date"] = f"{when:%Y%m%d}"
        req["time"] = f"{when:%H%M%S}"  # muss 6-stellig sein, sonst FAIL
    return _call("StationBoard", req)


def departures(data: dict) -> list[dict]:
    res = _res(data)
    prods = res["common"].get("prodL", [])
    locs = res["common"].get("locL", [])
    out = []
    for j in res.get("jnyL", []):
        s = j["stbStop"]
        prod = prods[s.get("dProdX", j.get("prodX", 0))] if prods else {}
        loc = locs[s["locX"]] if "locX" in s and locs else {}
        out.append({
            **_stop(j["date"], s),
            "linie": prod.get("name", "?"),
            "richtung": j.get("dirTxt", ""),
            "halt": loc.get("name", ""),
            "mast": loc.get("extId", ""),  # Steig-ID, z. B. de:03241:11:1:12
            "jid": j.get("jid"),
            "datum": j["date"],
        })
    return out


def _match(d: dict, linien: set[str] | None, richtung: str | None) -> bool:
    if linien and d["linie"].lower() not in linien:
        return False
    return not richtung or richtung.lower() in d["richtung"].lower()


def board(lid: str, n: int, linien: set[str] | None = None, richtung: str | None = None,
          debug: bool = False) -> list[dict]:
    """Die nächsten n Abfahrten (optional gefiltert).

    Der Server liefert nur bis 24:00 des Tages und kennt keinen funktionierenden Linien-/
    Richtungsfilter - daher wird bei Filtern alles geholt und lokal gefiltert, und bei zu
    wenigen Treffern der Folgetag ab 00:00 nachgeladen.
    """
    max_jny = 1000 if (linien or richtung) else n
    out: list[dict] = []
    when = None  # None = jetzt
    for tag in range(MAX_TAGE):
        data = fetch(lid, max_jny, when)
        if debug and tag == 0:
            with open("antwort.json", "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            print("[Rohantwort in antwort.json gespeichert]", file=sys.stderr)
        out += [d for d in departures(data) if _match(d, linien, richtung)]
        if len(out) >= n:
            break
        base = when or datetime.now(TZ)
        when = (base + timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)
    return out[:n]


def print_departures(deps: list[dict]) -> None:
    print(deps[0]["halt"])
    erster_tag = deps[0]["plan"].date()
    lw = max(len(d["linie"]) for d in deps)
    mit_steig = any(d["steig"] for d in deps)
    for d in deps:
        zeit = f"{d['plan']:%H:%M}" if d["plan"].date() == erster_tag else f"{d['plan']:%d.%m. %H:%M}"
        steig = f"{_steig_text(d):<16} " if mit_steig else ""
        print(f"{zeit:<12} {d['linie']:>{lw}}  {d['richtung']:<28} {steig}{_status(d)}")


# --------------------------------------------------------------------------- Steige

def print_platforms(lid: str) -> None:
    """Steig-IDs einer Haltestelle mit Linien und Zielen (aus dem Fahrplan von morgen 08:00)."""
    morgen = (datetime.now(TZ) + timedelta(days=1)).replace(hour=8, minute=0, second=0, microsecond=0)
    deps = departures(fetch(lid, 400, morgen))
    if not deps:
        raise SystemExit("Keine Abfahrten gefunden - Haltestelle prüfen (--suche).")
    gruppen: dict[str, dict] = {}
    for d in deps:
        g = gruppen.setdefault(d["mast"], {"linien": set(), "ziele": set(), "gleise": set()})
        g["linien"].add(d["linie"])
        g["ziele"].add(d["richtung"])
        if d["steig"]:
            g["gleise"].add(d["steig"])
    print(f"{deps[0]['halt']} - Steige (Fahrplan für {morgen:%d.%m.}, ab 08:00)")
    key = lambda x: (len(x), x)
    for mast in sorted(gruppen):
        g = gruppen[mast]
        gleis = f"  Gl. {', '.join(sorted(g['gleise'], key=key))}" if g["gleise"] else ""
        print(f"{mast:<20}{gleis}  Linien {', '.join(sorted(g['linien'], key=key))}"
              f"  → {', '.join(sorted(g['ziele']))}")
    print("\nAbfahrten nur eines Steigs: Steig-ID statt Haltestelle angeben.")


# --------------------------------------------------------------------------- Fahrtverlauf

def print_journey(dep: dict) -> None:
    """Alle Halte ab dem Einstiegshalt der gegebenen Abfahrt (JourneyDetails)."""
    res = _res(_call("JourneyDetails", {"jid": dep["jid"], "date": dep["datum"], "getPolyline": False}))
    j = res["journey"]
    locs = res["common"]["locL"]
    stops = j["stopL"]
    start = next((i for i, s in enumerate(stops) if locs[s["locX"]].get("extId") == dep["mast"]), 0)
    print(f"Linie {dep['linie']} → {dep['richtung']}  (ab {dep['halt']} {dep['plan']:%H:%M})")
    for s in stops[start:]:
        x = _stop(j["date"], s, "d" if "dTimeS" in s else "a")
        steig = _steig_text(x)
        print(f"{x['plan']:%H:%M}  {locs[s['locX']]['name']:<36} {_status(x)}" + (f"  {steig}" if steig else ""))


# --------------------------------------------------------------------------- Verbindungen

def parse_ab(text: str) -> datetime:
    now = datetime.now(TZ)
    try:
        if len(text) <= 5:  # HH:MM, bei Vergangenheit: morgen
            h, m = text.split(":")
            t = now.replace(hour=int(h), minute=int(m), second=0, microsecond=0)
            return t + timedelta(days=1) if t < now - timedelta(minutes=1) else t
        return datetime.fromisoformat(text).replace(tzinfo=TZ)
    except ValueError:
        raise SystemExit("--ab: Format HH:MM oder 'YYYY-MM-DD HH:MM'")


def print_trips(von: str, nach: str, ab: datetime | None, n: int) -> None:
    req = {"depLocL": [{"lid": resolve(von)}], "arrLocL": [{"lid": resolve(nach)}],
           "numF": n, "getPasslist": False, "getPolyline": False}
    if ab:
        req["outDate"] = f"{ab:%Y%m%d}"
        req["outTime"] = f"{ab:%H%M%S}"
    res = _res(_call("TripSearch", req))
    common = res["common"]
    locs, prods, dirs, him = common["locL"], common["prodL"], common.get("dirL", []), common.get("himL", [])
    cons = res.get("outConL", [])[:n]
    if not cons:
        raise SystemExit("Keine Verbindung gefunden.")
    meldungen: list[str] = []
    for i, c in enumerate(cons, 1):
        umstieg_an = False
        dep, arr = _stop(c["date"], c["dep"]), _stop(c["date"], c["arr"], "a")
        umst = c.get("chg", 0)
        print(f"Verbindung {i}: {_zeit(dep)} → {_zeit(arr)}  "
              f"({parse_dur(c['dur'])} min, {umst} Umstieg{'e' if umst != 1 else ''})")
        secs = c["secL"]
        for k, sec in enumerate(secs):
            sd, sa = _stop(c["date"], sec["dep"]), _stop(c["date"], sec["arr"], "a")
            nd, na = locs[sec["dep"]["locX"]]["name"], locs[sec["arr"]["locX"]]["name"]
            if sec["type"] == "JNY":
                jny = sec["jny"]
                prod = prods[sec["dep"].get("dProdX", jny.get("prodX", 0))]
                ziel = jny.get("dirTxt") or (dirs[jny["dirL"][0]["dirX"]]["txt"] if jny.get("dirL") else "")
                if not umstieg_an:  # Einstieg nicht schon als Umstieg-Zeile gedruckt
                    print(f"  {_zeit(sd):<12} {nd}  {_steig_text(sd)}".rstrip())
                print(f"  {'':<12} └ Linie {prod['name']} → {ziel}")
                nxt = secs[k + 1:k + 3]
                folge = next((x for x in nxt if x["type"] == "JNY"), None)
                if folge and locs[folge["dep"]["locX"]]["name"] == na and not any(
                        (x.get("gis") or {}).get("dist") for x in nxt[:nxt.index(folge)]):
                    # Umstieg am selben Halt: eine Zeile mit Ankunft und Weiterfahrt
                    ab = _stop(c["date"], folge["dep"])
                    print(f"  {_zeit(sa):<12} {na}  {_steig_text(sa)}".rstrip())
                    print(f"  {'':<12} ⇄ Umstieg, weiter {_zeit(ab)} {_steig_text(ab)}".rstrip())
                    umstieg_an = True
                else:
                    umstieg_an = False
                    print(f"  {_zeit(sa):<12} {na}  {_steig_text(sa)}".rstrip())
                for m in jny.get("msgL", []):
                    if m.get("type") == "HIM":
                        head = him[m["himX"]].get("head")
                        if head and head not in meldungen:
                            meldungen.append(head)
            else:  # TRSF / WALK / DEVI: Fußweg zwischen verschiedenen Halten
                gis = sec.get("gis") or {}
                dauer = parse_dur(gis["durS"]) if gis.get("durS") else 0
                if nd != na or gis.get("dist"):
                    print(f"  {'':<12} ↳ Fußweg {dauer} min" + (f" ({gis['dist']} m)" if gis.get("dist") else "")
                          + f" nach {na}")
        print()
    if meldungen:
        print("Meldungen auf der Strecke:")
        for m in meldungen:
            print(f"  ⚠ {m}")


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(
        description="Echtzeit-Abfahrten der Üstra/GVH.", epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("halt", nargs="?", default=DEFAULT_HALT, help="Haltestelle: Name, Haltestellen-ID oder Steig-ID")
    ap.add_argument("anzahl", nargs="?", type=int, default=15, help="Anzahl Abfahrten (Standard 15)")
    ap.add_argument("--linie", help="nur diese Linien, kommagetrennt (z. B. 6,11)")
    ap.add_argument("--richtung", help="nur Fahrten, deren Ziel diesen Text enthält")
    ap.add_argument("--steige", action="store_true", help="Steig-IDs der Haltestelle anzeigen")
    ap.add_argument("--fahrt", nargs="?", const=1, type=int, metavar="N",
                    help="Fahrtverlauf der N-ten Abfahrt der Liste (Standard: 1.)")
    ap.add_argument("--von", help="Verbindungssuche: Start")
    ap.add_argument("--nach", help="Verbindungssuche: Ziel")
    ap.add_argument("--ab", help="Verbindungssuche: Abfahrt HH:MM oder 'YYYY-MM-DD HH:MM' (Standard: jetzt)")
    ap.add_argument("-n", type=int, default=3, help="Anzahl Verbindungen (Standard 3)")
    ap.add_argument("--suche", nargs="+", metavar="NAME", help="Haltestellen-IDs suchen")
    ap.add_argument("--file", help="gespeicherte Antwort (antwort.json) auswerten")
    ap.add_argument("--debug", action="store_true", help="Rohantwort in antwort.json speichern")
    a = ap.parse_args()

    if a.suche:
        for h in search(" ".join(a.suche)):
            print(f"{h['id']:<22} {h['name']}")
        return
    if a.von or a.nach:
        if not (a.von and a.nach):
            ap.error("--von und --nach gehören zusammen")
        print_trips(a.von, a.nach, parse_ab(a.ab) if a.ab else None, a.n)
        return

    linien = {x.strip().lower() for x in a.linie.split(",") if x.strip()} if a.linie else None
    if a.file:
        with open(a.file, encoding="utf-8") as f:
            deps = [d for d in departures(json.load(f)) if _match(d, linien, a.richtung)]
    else:
        lid = resolve(a.halt)
        if a.steige:
            print_platforms(lid)
            return
        n = max(a.anzahl, a.fahrt or 0)
        deps = board(lid, n, linien, a.richtung, a.debug)

    if not deps:
        print("Keine Abfahrten gefunden" + (" (Filter --linie/--richtung aktiv)." if linien or a.richtung else
              ". Mit --debug ausführen und antwort.json schicken."))
        return
    if a.fahrt:
        if a.fahrt > len(deps):
            raise SystemExit(f"Nur {len(deps)} Abfahrten gefunden, --fahrt {a.fahrt} nicht möglich.")
        print_journey(deps[a.fahrt - 1])
    else:
        print_departures(deps[:a.anzahl])


if __name__ == "__main__":
    main()
