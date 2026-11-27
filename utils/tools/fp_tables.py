"""Print the tables and the coefficients of the elementary functions of include/fixy/fp/.

The headers include/fixy/fp/Exp.h, Log.h and Erf.h hold constant tables and
polynomial coefficients.  This script computes each value with mpmath at 256
bits and prints it as C++ text: a double as a hexadecimal float literal, the
bits of a double as a 64-bit hexadecimal integer.  A person copies the printed
text into the header.  The script writes no file.

Each value is the double nearest its stated real value, unless the comment of
the table states a different rule (a value cut to a number of bits, or to a
multiple of a power of two).  The polynomials are minimax polynomials of the
Remez exchange, with the weight that the comment of each fit states.  After the
round of each coefficient to a double, the script measures the error of the
rounded polynomial on a grid and prints it.

The script runs with the pinned mpmath, which `python3 utils/bin/setup install
mpmath` installs.  It finds the package through `setup install --print-bin
mpmath`, so it reads no mpmath of the Python of the user.

    python3 utils/tools/fp_tables.py exp     the tables of Exp.h
    python3 utils/tools/fp_tables.py log     the tables of Log.h, double
    python3 utils/tools/fp_tables.py logf    the tables of Log.h, float
    python3 utils/tools/fp_tables.py erf     the tables of Erf.h, double
    python3 utils/tools/fp_tables.py erff    the polynomial and the table of Erf.h, float

The output of one subcommand is the same on each run and on each host,
because mpmath computes with integers only.  A run takes some minutes.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from collections.abc import Callable, Sequence
from fractions import Fraction
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
SETUP = ROOT / "utils" / "bin" / "setup"

# The working precision of mpmath, in bits.
PRECISION = 256
# The points of the grid of the search for the extrema of an error curve.
GRID = 3000


def load_mpmath() -> Any:
    """Import the pinned mpmath from its site directory and return the module.

    Raises:
        SystemExit: If the pinned mpmath is not installed
    """
    found = subprocess.run(
        [sys.executable, "-S", str(SETUP), "install", "--print-bin", "mpmath"],
        capture_output=True,
        text=True,
        check=False,
    )
    site = found.stdout.strip()
    if found.returncode != 0 or not site:
        raise SystemExit("fp_tables: the pinned mpmath is not installed.  Run: python3 utils/bin/setup install mpmath")
    sys.path.insert(0, site)
    import mpmath

    return mpmath


mp: Any = None


def round_double(value: Any) -> float:
    """Return the double nearest an mpmath value, with ties to even.

    The conversion goes through an exact fraction, and Python rounds a
    fraction to the nearest double correctly.  The tuple _mpf_ of mpmath holds
    the sign apart from the mantissa: the property man_exp drops the sign.
    """
    sign, mantissa, exponent, _ = mp.mpf(value)._mpf_
    exact = Fraction(int(mantissa)) * Fraction(2) ** int(exponent)
    return float(-exact if sign else exact)


def round_to_multiple(value: Any, step_exponent: int) -> Any:
    """Return the multiple of 2**step_exponent nearest value, as an exact mpmath value."""
    scale = mp.ldexp(mp.mpf(1), -step_exponent)
    return mp.nint(value * scale) / scale


def round_to_bits(value: Any, bits: int) -> Any:
    """Return value rounded to the nearest number with `bits` significant bits, as an exact mpmath value."""
    if value == 0:
        return mp.mpf(0)
    exponent = int(mp.floor(mp.log(abs(value), 2)))
    return round_to_multiple(value, exponent - bits + 1)


def bits_of(value: float) -> int:
    """Return the IEEE 754 bits of a double as an unsigned integer."""
    return int(struct.unpack("<Q", struct.pack("<d", value))[0])


def float_bits_of(value: float) -> int:
    """Return the IEEE 754 bits of the float nearest a double as an unsigned integer."""
    return int(struct.unpack("<I", struct.pack("<f", value))[0])


def hex_double(value: float) -> str:
    """Return a double as a C++ hexadecimal float literal."""
    if value == 0.0:
        return "-0.0" if struct.pack("<d", value)[7] & 0x80 else "0.0"
    return value.hex()


def hex_word(value: int) -> str:
    """Return a 64-bit word as a C++ unsigned literal."""
    return f"0x{value & 0xFFFFFFFFFFFFFFFF:016X}ULL"


def log2_text(value: Any) -> str:
    """Return the base-2 logarithm of a positive error as text, or -inf for zero."""
    if value == 0:
        return "-inf"
    return f"{float(mp.log(abs(value), 2)):.2f}"


# ── The Remez exchange ─────────────────────────────────────────────


def polynomial_value(coefficients: Sequence[Any], point: Any) -> Any:
    """Return the value of a polynomial at a point, the constant term first.  O(degree)."""
    total = mp.mpf(0)
    for coefficient in reversed(coefficients):
        total = total * point + coefficient
    return total


def alternating_extrema(error: Callable[[Any], Any], low: Any, high: Any, count: int) -> list[Any]:
    """Return `count` points of alternating sign of the error curve where its magnitude is largest.

    A grid finds one extremum in each run of one sign, and a golden-section
    search refines it.  When there are more runs than `count`, the run with the
    smallest extremum at either end goes, until `count` runs stay.
    O(GRID) error evaluations.

    Raises:
        SystemExit: If the curve has fewer runs of one sign than `count`
    """
    points = [low + (high - low) * index / GRID for index in range(GRID + 1)]
    values = [error(point) for point in points]
    # A zero of the curve, such as a zero of the weight, keeps the sign of the run before it, so it starts no run.
    signs: list[bool] = []
    for value in values:
        signs.append(value > 0 if value != 0 or not signs else signs[-1])
    runs: list[tuple[int, int]] = []
    start = 0
    for index in range(1, GRID + 2):
        if index == GRID + 1 or signs[index] != signs[start]:
            best = max(range(start, index), key=lambda position: abs(values[position]))
            runs.append((best, 1 if signs[best] else -1))
            start = index
    if len(runs) < count:
        raise SystemExit(f"fp_tables: the error curve has {len(runs)} runs of one sign, the fit needs {count}.")
    while len(runs) > count:
        if abs(values[runs[0][0]]) < abs(values[runs[-1][0]]):
            runs.pop(0)
        else:
            runs.pop()
    refined: list[Any] = []
    for position, sign in runs:
        left = points[max(position - 1, 0)]
        right = points[min(position + 1, GRID)]
        refined.append(golden_maximum(lambda point, sign=sign: sign * error(point), left, right))
    return refined


def golden_maximum(function: Callable[[Any], Any], left: Any, right: Any) -> Any:
    """Return the point of the maximum of a unimodal function on [left, right].  60 evaluations."""
    ratio = (mp.sqrt(5) - 1) / 2
    first = right - ratio * (right - left)
    second = left + ratio * (right - left)
    first_value = function(first)
    second_value = function(second)
    for _ in range(60):
        if first_value < second_value:
            left, first, first_value = first, second, second_value
            second = left + ratio * (right - left)
            second_value = function(second)
        else:
            right, second, second_value = second, first, first_value
            first = right - ratio * (right - left)
            first_value = function(first)
    return (left + right) / 2


def remez(target: Callable[[Any], Any], weight: Callable[[Any], Any], low: Any, high: Any, degree: int) -> list[Any]:
    """Return the coefficients of the polynomial p of a degree that minimizes max |weight * (target - p)|.

    The constant term comes first.  The exchange starts at the Chebyshev
    points, moved a little so that no point falls on a zero of the weight, and
    it stops when the largest error is within 2**-20 of the level of the
    reference.  O(iterations * GRID) evaluations.
    """
    count = degree + 2
    reference = [
        (low + high) / 2 - (high - low) / 2 * mp.cos(mp.pi * (index + mp.mpf("0.0137")) / (count - mp.mpf("0.97")))
        for index in range(count)
    ]
    coefficients: list[Any] = []
    for _ in range(40):
        matrix = mp.matrix(count, count)
        vector = mp.matrix(count, 1)
        for row, point in enumerate(reference):
            for column in range(degree + 1):
                matrix[row, column] = point**column
            matrix[row, degree + 1] = (-1) ** row / weight(point)
            vector[row] = target(point)
        solution = mp.lu_solve(matrix, vector)
        coefficients = [solution[index] for index in range(degree + 1)]
        level = abs(solution[degree + 1])

        def error(point: Any, fit: list[Any] = coefficients) -> Any:
            return weight(point) * (target(point) - polynomial_value(fit, point))

        reference = alternating_extrema(error, low, high, count)
        largest = max(abs(error(point)) for point in reference)
        if largest - level <= level * mp.mpf(2) ** -20:
            break
    return coefficients


def measured_error(
    target: Callable[[Any], Any], weight: Callable[[Any], Any], low: Any, high: Any, coefficients: Sequence[float]
) -> Any:
    """Return the largest weighted error of a polynomial with double coefficients on a fine grid.  O(4 * GRID)."""
    steps = 4 * GRID
    largest = mp.mpf(0)
    for index in range(steps + 1):
        point = low + (high - low) * index / steps
        exact = polynomial_value([mp.mpf(coefficient) for coefficient in coefficients], point)
        largest = max(largest, abs(weight(point) * (target(point) - exact)))
    return largest


# ── The rigorous bound ─────────────────────────────────────────────
#
# A bound of this section holds for each point of its interval, not for the points of a grid.  The arithmetic is
# exact: each value is a Fraction, each double converts to its exact Fraction, and each constant that mpmath gives
# rounds up to a dyadic Fraction.


def fraction_of(value: float | Fraction) -> Fraction:
    """Return the exact value of a double, or of a sum of doubles as a Fraction, as a Fraction."""
    return Fraction(value)


def taylor_shift(coefficients: Sequence[Fraction], center: Fraction) -> list[Fraction]:
    """Return the coefficients in t of p(center + t), the constant term first.  Exact.  O(degree^2)."""
    shifted = list(coefficients)
    degree = len(shifted) - 1
    for start in range(degree):
        for index in range(degree - 1, start - 1, -1):
            shifted[index] += center * shifted[index + 1]
    return shifted


def sup_bound(coefficients: Sequence[Fraction], low: Fraction, high: Fraction, pieces: int) -> Fraction:
    """Return an upper bound of max |p(x)| on [low, high] for p with exact coefficients.

    The interval divides into `pieces` equal pieces.  On a piece with center c and half-width h, p(c + t) is
    sum d_k t^k with exact d_k, and |p(c + t)| <= sum |d_k| h^k for each |t| <= h.  The bound is the largest such
    sum.  O(pieces * degree^2).
    """
    width = (high - low) / pieces
    half = width / 2
    largest = Fraction(0)
    for piece in range(pieces):
        center = low + width * piece + half
        bound = Fraction(0)
        power = Fraction(1)
        for coefficient in taylor_shift(coefficients, center):
            bound += abs(coefficient) * power
            power *= half
        largest = max(largest, bound)
    return largest


def rigorous_exp_error(
    coefficients: Sequence[float | Fraction], bound: Fraction, terms: int = 24, pieces: int = 512
) -> Fraction:
    """Return a bound of |exp(r) - p(r)| / exp(r) for each |r| <= bound.

    Each coefficient of p is a double or an exact sum of doubles.

    exp(r) is the Taylor sum T(r) of its first `terms` terms plus a rest.  For bound <= 1/2 the rest is at most
    2 bound^terms / terms!.  The bound of |T - p| comes from sup_bound, and exp(r) >= 1 - bound.  Exact.
    O(pieces * terms^2).
    """
    taylor = [Fraction(1, mp_factorial(n)) for n in range(terms)]
    difference = [taylor[n] - (fraction_of(coefficients[n]) if n < len(coefficients) else 0) for n in range(terms)]
    rest = 2 * bound**terms / mp_factorial(terms)
    return (sup_bound(difference, -bound, bound, pieces) + rest) / (1 - bound)


def mp_factorial(count: int) -> int:
    """Return count! as an exact integer."""
    product = 1
    for factor in range(2, count + 1):
        product *= factor
    return product


def dyadic_above(value: Any, bits: int = 64) -> Fraction:
    """Return a dyadic Fraction at or above a positive mpmath value, with `bits` significant bits."""
    exponent = int(mp.floor(mp.log(value, 2)))
    scale = mp.ldexp(mp.mpf(1), bits - 1 - exponent)
    return Fraction(int(mp.ceil(value * scale))) / Fraction(2) ** (bits - 1 - exponent)


def log2_of_fraction(value: Fraction) -> str:
    """Return the base-2 logarithm of a positive Fraction as text."""
    return log2_text(mp.mpf(value.numerator) / value.denominator)


def print_fit(name: str, coefficients: Sequence[float], error: Any, first_index: int = 0) -> None:
    """Print the coefficients of a fit as C++ constants, with the measured error in a comment."""
    print(f"// {name}: weighted error 2^{log2_text(error)} after the round to double.")
    for index, coefficient in enumerate(coefficients):
        print(f"//   {name}{index + first_index} = {hex_double(coefficient)}")


def fit_and_print(
    name: str,
    target: Callable[[Any], Any],
    weight: Callable[[Any], Any],
    low: Any,
    high: Any,
    degree: int,
    first_index: int = 0,
) -> list[float]:
    """Fit, round each coefficient to a double, measure, print, and return the doubles."""
    coefficients = [round_double(value) for value in remez(target, weight, low, high, degree)]
    print_fit(name, coefficients, measured_error(target, weight, low, high, coefficients), first_index)
    return coefficients


# ── exp ────────────────────────────────────────────────────────────

EXP_TABLE_BITS = 7


def print_exp() -> None:
    """Print the table, the reduction constants and the polynomials of Exp.h."""
    entries = 1 << EXP_TABLE_BITS
    ln2 = mp.log(2)
    ln2_n = ln2 / entries
    ln2_hi = round_to_bits(ln2_n, 36)
    ln2_lo = round_double(ln2_n - ln2_hi)
    print(f"// inverse of ln2/N: {hex_double(round_double(entries / ln2))}")
    print(f"// -ln2/N, high part of 36 bits: {hex_double(-round_double(ln2_hi))}")
    print(f"// -ln2/N, low part: {hex_double(-ln2_lo)}")
    print(f"// -ln2/N as one double: {hex_double(-round_double(ln2_n))}")
    print("// Table: for each j, the bits of tail_j, then the bits of s_j less j << 45.")
    for index in range(entries):
        exact = mp.power(2, mp.mpf(index) / entries)
        scale = round_double(exact)
        tail = round_double((exact - mp.mpf(scale)) / mp.mpf(scale))
        print(f"    {hex_word(bits_of(tail))}, {hex_word(bits_of(scale) - (index << (52 - EXP_TABLE_BITS)))},")
    # The reduced argument r has |r| <= (1/2 + 2^-40) ln2/N: the round of k * N / ln2 to an integer
    # and the error of that product add a little to the half step.
    bound = (mp.mpf(1) / 2 + mp.mpf(2) ** -40) * ln2_n * (1 + mp.mpf(2) ** -20)

    def exp_tail(point: Any) -> Any:
        if point == 0:
            return mp.mpf(1) / 2
        return (mp.exp(point) - 1 - point) / point**2

    def exp_weight(point: Any) -> Any:
        return point**2 / mp.exp(point)

    coefficients = fit_and_print("C", exp_tail, exp_weight, -bound, bound, 3, 2)
    fit_and_print("float C", exp_tail, exp_weight, -bound, bound, 1, 2)
    # The rigorous bounds of the relative error of exp on |r| <= bound: the fast path takes 1 + r + r^2 (C2 + ...),
    # and the slow path the Taylor polynomial of degree 11 with each coefficient to degree 6 in two doubles.
    exact_bound = dyadic_above(bound)
    fast = rigorous_exp_error([1.0, 1.0, *coefficients], exact_bound)
    print(f"// rigorous bound of the fast polynomial: 2^{log2_of_fraction(fast)}")
    slow_terms: list[Fraction] = []
    for order in range(12):
        exact = Fraction(1, mp_factorial(order))
        high_part = float(exact)
        slow_terms.append(Fraction(high_part) + Fraction(float(exact - Fraction(high_part))) if order <= 6 else Fraction(high_part))
    slow = rigorous_exp_error(slow_terms, exact_bound, terms=28)
    print(f"// rigorous bound of the slow polynomial: 2^{log2_of_fraction(slow)}")
    # The edges of the float form: exp(x) rounds to +inf above the largest float at or below the first edge, to +0
    # below the smallest float at or above the second edge, and is below 2^-126 below the smallest float at or
    # above the third edge.
    float_overflow = mp.log(mp.ldexp(1, 128) * (1 - mp.ldexp(1, -25)))
    float_zero = mp.log(mp.ldexp(1, -150))
    float_subnormal = mp.log(mp.ldexp(1, -126))
    print(f"// expf overflow above x = {hex_double(float_toward(float_overflow, below=True))}f")
    print(f"// expf zero below x = {hex_double(float_toward(float_zero, below=False))}f")
    print(f"// expf subnormal result below x = {hex_double(float_toward(float_subnormal, below=False))}f")


def float_toward(edge: Any, below: bool) -> float:
    """Return the largest float at or below an edge, or the smallest float at or above it, as a double."""
    candidate = float_bits_of(round_double(edge))
    if below:
        while mp.mpf(float_of_bits(candidate)) > edge:
            candidate = candidate - 1 if candidate < 0x80000000 else candidate + 1
    else:
        while mp.mpf(float_of_bits(candidate)) < edge:
            candidate = candidate + 1 if candidate < 0x80000000 else candidate - 1
    return float_of_bits(candidate)


# ── log, double ────────────────────────────────────────────────────

LOG_TABLE_BITS = 7
LOG_OFFSET = 0x3FE6000000000000


def double_of_bits(bits: int) -> float:
    """Return the double of a bit pattern."""
    return float(struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0])


def print_log() -> None:
    """Print the table, the constants and the polynomials of the double log of Log.h."""
    entries = 1 << LOG_TABLE_BITS
    step = 1 << (52 - LOG_TABLE_BITS)
    ln2 = mp.log(2)
    ln2_hi = round_to_multiple(ln2, -42)
    ln2_lo = round_double(ln2 - ln2_hi)
    print(f"// ln2, a multiple of 2^-42: {hex_double(round_double(ln2_hi))}")
    print(f"// ln2, the rest: {hex_double(ln2_lo)}")
    print("// Table: invc, logc_hi (a multiple of 2^-43), logc_lo.")
    largest_reduced = mp.mpf(0)
    for index in range(entries):
        low = mp.mpf(double_of_bits(LOG_OFFSET + index * step))
        high = mp.mpf(double_of_bits(LOG_OFFSET + (index + 1) * step))
        center = (low + high) / 2
        inverse = round_double(1 / center)
        logc = -mp.log(mp.mpf(inverse))
        logc_hi = round_to_multiple(logc, -43)
        logc_lo = round_double(logc - logc_hi)
        largest_reduced = max(largest_reduced, abs(low * inverse - 1), abs(high * inverse - 1))
        print(f"    {{{hex_double(inverse)}, {hex_double(round_double(logc_hi))}, {hex_double(logc_lo)}}},")
    print(f"// largest |r| of the table: 2^{log2_text(largest_reduced)}")
    bound = largest_reduced * (1 + mp.mpf(2) ** -20)

    def log_tail(point: Any) -> Any:
        if point == 0:
            return mp.mpf(-1) / 2
        return (mp.log1p(point) - point) / point**2

    def log_weight(point: Any) -> Any:
        return point**2

    fit_and_print("A", log_tail, log_weight, -bound, bound, 4)
    near_low = mp.mpf(-1) / 16
    near_high = mp.mpf(double_of_bits(0x3FF1090000000000)) - 1

    def near_tail(point: Any) -> Any:
        if point == 0:
            return mp.mpf(1) / 3
        return (mp.log1p(point) - point + point**2 / 2) / point**3

    def near_weight(point: Any) -> Any:
        if point == 0:
            return mp.mpf(0)
        return abs(point**3 / mp.log1p(point))

    fit_and_print("B", near_tail, near_weight, near_low, near_high, 9, 1)


# ── log, float ─────────────────────────────────────────────────────

LOGF_TABLE_BITS = 7
# The bits of the float 0x1.67p-1: the start of the range of z.  1 is at the middle of an interval in the bits.
LOGF_OFFSET = 0x3F338000
# The degree of the tail polynomial of log1p(r) - r = r^2 (A0 + ... + A3 r^3).
LOGF_DEGREE = 3


def float_of_bits(bits: int) -> float:
    """Return the float of a bit pattern, as a double."""
    return float(struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0])


def print_doubles(values: Sequence[float], per_line: int) -> None:
    """Print doubles as rows of C++ hexadecimal float literals, `per_line` on each row.  O(len(values))."""
    for start in range(0, len(values), per_line):
        print("    " + " ".join(f"{hex_double(value)}," for value in values[start : start + per_line]))


def print_logf() -> None:
    """Print the two tables and the polynomial of the float log of Log.h."""
    entries = 1 << LOGF_TABLE_BITS
    step = 1 << (23 - LOGF_TABLE_BITS)
    print(f"// ln2: {hex_double(round_double(mp.log(2)))}")
    inverses: list[float] = []
    logcs: list[float] = []
    largest_reduced = mp.mpf(0)
    for index in range(entries):
        low = mp.mpf(float_of_bits(LOGF_OFFSET + index * step))
        high = mp.mpf(float_of_bits(LOGF_OFFSET + (index + 1) * step))
        inverse = mp.mpf(1) if low <= 1 < high else round_to_bits(2 / (low + high), 29)
        largest_reduced = max(largest_reduced, abs(low * inverse - 1), abs(high * inverse - 1))
        inverses.append(round_double(inverse))
        logcs.append(round_double(-mp.log(inverse)))
    print("// Table: invc (29 significant bits, 1 in the interval of 1).")
    print_doubles(inverses, 4)
    print("// Table: logc.")
    print_doubles(logcs, 4)
    print(f"// largest |r| of the table: 2^{log2_text(largest_reduced)}")
    bound = largest_reduced * (1 + mp.mpf(2) ** -20)

    def log_tail(point: Any) -> Any:
        if point == 0:
            return mp.mpf(-1) / 2
        return (mp.log1p(point) - point) / point**2

    def log_weight(point: Any) -> Any:
        return abs(point)

    fit_and_print("float A", log_tail, log_weight, -bound, bound, LOGF_DEGREE)


# ── erf ────────────────────────────────────────────────────────────

ERF_INTERVALS = 44
ERF_DEGREE = 11


def print_erf() -> None:
    """Print the constants, the polynomial near zero and the table of the intervals of Erf.h."""
    two_over_root_pi = 2 / mp.sqrt(mp.pi)
    high = round_double(two_over_root_pi)
    print(f"// 2/sqrt(pi): {hex_double(high)} + {hex_double(round_double(two_over_root_pi - high))}")

    def ratio(point: Any) -> Any:
        if point == 0:
            return two_over_root_pi
        root = mp.sqrt(point)
        return mp.erf(root) / root

    def small_target(point: Any) -> Any:
        if point == 0:
            return -two_over_root_pi / 3
        return (ratio(point) - two_over_root_pi) / point

    def small_weight(point: Any) -> Any:
        return point / ratio(point)

    # erf(x) = x (2/sqrt(pi) + z P(z)) with z = x^2: the fit of P for the relative error of erf.
    fit_and_print("P", small_target, small_weight, mp.mpf(0), mp.mpf(1) / 4, 9)
    start = mp.mpf(1) / 2
    width = mp.mpf(1) / 8
    print(f"// Table: {ERF_INTERVALS} intervals of width 1/8 from 1/2.  For each: erf(center) as hi and lo,")
    print(f"// then the Taylor coefficients 1 to {ERF_DEGREE} at the center.")
    largest_truncation = mp.mpf(0)
    for index in range(ERF_INTERVALS):
        center = start + width * index + width / 2
        value = mp.erf(center)
        value_hi = round_double(value)
        value_lo = round_double(value - mp.mpf(value_hi))
        derivative_scale = two_over_root_pi * mp.exp(-(center**2))
        coefficients = [
            derivative_scale * (-1) ** (order - 1) * mp.hermite(order - 1, center) / mp.factorial(order)
            for order in range(1, ERF_DEGREE + 1)
        ]
        doubles = [round_double(coefficient) for coefficient in coefficients]
        for step in range(33):
            offset = width / 2 * (step - 16) / 16
            approximate = (
                mp.mpf(value_hi)
                + mp.mpf(value_lo)
                + offset * polynomial_value([mp.mpf(coefficient) for coefficient in doubles], offset)
            )
            largest_truncation = max(largest_truncation, abs(approximate - mp.erf(center + offset)))
        text = ", ".join(hex_double(item) for item in (value_hi, value_lo, *doubles))
        print(f"    {{{text}}},")
    print(f"// largest absolute error of the table polynomials: 2^{log2_text(largest_truncation)}")
    double_one = mp.findroot(lambda point: mp.erfc(point) - mp.ldexp(1, -54), mp.mpf("5.92"))
    float_one = mp.findroot(lambda point: mp.erfc(point) - mp.ldexp(1, -25), mp.mpf("3.92"))
    print(
        f"// erf rounds to 1 from x = {hex_double(first_at_or_above(double_one, 52))} (double), "
        f"{hex_double(first_at_or_above(float_one, 23))}f (float)"
    )


ERF_FLOAT_FIRST = 8
ERF_FLOAT_INTERVALS = 55
ERF_FLOAT_DEGREE = 7
# The degree of Q in z = x^2 of erf(x) = x Q(z) below 1/2, for the float form.
ERF_FLOAT_SMALL_DEGREE = 7


def print_erff() -> None:
    """Print the polynomial below 1/2 and the table of the intervals of the float form of Erf.h.

    Below 1/2, erf(x) = x Q(z) with z = x^2 and Q of degree
    ERF_FLOAT_SMALL_DEGREE.  Q0 is the double nearest 2/sqrt(pi), so a small x
    has the error of that one round, and Q1 to Q7 are the minimax polynomial
    of (erf(x)/x - 2/sqrt(pi)) / z for the relative error of erf.  Interval i
    covers [i/16, (i + 1)/16) for i from ERF_FLOAT_FIRST, and the last one
    holds the first x whose erf rounds to 1 in float.  For each, the minimax
    polynomial of degree ERF_FLOAT_DEGREE in the offset from the center, for
    the relative error of erf.  O(intervals * remez).
    """
    two_over_root_pi = 2 / mp.sqrt(mp.pi)

    def ratio(point: Any) -> Any:
        if point == 0:
            return two_over_root_pi
        root = mp.sqrt(point)
        return mp.erf(root) / root

    def small_target(point: Any) -> Any:
        if point == 0:
            return -two_over_root_pi / 3
        return (ratio(point) - two_over_root_pi) / point

    def small_weight(point: Any) -> Any:
        return point / ratio(point)

    print(f"// float Q0 = {hex_double(round_double(two_over_root_pi))}")
    fit_and_print("float Q", small_target, small_weight, mp.mpf(0), mp.mpf(1) / 4, ERF_FLOAT_SMALL_DEGREE - 1, 1)
    width = mp.mpf(1) / 16
    half = width / 2
    largest = mp.mpf(0)
    print(f"// Table: {ERF_FLOAT_INTERVALS} intervals of width 1/16 from {ERF_FLOAT_FIRST}/16.  For each: the")
    print(f"// coefficients 0 to {ERF_FLOAT_DEGREE} of the minimax polynomial in the offset from the center.")
    for index in range(ERF_FLOAT_FIRST, ERF_FLOAT_FIRST + ERF_FLOAT_INTERVALS):
        center = width * index + half

        def target(offset: Any, middle: Any = center) -> Any:
            return mp.erf(middle + offset)

        def weight(offset: Any, middle: Any = center) -> Any:
            return 1 / mp.erf(middle + offset)

        coefficients = [round_double(value) for value in remez(target, weight, -half, half, ERF_FLOAT_DEGREE)]
        largest = max(largest, measured_error(target, weight, -half, half, coefficients))
        print(f"    {{{{{', '.join(hex_double(item) for item in coefficients)}}}}},")
    print(f"// largest relative error of the table polynomials: 2^{log2_text(largest)}")


def first_at_or_above(edge: Any, fraction_bits: int) -> float:
    """Return the smallest positive number with fraction_bits bits after its point at or above an edge."""
    exponent = int(mp.floor(mp.log(edge, 2)))
    step = mp.ldexp(1, exponent - fraction_bits)
    return round_double(mp.ceil(edge / step) * step)


SUBCOMMANDS: dict[str, Callable[[], None]] = {
    "exp": print_exp,
    "log": print_log,
    "logf": print_logf,
    "erf": print_erf,
    "erff": print_erff,
}


def main(arguments: list[str]) -> int:
    """Parse the subcommand, load the pinned mpmath and print its tables."""
    global mp
    parser = argparse.ArgumentParser(description="Print the tables of include/fixy/fp/.")
    parser.add_argument("table", choices=sorted(SUBCOMMANDS))
    options = parser.parse_args(arguments)
    module = load_mpmath()
    mp = module.mp
    mp.prec = PRECISION
    SUBCOMMANDS[options.table]()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
