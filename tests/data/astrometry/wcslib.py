# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Writes wcslib.csv: linear astrometric solutions (the first layer of spec §11.5.3.7) evaluated by WCSLIB, the
implementation of the WCS formulation of celestial coordinate systems that the specification follows, through astropy.

The solutions use every projection of the specification, reference points near the poles and the equator, and native
coordinates of the reference point and of the celestial pole other than the defaults, which the samples of PixInsight
cannot provide. A solution maps to WCS keywords as follows: ProjectionSystem to CTYPE (TAN, STG, ZEA, SIN, CAR, MER,
AIT), ReferenceCelestialCoordinates to CRVAL, LinearTransformationMatrix to CD, ReferenceImageCoordinates plus 0.5 to
CRPIX (the centre of the first pixel is at (0.5, 0.5) in image coordinates and at (1, 1) in FITS pixel coordinates),
ReferenceNativeCoordinates to PV1_1 and PV1_2, and CelestialPoleNativeCoordinates to LONPOLE and LATPOLE. WCSLIB applies
no offset to the projection plane (PV1_0 = 0).

Lines of the output:

    solution,<name>,<projection>,<ra0>,<dec0>,<m00>,<m01>,<m10>,<m11>,<x0>,<y0>,<phi0>,<theta0>,<phip>,<thetap>
    invalid,<name>,<same fields>        parameters that define no spherical rotation, which WCSLIB refuses
    image,<name>,<x>,<y>,<ra>,<dec>     right ascension and declination of an image point; empty when there are none
    celestial,<name>,<ra>,<dec>,<x>,<y> image point of a sky position; empty when there is none

Empty native coordinates are left out of the solution. Run with Python 3 and astropy (pip install astropy):

    python wcslib.py wcslib.csv
"""

import math
import random
import sys
import warnings

import numpy as np
from astropy.wcs import WCS, FITSFixedWarning

warnings.simplefilter("ignore", FITSFixedWarning)

CODES = {
    "Gnomonic": "TAN",
    "Stereographic": "STG",
    "ZenithalEqualArea": "ZEA",
    "Orthographic": "SIN",
    "PlateCarree": "CAR",
    "Mercator": "MER",
    "HammerAitoff": "AIT",
}
ZENITHAL = ("Gnomonic", "Stereographic", "ZenithalEqualArea", "Orthographic")
WIDTH, HEIGHT = 400.0, 300.0
X0, Y0 = 200.0, 150.0


def text(value):
    return "" if value is None else repr(float(value))


def make_wcs(case):
    w = WCS(naxis=2)
    code = CODES[case["projection"]]
    w.wcs.ctype = ["RA---" + code, "DEC--" + code]
    w.wcs.crval = [case["ra0"], case["dec0"]]
    w.wcs.crpix = [X0 + 0.5, Y0 + 0.5]
    w.wcs.cd = np.array(case["matrix"])
    if case["phi0"] is not None:
        w.wcs.set_pv([(1, 1, case["phi0"]), (1, 2, case["theta0"])])
    if case["phip"] is not None:
        w.wcs.lonpole = case["phip"]
        w.wcs.latpole = case["thetap"]
    w.wcs.set()
    return w


def matrix(scale, rotation):
    t = math.radians(rotation)
    return [[-scale * math.cos(t), scale * math.sin(t)], [-scale * math.sin(t), -scale * math.cos(t)]]


def case(name, projection, ra0, dec0, scale, rotation, phi0=None, theta0=None, phip=None, thetap=None):
    return {"name": name, "projection": projection, "ra0": ra0, "dec0": dec0, "matrix": matrix(scale, rotation),
            "phi0": phi0, "theta0": theta0, "phip": phip, "thetap": thetap}


def cases():
    result = []
    # Every projection with the native longitude of the celestial pole other than the default, and the latitude that
    # selects one of the two solutions for the native pole.
    for projection in CODES:
        zenithal = projection in ZENITHAL
        tag = projection
        result.append(case(tag + "-pole-90", projection, 40.0, 30.0, 0.05, 10.0, phip=90.0, thetap=90.0))
        result.append(case(tag + "-pole-minus-135", projection, 300.0, -20.0, 0.08, -25.0, phip=-135.0, thetap=-60.0))
        result.append(case(tag + "-pole-0-south", projection, 150.0, -70.0, 0.04, 120.0, phip=0.0, thetap=-90.0))
        if zenithal:
            # The reference point away from the native pole: the projection plane keeps its origin at the pole.
            result.append(case(tag + "-oblique", projection, 210.0, 15.0, 0.03, 0.0, phi0=0.0, theta0=70.0))
            result.append(case(tag + "-oblique-pole", projection, 60.0, -40.0, 0.03, 45.0, phi0=30.0, theta0=60.0,
                               phip=150.0, thetap=10.0))
        else:
            result.append(case(tag + "-native-30", projection, 100.0, 30.0, 0.15, 20.0, phi0=0.0, theta0=30.0))
            result.append(case(tag + "-native-20-minus-45", projection, 330.0, -10.0, 0.2, -10.0, phi0=20.0,
                               theta0=-45.0, phip=-30.0, thetap=40.0))
            # theta0 = 0 and |phip - phi0| = 90 define no rotation off the celestial equator. On it, where the
            # specification and the WCS formulation take deltap = LATPOLE, WCSLIB gives ±90 instead, the candidate
            # closest to LATPOLE that a tiny cos(90 degrees) gives, so that case is left out.
            result.append(case(tag + "-equator-invalid", projection, 75.0, 30.0, 0.1, 5.0, phi0=0.0, theta0=0.0,
                               phip=90.0, thetap=45.0))
    # Random native coordinates.
    rng = random.Random(13)
    for i in range(40):
        projection = rng.choice(list(CODES))
        zenithal = projection in ZENITHAL
        ra0 = round(rng.uniform(0.0, 360.0), 6)
        dec0 = round(rng.choice([rng.uniform(-90.0, 90.0), rng.uniform(80.0, 90.0), rng.uniform(-90.0, -80.0)]), 6)
        phi0 = theta0 = phip = thetap = None
        if rng.random() < 0.6:
            phip = round(rng.uniform(-180.0, 180.0), 3)
            thetap = round(rng.uniform(-90.0, 90.0), 3)
        if not zenithal and rng.random() < 0.5:
            phi0 = 0.0
            theta0 = round(rng.uniform(-60.0, 60.0), 3)
        result.append(case("random-%d" % i, projection, ra0, dec0, round(rng.uniform(0.002, 0.25), 6),
                           round(rng.uniform(-180.0, 180.0), 3), phi0, theta0, phip, thetap))
    return result


def main():
    out = open(sys.argv[1], "w", encoding="ascii", newline="\n")
    out.write("# WCSLIB through astropy: linear solutions (layer 1). Written by wcslib.py.\n")
    points = [(x, y) for y in (0.75, HEIGHT / 2 + 0.75, HEIGHT + 0.75) for x in (0.25, WIDTH / 2 + 0.25, WIDTH + 0.25)]
    points += [(-150.5, 75.25), (550.125, 380.5)]
    for c in cases():
        m = c["matrix"]
        fields = [c["name"], c["projection"], text(c["ra0"]), text(c["dec0"]), text(m[0][0]), text(m[0][1]),
                  text(m[1][0]), text(m[1][1]), text(X0), text(Y0), text(c["phi0"]), text(c["theta0"]),
                  text(c["phip"]), text(c["thetap"])]
        try:
            w = make_wcs(c)
        except Exception:
            out.write("invalid," + ",".join(fields) + "\n")
            continue
        out.write("solution," + ",".join(fields) + "\n")
        positions = []
        for x, y in points:
            ra, dec = w.all_pix2world([[x + 0.5, y + 0.5]], 1)[0]
            if np.isfinite(ra) and np.isfinite(dec):
                out.write("image,%s,%r,%r,%r,%r\n" % (c["name"], x, y, float(ra), float(dec)))
                positions.append((round(float(ra), 6), round(float(dec), 6)))
            else:
                out.write("image,%s,%r,%r,,\n" % (c["name"], x, y))
        # Sky positions: those of some image points, and one beyond the hemisphere of the reference point.
        positions = positions[::2]
        positions.append(((c["ra0"] + 100.0) % 360.0, -c["dec0"] / 2.0))
        for ra, dec in positions:
            x, y = w.all_world2pix([[ra, dec]], 1)[0]
            if np.isfinite(x) and np.isfinite(y):
                out.write("celestial,%s,%r,%r,%r,%r\n" % (c["name"], ra, dec, float(x) - 0.5, float(y) - 0.5))
            else:
                out.write("celestial,%s,%r,%r,,\n" % (c["name"], ra, dec))
    out.close()


if __name__ == "__main__":
    main()
