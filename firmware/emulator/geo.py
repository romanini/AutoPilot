"""Great-circle navigation helpers for the Garmin emulator.

Just enough geodesy to drive a believable RMB/XTE/BOD stream: bearing, distance,
cross-track error, and dead-reckoning a position forward along a leg. The
`bearing` formula mirrors the controller's `GeodesicBearing` so the emulated
Garmin and the controller agree on leg courses.

Distances are nautical miles (NMEA's native unit for RMB range / XTE).
"""

from __future__ import annotations

import math

EARTH_RADIUS_NM = 3440.065


def _r(d: float) -> float:
    return math.radians(d)


def _d(r: float) -> float:
    return math.degrees(r)


def bearing(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Initial true bearing from point 1 to point 2, in [0, 360)."""
    y = math.sin(_r(lon2 - lon1)) * math.cos(_r(lat2))
    x = (math.cos(_r(lat1)) * math.sin(_r(lat2))
         - math.sin(_r(lat1)) * math.cos(_r(lat2)) * math.cos(_r(lon2 - lon1)))
    return (_d(math.atan2(y, x)) + 360.0) % 360.0


def distance_nm(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Great-circle distance in nautical miles (haversine)."""
    dlat = _r(lat2 - lat1)
    dlon = _r(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(_r(lat1)) * math.cos(_r(lat2)) * math.sin(dlon / 2) ** 2)
    return 2 * EARTH_RADIUS_NM * math.asin(min(1.0, math.sqrt(a)))


def cross_track_nm(lat: float, lon: float,
                   lat1: float, lon1: float, lat2: float, lon2: float):
    """Signed cross-track distance (nm) of a point from the leg start->end.

    Returns (magnitude_nm, steer) where `steer` is the direction the boat must
    turn to regain track: 'L' if it is right of the intended track, 'R' if left
    (the NMEA RMB/XTE convention — steer toward the line).
    """
    d13 = distance_nm(lat1, lon1, lat, lon) / EARTH_RADIUS_NM   # angular
    t13 = _r(bearing(lat1, lon1, lat, lon))
    t12 = _r(bearing(lat1, lon1, lat2, lon2))
    xt = math.asin(math.sin(d13) * math.sin(t13 - t12)) * EARTH_RADIUS_NM
    # xt > 0 -> boat is right of track -> steer left to correct.
    steer = "L" if xt > 0 else "R"
    return abs(xt), steer


def destination_point(lat: float, lon: float, bearing_deg: float, dist_nm: float):
    """Point reached starting at lat/lon, going `bearing_deg` for `dist_nm`."""
    ang = dist_nm / EARTH_RADIUS_NM
    brg = _r(bearing_deg)
    lat1 = _r(lat)
    lon1 = _r(lon)
    lat2 = math.asin(math.sin(lat1) * math.cos(ang)
                     + math.cos(lat1) * math.sin(ang) * math.cos(brg))
    lon2 = lon1 + math.atan2(math.sin(brg) * math.sin(ang) * math.cos(lat1),
                             math.cos(ang) - math.sin(lat1) * math.sin(lat2))
    return _d(lat2), (_d(lon2) + 540.0) % 360.0 - 180.0
