"""Offline-Tests (kein Netzwerk): Parser, Filter und das Nachladen des Folgetags."""
import unittest
from datetime import datetime
from unittest import mock

import uestra_abfahrten as u


def stop(**kw):
    base = {"aCncl": False, "dCncl": False, "dPlatfCh": False, "dProdX": 0, "idx": 0, "locX": 0, "type": "N"}
    base.update(kw)
    return base


def jny(date="20261121", direction="Nordhafen", **stop_kw):
    return {"date": date, "dirTxt": direction, "jid": "x", "prodX": 0, "status": "P",
            "stbStop": stop(**stop_kw)}


def response(jnys, line="6"):
    return {"err": "OK", "svcResL": [{"err": "OK", "res": {
        "common": {"prodL": [{"name": line, "cls": 256}],
                   "locL": [{"name": "Hannover/Kröpcke", "extId": "de:03241:11:1:12"}]},
        "jnyL": jnys}}]}


class Zeiten(unittest.TestCase):
    def test_parse_time(self):
        t = u.parse_time("20261121", "153000")
        self.assertEqual((t.hour, t.minute), (15, 30))
        self.assertEqual(str(t.tzinfo), "Europe/Berlin")

    def test_parse_time_tagesversatz(self):
        t = u.parse_time("20261121", "01001500")  # DDHHMMSS: +1 Tag, 00:15
        self.assertEqual((t.day, t.hour, t.minute), (22, 0, 15))

    def test_parse_dur(self):
        self.assertEqual(u.parse_dur("002354"), 23)
        self.assertEqual(u.parse_dur("01000000"), 1440)

    def test_delay_schneidet_sekunden_ab(self):
        plan = u.parse_time("20261121", "033700")
        rt = u.parse_time("20261121", "033740")
        self.assertEqual(u._delay(plan, rt), 0)  # nicht "+1"
        self.assertEqual(u._delay(plan, u.parse_time("20261121", "033900")), 2)
        self.assertIsNone(u._delay(plan, None))


class Gleis(unittest.TestCase):
    def test_nur_plan(self):
        self.assertEqual(u._platform({"dPltfS": {"txt": "10"}}, "d"), ("10", None))

    def test_nur_ist(self):  # früher ging dPltfR ohne dPltfS verloren
        self.assertEqual(u._platform({"dPltfR": {"txt": "11"}}, "d"), ("11", None))

    def test_aenderung(self):
        s = {"dPltfS": {"txt": "7"}, "dPltfR": {"txt": "11A-C"}}
        self.assertEqual(u._platform(s, "d"), ("11A-C", "7"))

    def test_keins(self):
        self.assertEqual(u._platform({}, "d"), (None, None))


class Abfahrten(unittest.TestCase):
    def test_status(self):
        pünktlich = u.departures(response([jny(dTimeS="153000", dTimeR="153000")]))[0]
        spät = u.departures(response([jny(dTimeS="153000", dTimeR="153300")]))[0]
        fahrplan = u.departures(response([jny(dTimeS="153000")]))[0]
        ausfall = u.departures(response([jny(dTimeS="153000", dCncl=True)]))[0]
        self.assertEqual(u._status(pünktlich), "pünktlich")
        self.assertEqual(u._status(spät), "+3 min → 15:33")
        self.assertEqual(u._status(fahrplan), "(Fahrplan)")
        self.assertEqual(u._status(ausfall), "FÄLLT AUS")

    def test_felder(self):
        d = u.departures(response([jny(dTimeS="153000")]))[0]
        self.assertEqual((d["linie"], d["richtung"], d["halt"]), ("6", "Nordhafen", "Hannover/Kröpcke"))
        self.assertEqual(d["mast"], "de:03241:11:1:12")

    def test_fehlerantwort(self):
        with self.assertRaises(SystemExit):
            u.departures({"err": "OK", "svcResL": [{"err": "FAIL", "errTxt": "Internal Error"}]})

    def test_filter(self):
        d = u.departures(response([jny(dTimeS="153000")]))[0]
        self.assertTrue(u._match(d, {"6"}, None))
        self.assertFalse(u._match(d, {"61"}, None))  # exakte Linie, kein Teilstring
        self.assertTrue(u._match(d, None, "nordh"))
        self.assertFalse(u._match(d, None, "messe"))


class Mitternacht(unittest.TestCase):
    def test_folgetag_wird_nachgeladen(self):
        heute = response([jny(dTimeS="235000")])
        morgen = response([jny(date="20261122", dTimeS="000500"), jny(date="20261122", dTimeS="002200")])
        with mock.patch.object(u, "fetch", side_effect=[heute, morgen]) as f:
            deps = u.board("de:03241:1807", 3)
        self.assertEqual(len(deps), 3)
        self.assertEqual(f.call_count, 2)
        when = f.call_args_list[1].args[2]
        self.assertEqual((when.hour, when.minute, when.second), (0, 0, 0))

    def test_kein_nachladen_wenn_genug(self):
        voll = response([jny(dTimeS="153000"), jny(dTimeS="154000")])
        with mock.patch.object(u, "fetch", return_value=voll) as f:
            self.assertEqual(len(u.board("de:03241:1807", 2)), 2)
        self.assertEqual(f.call_count, 1)

    def test_nachladen_ist_begrenzt(self):
        leer = response([])
        with mock.patch.object(u, "fetch", return_value=leer) as f:
            self.assertEqual(u.board("de:03241:nope", 5), [])
        self.assertEqual(f.call_count, u.MAX_TAGE)


if __name__ == "__main__":
    unittest.main()
