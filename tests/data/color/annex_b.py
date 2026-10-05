# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

"""Writes annex_b.txt: reference values of the colour transformations of Annex B of the XISF 1.0 specification.

It is an independent implementation of the equations, in decimal arithmetic of 50 digits, so that the tests compare the
library with values that no double computation produced. Run it with Python 3 from any directory:

    python annex_b.py

Each line of the output is a working space, a transformation, its input and its output, as 17 significant digits:

    <space> rgb <R> <G> <B> xyz <X> <Y> <Z> lab <L> <a> <b> gray <K>
    <space> lab <L> <a> <b> rgb <R> <G> <B>
"""

from decimal import Decimal, getcontext
from pathlib import Path

getcontext().prec = 50

D = Decimal
ZERO = D(0)
ONE = D(1)

# The D50 reference white of spec §8.5.4.1.
XW = D("0.96422")
ZW = D("0.82521")

# The working spaces: name, gamma (None for the sRGB functions), x and y chromaticities of the primaries. sRGB and Adobe
# RGB (1998) are the examples of spec §8.5.4.1 and §11.8.2; ProPhoto RGB is defined relative to D50.
SPACES = [
    ("srgb", None, ("0.648431", "0.321152", "0.155886"), ("0.330856", "0.597871", "0.066044")),
    ("linear-srgb", D(1), ("0.648431", "0.321152", "0.155886"), ("0.330856", "0.597871", "0.066044")),
    ("adobe-rgb", D("2.2"), ("0.648431", "0.230154", "0.155886"), ("0.330856", "0.701572", "0.066044")),
    ("prophoto-rgb", D("1.8"), ("0.7347", "0.1596", "0.0366"), ("0.2653", "0.8404", "0.0001")),
]

RGB_INPUTS = [
    ("0", "0", "0"),
    ("1", "1", "1"),
    ("1", "0", "0"),
    ("0", "1", "0"),
    ("0", "0", "1"),
    ("1", "1", "0"),
    ("0", "1", "1"),
    ("0.5", "0.5", "0.5"),
    ("0.04045", "0.04", "0.05"),
    ("0.2", "0.4", "0.6"),
    ("0.9", "0.1", "0.3"),
    ("0.01", "0.02", "0.03"),
    ("0.001", "0.0005", "0.002"),
    ("0.75", "0.6", "0.05"),
    # A gray whose Y lies just below ε, where the two parts of f() meet, in sRGB.
    ("0.092201", "0.092201", "0.092201"),
]

LAB_INPUTS = [
    ("0", "0.5", "0.5"),
    ("1", "0.5", "0.5"),
    ("0.5", "0.5", "0.5"),
    ("0.7", "0.6", "0.3"),
    ("0.05", "0.52", "0.47"),
    ("0.3", "0.8", "0.2"),
    ("0.5", "1", "0"),
    ("0.95", "0", "1"),
    ("0.02", "0.5", "0.5"),
    # The cube of f_Y just below ε, where the two parts of g() meet.
    ("0.079985", "0.5", "0.5"),
]


def det3(m):
    return (
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    )


def inverse3(m):
    det = det3(m)
    cofactors = [[ZERO] * 3 for _ in range(3)]
    for r in range(3):
        for c in range(3):
            minor = [[m[i][j] for j in range(3) if j != c] for i in range(3) if i != r]
            sign = ONE if (r + c) % 2 == 0 else -ONE
            cofactors[r][c] = sign * (minor[0][0] * minor[1][1] - minor[0][1] * minor[1][0])
    # The inverse is the transposed matrix of cofactors divided by the determinant.
    return [[cofactors[c][r] / det for c in range(3)] for r in range(3)]


def apply(m, v):
    return [sum(m[r][c] * v[c] for c in range(3)) for r in range(3)]


def luminance(x, y):
    # Spec §8.5.4.1, equation [3]: the columns are the tristimulus values of the primaries per unit of luminance.
    system = [[x[i] / y[i] for i in range(3)], [ONE] * 3, [(ONE - x[i] - y[i]) / y[i] for i in range(3)]]
    white = [XW, ONE, ZW]
    whole = det3(system)
    result = []
    for i in range(3):
        replaced = [row[:] for row in system]
        for r in range(3):
            replaced[r][i] = white[r]
        result.append(det3(replaced) / whole)
    return result


def clip(v):
    return min(ONE, max(ZERO, v))


def linearize(v, gamma):
    if gamma is None:
        if v <= D("0.04045"):
            return v / D("12.92")
        return ((v + D("0.055")) / D("1.055")) ** D("2.4")
    return v**gamma if v > ZERO else ZERO


def delinearize(v, gamma):
    v = clip(v)
    if gamma is None:
        if v <= D("0.0031308"):
            return D("12.92") * v
        return D("1.055") * v ** (ONE / D("2.4")) - D("0.055")
    return v ** (ONE / gamma) if v > ZERO else ZERO


EPSILON = D(216) / D(24389)
KAPPA = D(24389) / D(27)


def f(t):
    if t > EPSILON:
        return t ** (ONE / D(3)) if t > ZERO else ZERO
    return (KAPPA * t + D(16)) / D(116)


def g(t):
    cube = t * t * t
    if cube > EPSILON:
        return cube
    return (D(116) * t - D(16)) / KAPPA


def xyz_to_lab(xyz):
    fx, fy, fz = f(xyz[0] / XW), f(xyz[1]), f(xyz[2] / ZW)
    half = ONE / D(2)
    k = D(29) / D(50)
    return [clip(D("1.16") * fy - D("0.16")), clip(half + k * (fx - fy)), clip(half + k * (fy - fz))]


def lab_to_xyz(lab):
    fy = (lab[0] + D("0.16")) / D("1.16")
    fx = fy + D(50) / D(29) * (lab[1] - ONE / D(2))
    fz = fy - D(50) / D(29) * (lab[2] - ONE / D(2))
    return [XW * g(fx), g(fy), ZW * g(fz)]


def text(v):
    return format(float(v), ".17g")


def main():
    lines = []
    for name, gamma, xs, ys in SPACES:
        x = [D(v) for v in xs]
        y = [D(v) for v in ys]
        lum = luminance(x, y)
        m = [[lum[i] * x[i] / y[i] for i in range(3)], lum, [lum[i] * (ONE - x[i] - y[i]) / y[i] for i in range(3)]]
        inverse = inverse3(m)
        for rgb_text in RGB_INPUTS:
            rgb = [D(v) for v in rgb_text]
            linear = [linearize(v, gamma) for v in rgb]
            xyz = apply(m, linear)
            lab = xyz_to_lab(xyz)
            gray = clip(D("1.16") * f(sum(lum[i] * linear[i] for i in range(3))) - D("0.16"))
            lines.append(
                " ".join([name, "rgb", *rgb_text, "xyz", *map(text, xyz), "lab", *map(text, lab), "gray", text(gray)])
            )
        for lab_text in LAB_INPUTS:
            lab = [D(v) for v in lab_text]
            rgb = [delinearize(v, gamma) for v in apply(inverse, lab_to_xyz(lab))]
            lines.append(" ".join([name, "lab", *lab_text, "rgb", *map(text, rgb)]))
    target = Path(__file__).with_name("annex_b.txt")
    with open(target, "w", encoding="ascii", newline="\n") as output:
        output.write("# Written by annex_b.py: Annex B in decimal arithmetic of 50 digits.\n")
        output.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
