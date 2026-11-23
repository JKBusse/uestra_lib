# üstra-abfahrten

Kommandozeilen-Tool für Echtzeit-Abfahrten, Fahrtverläufe und Verbindungen der
Üstra / des GVH (Hannover). Es spricht die HAFAS-Schnittstelle von `gvh.hafas.de` an –
dieselbe, die die Web-Auskunft benutzt.

- nur Python-Standardbibliothek (Python ≥ 3.10), keine Installation nötig
- **inoffizielle Schnittstelle**, kann sich jederzeit ändern (Details: [docs/schnittstelle.md](docs/schnittstelle.md))

## Benutzung

```sh
python3 uestra_abfahrten.py                         # Stockholmer Allee, jetzt
python3 uestra_abfahrten.py "Kröpcke" 20            # Haltestelle per Name, 20 Abfahrten
python3 uestra_abfahrten.py de:03241:1807           # Haltestelle per ID
python3 uestra_abfahrten.py --suche Stockholmer     # Haltestellen-IDs suchen
```

Filter (werden lokal angewendet, der Server ignoriert sie):

```sh
python3 uestra_abfahrten.py Kröpcke --linie 6,11
python3 uestra_abfahrten.py Kröpcke --richtung Hauptbahnhof
```

Nur eine Richtung: Steig-IDs anzeigen und statt der Haltestelle verwenden.

```sh
python3 uestra_abfahrten.py Kröpcke --steige
python3 uestra_abfahrten.py de:03241:11:1:12
```

Fahrtverlauf (alle folgenden Halte mit Echtzeit, bei Bahnhöfen mit Gleis):

```sh
python3 uestra_abfahrten.py Kröpcke --linie 6 --fahrt      # nächste Fahrt
python3 uestra_abfahrten.py Kröpcke --linie 6 --fahrt 3    # 3. Abfahrt der Liste
```

Verbindungen (inkl. Rolltreppen-/Aufzugsmeldungen auf der Strecke):

```sh
python3 uestra_abfahrten.py --von "Stockholmer Allee" --nach Hauptbahnhof
python3 uestra_abfahrten.py --von Kröpcke --nach Flughafen --ab 18:30 -n 5
```

Debugging: `--debug` speichert die Rohantwort in `antwort.json`, `--file antwort.json`
wertet sie offline aus.

## Hinweise

- Der Server liefert Abfahrten nur bis 24:00 des angefragten Tages. Das Tool lädt bei
  Bedarf automatisch den Folgetag nach.
- Die Namenssuche ist bundesweit und nimmt den ersten Treffer. Bei mehrdeutigen Namen
  die ID aus `--suche` verwenden.

## Entwicklung

```sh
python3 -m unittest -v      # Tests laufen offline, ohne Netzwerk
```

Optional als Befehl `uestra` installieren: `pip install -e .`

## Lizenz und Hinweis

[MIT](LICENSE) – die Lizenz gilt nur für diesen Code, nicht für Daten oder Schnittstelle.
Das Projekt ist inoffiziell und steht in keiner Verbindung zu Üstra, GVH oder HAFAS
(Hacon/Siemens Mobility); die Namen gehören ihren jeweiligen Inhabern.
