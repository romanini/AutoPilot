"""Tests for geo.py navigation helpers."""

import geo
import pytest


def test_bearing_cardinals():
    assert geo.bearing(0, 0, 1, 0) == pytest.approx(0.0, abs=0.1)      # north
    assert geo.bearing(0, 0, 0, 1) == pytest.approx(90.0, abs=0.1)     # east
    assert geo.bearing(0, 0, -1, 0) == pytest.approx(180.0, abs=0.1)   # south
    assert geo.bearing(0, 0, 0, -1) == pytest.approx(270.0, abs=0.1)   # west


def test_distance_one_degree_latitude():
    # 1 degree of latitude is ~60 nm.
    assert geo.distance_nm(0, 0, 1, 0) == pytest.approx(60.0, abs=0.2)


def test_distance_symmetric_and_zero():
    assert geo.distance_nm(37.8, -122.4, 37.8, -122.4) == pytest.approx(0.0, abs=1e-6)
    d1 = geo.distance_nm(37.8, -122.4, 37.6, -122.6)
    d2 = geo.distance_nm(37.6, -122.6, 37.8, -122.4)
    assert d1 == pytest.approx(d2, abs=1e-6)


def test_destination_point_round_trip():
    lat, lon = geo.destination_point(0.0, 0.0, 90.0, 60.0)
    assert lat == pytest.approx(0.0, abs=0.01)
    assert lon == pytest.approx(1.0, abs=0.02)
    # Going out and coming back returns to start.
    lat2, lon2 = geo.destination_point(37.8, -122.4, 45.0, 5.0)
    back = geo.bearing(lat2, lon2, 37.8, -122.4)
    lat3, lon3 = geo.destination_point(lat2, lon2, back,
                                       geo.distance_nm(lat2, lon2, 37.8, -122.4))
    assert lat3 == pytest.approx(37.8, abs=1e-3)
    assert lon3 == pytest.approx(-122.4, abs=1e-3)


def test_cross_track_on_track_is_zero():
    # A point on the leg (0,0)->(0,1) has ~zero cross-track.
    mag, _ = geo.cross_track_nm(0.0, 0.5, 0.0, 0.0, 0.0, 1.0)
    assert mag == pytest.approx(0.0, abs=1e-3)


def test_cross_track_sign():
    # North of an eastbound leg -> steer right (turn south) to regain track.
    mag, steer = geo.cross_track_nm(0.01, 0.5, 0.0, 0.0, 0.0, 1.0)
    assert steer == "R"
    assert mag == pytest.approx(0.6, abs=0.1)   # 0.01 deg ~ 0.6 nm
    # South of the same leg -> steer left.
    _, steer2 = geo.cross_track_nm(-0.01, 0.5, 0.0, 0.0, 0.0, 1.0)
    assert steer2 == "L"
