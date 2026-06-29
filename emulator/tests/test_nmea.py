"""Tests for nmea.py — including byte-exact checksum vectors that pin this module
to the controller's C implementation (Arduino/controller/garmin.ino) and the
example lines in .claude/docs (the telnet `g` inject vectors)."""

from datetime import datetime

import nmea
import pytest


# These are the exact bodies/checksums the controller computes and the plan doc
# documents. If either side drifts, this fails loudly (the whole point of having
# one wire contract implemented three times).
CHECKSUM_VECTORS = [
    ("GPWPL,3723.460,N,12158.940,W,WPT01", "0E"),
    ("GPRMB,A,0.66,L,WPT01,WPT02,3729.000,N,12200.000,W,5.2,231.5,1.1,V", "2B"),
    ("GPBOD,231.5,T,217.2,M,WPT02,WPT01", "47"),
    ("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", "6A"),
    ("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", "47"),
]


@pytest.mark.parametrize("body,expected", CHECKSUM_VECTORS)
def test_checksum_matches_controller(body, expected):
    assert nmea.checksum(body) == expected
    assert nmea.frame(body) == f"${body}*{expected}"
    assert nmea.verify(f"${body}*{expected}")


def test_verify_rejects_corruption():
    line = nmea.frame("GPWPL,3723.460,N,12158.940,W,WPT01")
    assert nmea.verify(line)
    assert not nmea.verify(line[:-1] + ("0" if line[-1] != "0" else "1"))
    assert not nmea.verify("GPWPL,no,dollar*00")     # missing leading $
    assert not nmea.verify("$GPWPL,no,star")         # missing *HH


def test_verify_tolerates_crlf():
    assert nmea.verify(nmea.wpl(37.391, -121.98233, "WPT01") + "\r\n")


# -- relay classification (mirrors garmin_should_relay) ---------------------

@pytest.mark.parametrize("stype,relayed", [
    ("WPL", True), ("RTE", True), ("RMB", True), ("XTE", True), ("BOD", True),
    ("RMC", False), ("GGA", False), ("GLL", False), ("VTG", False),
])
def test_relay_types(stype, relayed):
    line = nmea.frame(f"GP{stype},foo,bar")
    assert nmea.is_relay_type(line) is relayed


def test_relay_ignores_talker_id():
    assert nmea.is_relay_type(nmea.frame("IIRMB,A"))   # Integrated Instrument talker
    assert nmea.is_relay_type(nmea.frame("ECRTE,1,1,c,R"))


# -- coordinates ------------------------------------------------------------

def test_format_lat_lon_exact():
    assert nmea.format_lat(37.391) == ("3723.460", "N")
    assert nmea.format_lat(-37.391) == ("3723.460", "S")
    assert nmea.format_lon(-121.982333) == ("12158.940", "W")


@pytest.mark.parametrize("lat,lon", [
    (37.808, -122.470), (-33.8688, 151.2093), (0.0, 0.0), (37.391, -121.982333),
])
def test_coord_round_trip(lat, lon):
    la, ns = nmea.format_lat(lat)
    lo, ew = nmea.format_lon(lon)
    assert nmea.parse_coord(la, ns) == pytest.approx(lat, abs=1e-4)
    assert nmea.parse_coord(lo, ew) == pytest.approx(lon, abs=1e-4)


# -- builder/parser round trips ---------------------------------------------

def test_wpl_round_trip():
    line = nmea.wpl(37.808, -122.470, "WPT01")
    assert nmea.verify(line)
    p = nmea.parse(line)
    assert p["type"] == "WPL" and p["name"] == "WPT01"
    assert p["lat"] == pytest.approx(37.808, abs=1e-4)
    assert p["lon"] == pytest.approx(-122.470, abs=1e-4)


def test_rmb_round_trip():
    line = nmea.rmb("A", 0.66, "L", "WPT01", "WPT02", 37.483, -122.0, 5.2,
                    231.5, 6.0, "V")
    p = nmea.parse(line)
    assert nmea.verify(line)
    assert p["status"] == "A" and p["arrival"] == "V"
    assert p["dest_id"] == "WPT02" and p["origin_id"] == "WPT01"
    assert p["dest_lat"] == pytest.approx(37.483, abs=1e-4)
    assert p["steer"] == "L"


def test_xte_and_bod_round_trip():
    x = nmea.parse(nmea.xte(0.66, "L"))
    assert x["magnitude_nm"] == pytest.approx(0.66) and x["steer"] == "L"
    b = nmea.parse(nmea.bod(231.5, "WPT02", "WPT01"))
    assert b["bearing_true"] == pytest.approx(231.5)
    assert b["dest_id"] == "WPT02" and b["origin_id"] == "WPT01"


def test_rmc_gga_round_trip():
    when = datetime(2026, 6, 28, 12, 35, 19)
    r = nmea.parse(nmea.rmc(when, 37.808, -122.470, 6.0, 231.0))
    assert r["lat"] == pytest.approx(37.808, abs=1e-4) and r["status"] == "A"
    g = nmea.parse(nmea.gga(when, 37.808, -122.470))
    assert g["fix_quality"] == 1 and g["num_sat"] == 8


# -- route serialization ----------------------------------------------------

def test_serialize_route():
    route = [("WPT01", 37.808, -122.470), ("WPT02", 37.700, -122.520),
             ("WPT03", 37.600, -122.600)]
    lines = nmea.serialize_route(route, "GARMN1")
    wpls = [l for l in lines if nmea.sentence_type(l) == "WPL"]
    rtes = [l for l in lines if nmea.sentence_type(l) == "RTE"]
    assert len(wpls) == 3 and len(rtes) >= 1
    assert all(nmea.verify(l) for l in lines)
    # RTE reassembly recovers all waypoint names in order.
    names = []
    for l in sorted(rtes, key=lambda s: nmea.parse(s)["num"]):
        names += nmea.parse(l)["waypoints"]
    assert names == ["WPT01", "WPT02", "WPT03"]


def test_rte_splits_long_routes_within_length():
    ids = [f"WP{i:02d}" for i in range(40)]
    sentences = nmea.rte(ids, "BIGRTE")
    assert len(sentences) > 1
    assert all(len(s) <= 82 for s in sentences)
    assert all(nmea.verify(s) for s in sentences)
    totals = {nmea.parse(s)["total"] for s in sentences}
    assert totals == {len(sentences)}
    nums = sorted(nmea.parse(s)["num"] for s in sentences)
    assert nums == list(range(1, len(sentences) + 1))
    recovered = []
    for s in sorted(sentences, key=lambda x: nmea.parse(x)["num"]):
        recovered += nmea.parse(s)["waypoints"]
    assert recovered == ids
