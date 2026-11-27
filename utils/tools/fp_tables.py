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

The subcommand exp also prints the rigorous error budgets of the two paths of
exp(double): each term of the budget of the fast path and of the slow path, in
the order of the operations of Exp.h, as an exact fraction from the bound of
each round, and their sums eps_fast and eps_acc.  The comment of Exp.h copies
these terms.

The script runs with the pinned mpmath, which `python3 utils/bin/setup install
mpmath` installs.  It finds the package through `setup install --print-bin
mpmath`, so it reads no mpmath of the Python of the user.

    python3 utils/tools/fp_tables.py exp     the tables of Exp.h
    python3 utils/tools/fp_tables.py exp-coverage
                                             the coverage table of the correct round of exp(double) in Exp.h,
                                             from the list exp.wc of the pinned CORE-MATH and an own check
    python3 utils/tools/fp_tables.py log     the tables of Log.h, double
    python3 utils/tools/fp_tables.py logf    the tables of Log.h, float
    python3 utils/tools/fp_tables.py erf     the tables of Erf.h, double
    python3 utils/tools/fp_tables.py erff    the polynomial and the table of Erf.h, float

The output of one subcommand is the same on each run and on each host,
because mpmath computes with integers only.  A run takes some minutes.
"""

from __future__ import annotations

import argparse
import bisect
import math
import multiprocessing
import os
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
    # The slow path: ln2/N in four parts.  The first two parts have 35 bits, so their product with an integer k
    # of 18 bits is exact.
    part0 = round_to_bits(ln2_n, 35)
    part1 = round_to_bits(ln2_n - part0, 35)
    part2 = round_double(ln2_n - part0 - part1)
    part3 = round_double(ln2_n - part0 - part1 - mp.mpf(part2))
    ln2_parts = [round_double(part0), round_double(part1), part2, part3]
    parts_gap = dyadic_above(abs(ln2_n - sum(mp.mpf(part) for part in ln2_parts)))
    for order, part in enumerate(ln2_parts):
        print(f"// ln2/N, slow part {order}: {hex_double(part)}")
    print(f"// |ln2/N - the four parts| <= 2^{log2_of_fraction(parts_gap)}")
    print("// Table: for each j, the bits of tail_j, then the bits of s_j less j << 45.")
    tails: list[float] = []
    tails_low: list[float] = []
    tail_error = Fraction(0)
    tail_low_error = Fraction(0)
    for index in range(entries):
        exact = mp.power(2, mp.mpf(index) / entries)
        scale = round_double(exact)
        relative = (exact - mp.mpf(scale)) / mp.mpf(scale)
        tail = round_double(relative)
        tail_low = round_double(relative - mp.mpf(tail))
        tails.append(tail)
        tails_low.append(tail_low)
        tail_error = max(tail_error, dyadic_above(abs(relative - mp.mpf(tail))) if relative != tail else Fraction(0))
        rest = abs(relative - mp.mpf(tail) - mp.mpf(tail_low))
        tail_low_error = max(tail_low_error, dyadic_above(rest) if rest != 0 else Fraction(0))
        print(f"    {hex_word(bits_of(tail))}, {hex_word(bits_of(scale) - (index << (52 - EXP_TABLE_BITS)))},")
    print("// Table of the slow path: for each j, the bits of the double nearest tau_j - tail_j.")
    for start in range(0, entries, 4):
        print("    " + " ".join(f"{hex_word(bits_of(value))}," for value in tails_low[start : start + 4]))
    tail_most = max(abs(Fraction(value)) for value in tails)
    tail_low_most = max(abs(Fraction(value)) for value in tails_low)
    print(
        f"// |tail_j| <= 2^{log2_of_fraction(tail_most)}, |tau_j - tail_j| <= 2^{log2_of_fraction(tail_error)},"
        f" |tau_j - tail_j - low_j| <= 2^{log2_of_fraction(tail_low_error)}"
    )
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
    print("// The Taylor coefficients of the slow path: 1/n! to degree 6 in two doubles, then one double.")
    for order in range(12):
        exact = Fraction(1, mp_factorial(order))
        high_part = float(exact)
        low_part = float(exact - Fraction(high_part)) if order <= 6 else 0.0
        slow_terms.append(Fraction(high_part) + Fraction(low_part))
        if order >= 3:
            print(
                f"//   slow C{order} = {hex_double(high_part)}" + (f" + {hex_double(low_part)}" if order <= 6 else "")
            )
    slow = rigorous_exp_error(slow_terms, exact_bound, terms=28)
    print(f"// rigorous bound of the slow polynomial: 2^{log2_of_fraction(slow)}")
    # The edges of the double form.
    double_overflow = mp.log(mp.ldexp(1, 1024) - mp.ldexp(1, 970))
    print(f"// exp largest input with a finite result: {hex_double(double_toward(double_overflow, below=True))}")
    double_zero = mp.log(mp.ldexp(1, -1075))
    double_normal = mp.log(mp.ldexp(1, -1022))
    print(f"// exp largest input with the result +0: {hex_double(double_toward(double_zero, below=True))}")
    print(f"// exp smallest input with a normal result: {hex_double(double_toward(double_normal, below=False))}")
    ln2_high = round_double(ln2_hi)
    ln2_gap = dyadic_above(abs(ln2_n - mp.mpf(ln2_high) - mp.mpf(ln2_lo)))
    fast_total = exp_fast_budget(coefficients, exact_bound, fast, ln2_lo, ln2_gap, tail_most, tail_error)
    exp_slow_budget(slow_terms, exact_bound, slow, ln2_parts, parts_gap, tail_most, tail_low_most, tail_low_error)
    print(f"// fast path, the test at 2^-65 holds: {fast_total <= Fraction(1, 2**65)}")
    # The edges of the float form: exp(x) rounds to +inf above the largest float at or below the first edge, to +0
    # below the smallest float at or above the second edge, and is below 2^-126 below the smallest float at or
    # above the third edge.
    float_overflow = mp.log(mp.ldexp(1, 128) * (1 - mp.ldexp(1, -25)))
    float_zero = mp.log(mp.ldexp(1, -150))
    float_subnormal = mp.log(mp.ldexp(1, -126))
    print(f"// expf overflow above x = {hex_double(float_toward(float_overflow, below=True))}f")
    print(f"// expf zero below x = {hex_double(float_toward(float_zero, below=False))}f")
    print(f"// expf subnormal result below x = {hex_double(float_toward(float_subnormal, below=False))}f")


def double_toward(edge: Any, below: bool) -> float:
    """Return the largest double below an edge, or the smallest double above it.  The edge is not a double."""
    candidate = round_double(edge)
    if below:
        while mp.mpf(candidate) >= edge:
            candidate = math.nextafter(candidate, -math.inf)
    else:
        while mp.mpf(candidate) <= edge:
            candidate = math.nextafter(candidate, math.inf)
    return candidate


def print_terms(title: str, terms: Sequence[tuple[str, Fraction]]) -> Fraction:
    """Print each term of an error budget and return their sum."""
    print(f"// {title}")
    for name, value in terms:
        print(f"//   {name}: 2^{log2_of_fraction(value) if value else '-inf'}")
    return sum((value for _, value in terms), Fraction(0))


def sum_rounding(magnitudes: Sequence[Fraction], unit: Fraction) -> Fraction:
    """Return a bound of the error of the sum of terms in sequence, each partial sum rounded to nearest.

    Each partial sum after the first add rounds once, with an error of at most unit times its magnitude, and the
    bound of each magnitude takes the earlier errors.  O(len(magnitudes)).
    """
    total = Fraction(0)
    partial = magnitudes[0]
    for magnitude in magnitudes[1:]:
        partial = (partial + magnitude) * (1 + unit)
        total += unit * partial
    return total


def exp_fast_budget(
    coefficients: Sequence[float],
    r_bound: Fraction,
    poly_error: Fraction,
    ln2_low: float,
    ln2_gap: Fraction,
    tail_most: Fraction,
    tail_error: Fraction,
) -> Fraction:
    """Print the error budget of the fast path of exp(double) and return the bound that the margin 2^-65 must hold.

    The terms follow the operations of exp_pair_ of Exp.h, for |k| <= 2^17 and |r_hi| <= R.  U is the quantity
    with exp(x) = s (1 + U): the path computes r_hi + q, and the dropped terms and the rounds of q are errors of
    U.  The path then rounds s + s r_hi to hi and s q + (the error of hi) to lo.  Exact.
    """
    u = Fraction(1, 2**53)
    reach = r_bound
    k_most = Fraction(2**17)
    c2, c3, c4, c5 = (abs(Fraction(value)) for value in coefficients)
    c2_gap = abs(2 * Fraction(coefficients[0]) - 1)
    # The reduction: r1 = x - k L1 is exact, r_hi = RN(r1 - k L2), d = RN(r1 - r_hi), r_lo = RN(d - k L2).
    round_high = u * reach
    difference = k_most * abs(Fraction(ln2_low)) + round_high
    low_most = (1 + u) * (round_high + u * difference)
    reduction = u * (round_high + u * difference) + u * difference + k_most * ln2_gap
    widened = reach + low_most
    g_most = c2 * widened**2 + c3 * widened**3 + c4 * widened**4 + c5 * widened**5
    g_second = 2 * c2 + 6 * c3 * widened + 12 * c4 * widened**2 + 20 * c5 * widened**3
    # The rounds of q: t = RN(tail + r_lo), a = RN(t + t r_hi), the pairs of P', P' and q.
    t_most = (tail_most + low_most) * (1 + u)
    a_most = t_most * (1 + reach) * (1 + u)
    low_pair = c2 + c3 * reach
    high_pair = c4 + c5 * reach
    square_most = reach**2 * (1 + u)
    poly_most = (low_pair + high_pair * square_most) * (1 + u) ** 2
    poly_round = u * low_pair + u * reach**2 * high_pair * (1 + u) + reach**2 * u * high_pair + u * poly_most
    q_most = (a_most + square_most * poly_most) * (1 + u)
    terms = [
        ("tau r_lo (dropped)", tail_most * low_most),
        ("tau r^2 P' (dropped)", tail_most * g_most),
        ("(tau - tail) (1 + r_hi) (table)", tail_error * (1 + reach)),
        (
            "g(r_hi + r_lo) - g(r_hi) - r_hi r_lo (dropped)",
            low_most * (c2_gap * reach + 3 * c3 * reach**2 + 4 * c4 * reach**3 + 5 * c5 * reach**4)
            + low_most**2 * g_second / 2,
        ),
        ("round of t, times 1 + r_hi", u * (tail_most + low_most) * (1 + reach)),
        ("round of a", u * a_most),
        ("round of r_hi^2, times P'", u * reach**2 * poly_most),
        ("rounds of P', times r_hi^2", reach**2 * poly_round),
        ("round of q", u * q_most),
    ]
    in_u = print_terms("fast path: the errors of U, absolute", terms)
    ratio = (1 + reach + reduction + (reach + reduction) ** 2) / (1 - tail_most)
    lo_over_y = (q_most * (1 + u) * ratio + u * (1 + u) * (1 + Fraction(1, 2**50))) * (1 + u)
    relative = [
        ("the errors of U, over (1 + tau) exp(r)", in_u * ratio),
        ("the polynomial (rigorous)", poly_error),
        ("the reduction r - r_hi - r_lo, through exp", (1 + poly_error) * (reduction + reduction**2)),
        ("round of the error of hi", u * u * (1 + Fraction(1, 2**50))),
        ("round of lo", u * lo_over_y),
    ]
    eps_fast = print_terms("fast path: the relative errors of hi + lo", relative)
    print(
        f"// eps_fast = 2^{log2_of_fraction(eps_fast)}, |r_lo| <= 2^{log2_of_fraction(low_most)},"
        f" |r - r_hi - r_lo| <= 2^{log2_of_fraction(reduction)}, |lo| <= 2^{log2_of_fraction(lo_over_y)} y"
    )
    y_over_hi = (1 + eps_fast) / (1 - lo_over_y * (1 + eps_fast))
    needed = eps_fast * y_over_hi + u * (lo_over_y * y_over_hi + Fraction(1, 2**65))
    print(f"// the rounding test needs a margin of 2^{log2_of_fraction(needed)} |hi|")
    return needed


def exp_slow_budget(
    slow_terms: Sequence[Fraction],
    r_bound: Fraction,
    poly_error: Fraction,
    ln2_parts: Sequence[float],
    parts_gap: Fraction,
    tail_most: Fraction,
    tail_low_most: Fraction,
    tail_low_error: Fraction,
) -> Fraction:
    """Print the error budget of the slow path of exp(double) and return eps_acc.

    The terms follow the operations of exp_accurate_ of Exp.h, for |k| <= 137602 (|x| below 745.2) and
    |r| <= R.  The double-word operations take the bounds of Joldes, Muller and Popescu (ACM TOMS 44(2), 2017):
    DWPlusFP 2u^2, AccurateDWPlusDW 3u^2 / (1 - 4u), DWTimesFP3 2u^2 and DWTimesDW3 5u^2, relative.  Exact.
    """
    u = Fraction(1, 2**53)
    reach = r_bound
    k_most = Fraction(137602)
    part2, part3 = (abs(Fraction(part)) for part in ln2_parts[2:])
    coefficient = [abs(value) for value in slow_terms]
    add_dd = 3 * u * u / (1 - 4 * u)
    times_dd = 5 * u * u
    times_fp = 2 * u * u
    add_fp = 2 * u * u
    # The reduction.  b0 + b1 = x - k P0 - k P1 exactly, and the parts after it are small.
    product2_most = k_most * part2 * (1 + u)
    product3_most = k_most * part3 * (1 + u)
    head_most = reach + k_most * (part2 + part3) * 2
    b1_most = u * head_most
    middle_most = (b1_most + product2_most) * (1 + u)
    middle_low = u * middle_most
    product2_low = u * product2_most
    last_most = (middle_low + product2_low + product3_most) * (1 + u) ** 2
    reduction = (
        k_most * parts_gap
        + u * k_most * part3
        + u * (middle_low + product2_low)
        + u * (middle_low + product2_low + product3_most) * (1 + u)
    )
    reduced_low = u * reach * (1 + u)
    reduced_last = u * (u * reach + last_most) * (1 + u)
    # H = c7 + R_h (c8 + R_h (c9 + R_h (c10 + R_h c11))) with four fma.
    c3, c4, c5, c6, c7, c8, c9, c10, c11 = coefficient[3:12]
    h_bound = [c10 + c11 * reach]
    for value in (c9, c8, c7):
        h_bound.append(value + reach * h_bound[-1] * (1 + u))
    h_round = Fraction(0)
    for bound_value in h_bound:
        h_round = reach * h_round + u * bound_value * (1 + u)
    h_most = h_bound[-1] * (1 + u)
    h_slope = c8 + 2 * c9 * reach + 3 * c10 * reach**2 + 4 * c11 * reach**3
    h_error = h_round + h_slope * (reduced_low + reduced_last)
    off = reduced_last
    # The Horner steps in double-word arithmetic: A6 = c6 + R H, A5, A4, A3, then Q = 1/2 + R A3.
    a_most = c6 + reach * h_most
    a_error = add_dd * a_most * (1 + u) + times_fp * reach * h_most + reach * h_error + off * h_most
    for value in (c5, c4, c3):
        product = reach * a_most * (1 + u)
        next_most = value + product
        a_error = add_dd * next_most * (1 + u) + times_dd * product + reach * a_error + off * a_most
        a_most = next_most * (1 + u)
    q_product = reach * a_most * (1 + u)
    q_most = (Fraction(1, 2) + q_product) * (1 + u)
    q_error = add_fp * q_most + times_dd * q_product + reach * a_error + off * a_most
    square_most = reach**2 * (1 + 4 * u)
    square_error = times_dd * square_most + off * (2 * reach + off)
    w_most = square_most * q_most * (1 + 6 * u)
    w_error = times_dd * w_most + q_most * square_error + square_most * q_error
    # V = r + w in three parts.
    v1_low = u * (reach + w_most) * (1 + u)
    v2_high = (reduced_low + u * w_most) * (1 + u)
    v3_high = (v1_low + v2_high) * (1 + u)
    v4_terms = [u * v2_high, u * v3_high, reduced_last]
    v4_most = sum(v4_terms, Fraction(0)) * (1 + u) ** 2
    v_error = w_error + sum_rounding(v4_terms, u)
    v1_high = (reach + w_most) * (1 + u)
    # W = tau + V + tau V, with tau = tail + low.
    w1_high = (v1_high + tail_most) * (1 + u)
    w1_low = u * w1_high
    cross_high = tail_most * v1_high * (1 + u)
    cross_low = u * cross_high
    cross_rest = (tail_most * v3_high + tail_low_most * v1_high) * (1 + u) ** 2
    cross_round = u * tail_low_most * v1_high + u * cross_rest
    w2_high = (w1_low + cross_high) * (1 + u)
    w3_high = (w2_high + v3_high) * (1 + u)
    rest_terms = [u * w2_high, u * w3_high, cross_low, tail_low_most, v4_most, cross_rest]
    w_rest_most = sum(rest_terms, Fraction(0)) * (1 + u) ** 5
    w_terms = [
        ("V, through 1 + tau", v_error * (1 + tail_most + tail_low_most)),
        ("tau - tail - low (table)", tail_low_error * (1 + v1_high)),
        ("tail v4 and low (v3 + v4) (dropped)", tail_most * v4_most + tail_low_most * (v3_high + v4_most)),
        ("rounds of the cross terms", cross_round),
        ("rounds of the sum of the rest of W", sum_rounding(rest_terms, u)),
    ]
    in_w = print_terms("slow path: the errors of W, absolute", w_terms)
    # Y' = s (1 + W), with s in [1, 2): p1 and p3 are exact products, p_rest = RN(s w_rest), and z_rest is the
    # sum of the low parts in sequence.
    p1_high = 2 * w1_high * (1 + u)
    z1_low = u * (2 + p1_high) * (1 + u)
    z2_high = (z1_low + u * p1_high) * (1 + u)
    p3_high = 2 * w3_high * (1 + u)
    z_terms = [u * z2_high, u * (z2_high + p3_high) * (1 + u), u * p3_high, 2 * w_rest_most * (1 + u)]
    assembly = 2 * u * w_rest_most + sum_rounding(z_terms, u)
    print(f"// slow path: the rounds of the assembly of Y', absolute: 2^{log2_of_fraction(assembly)}")
    ratio = (1 + reach + reduction + (reach + reduction) ** 2) / (1 - tail_most)
    relative = [
        ("the errors of W and Y', over Y'", (in_w + assembly) * ratio),
        ("the polynomial (rigorous)", poly_error),
        ("the reduction, through exp", (1 + poly_error) * (reduction + reduction**2)),
    ]
    eps_acc = print_terms("slow path: the relative errors of Y'", relative)
    print(
        f"// slow path: |Q error| <= 2^{log2_of_fraction(q_error)}, |r^2 error| <= 2^{log2_of_fraction(square_error)},"
        f" |w error| <= 2^{log2_of_fraction(w_error)}, |r - r~| <= 2^{log2_of_fraction(reduction)}"
    )
    print(f"// eps_acc = 2^{log2_of_fraction(eps_acc)}")
    return eps_acc


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


# ── The coverage of the correct round of exp(double) ───────────────
#
# exp increases with x, so between two midpoints the doubles next to ln(midpoint) are the doubles nearest that
# midpoint.  The own check walks each midpoint of a range with that fact.  The list check reads the list exp.wc of
# the pinned CORE-MATH, and measures the distance of exp(x) to the nearest boundary for each input of it.

# The range of the own check: 2^-54 <= |x| < 2^-30.
COVERAGE_LOW = 2.0**-54
COVERAGE_HIGH = 2.0**-30
# The window of the own check, in units in the last place: the doubles within it of a midpoint.
COVERAGE_WINDOW = -42
# The count of the midpoints of one task of the pool.
COVERAGE_CHUNK = 1 << 14


def neighbors_of(edge: Any) -> tuple[float, float]:
    """Return the two doubles around a real number that is not a double."""
    near = round_double(edge)
    if mp.mpf(near) > edge:
        return math.nextafter(near, -math.inf), near
    return near, math.nextafter(near, math.inf)


def measure_exp(x: float) -> tuple[str, Any, Any, Any]:
    """Return the class of exp(x) and its distances: to a midpoint in ulp, to any boundary in ulp, and relative.

    The class is normal, subnormal (the last place 2^-1074), past the overflow (the boundary 2^1024 - 2^970 in units
    of 2^971), or below 2^-1074 (the boundary 2^-1075 in units of 2^-1074).
    """
    y = mp.exp(mp.mpf(x))
    smallest = mp.ldexp(1, -1074)
    if y >= mp.ldexp(1, 1024):
        distance = abs(y - mp.ldexp(1, 1024) + mp.ldexp(1, 970)) / mp.ldexp(1, 971)
        return "past-overflow", distance, distance, distance * mp.ldexp(1, 971) / y
    if y < smallest:
        distance = abs(y - mp.ldexp(1, -1075)) / smallest
        return "below-2^-1074", distance, distance, distance * smallest / y
    _, exponent = mp.frexp(y)
    is_normal = y >= mp.ldexp(1, -1022)
    unit = mp.ldexp(1, int(exponent) - 53) if is_normal else smallest
    scaled = y / unit
    fraction = scaled - mp.floor(scaled)
    to_mid = abs(fraction - mp.mpf(1) / 2)
    return ("normal" if is_normal else "subnormal"), to_mid, min(to_mid, fraction, 1 - fraction), to_mid * unit / y


def check_midpoints(
    task: tuple[int, int, int],
) -> tuple[dict[tuple[int, int], tuple[Any, float]], list[tuple[int, int]]]:
    """Check the midpoints of index first to last - 1 of one sign, in a worker of the pool.

    A positive sign takes the midpoints 1 + (k + 1/2) 2^-52, a negative sign 1 - (k + 1/2) 2^-53.  Returns the
    smallest distance in ulp of each (sign, binade of x), and the window of the doubles within 2^COVERAGE_WINDOW ulp
    of each midpoint, as a pair of the bits of |x|.  O(last - first).
    """
    sign, first, last = task
    unit = mp.ldexp(1, -52 if sign > 0 else -53)
    window = mp.ldexp(1, COVERAGE_WINDOW) * unit
    smallest: dict[tuple[int, int], tuple[Any, float]] = {}
    windows: list[tuple[int, int]] = []
    for index in range(first, last):
        midpoint = 1 + sign * (index + mp.mpf(1) / 2) * unit
        for x in neighbors_of(mp.log(midpoint)):
            if not COVERAGE_LOW <= abs(x) < COVERAGE_HIGH:
                continue
            distance = abs(mp.exp(mp.mpf(x)) - midpoint) / unit
            key = (sign, math.frexp(abs(x))[1])
            if key not in smallest or distance < smallest[key][0]:
                smallest[key] = (distance, x)
        low_edge, high_edge = mp.log(midpoint - window), mp.log(midpoint + window)
        low = neighbors_of(low_edge)[1]
        high = neighbors_of(high_edge)[0]
        if low <= high:
            low_bits, high_bits = sorted((bits_of(abs(low)), bits_of(abs(high))))
            low_bits = max(low_bits, bits_of(COVERAGE_LOW))
            high_bits = min(high_bits, bits_of(COVERAGE_HIGH) - 1)
            if low_bits <= high_bits:
                windows.append((sign * low_bits, high_bits))
    return smallest, windows


def read_exp_list(path: Path) -> list[tuple[int, str, list[tuple[float, str, Any, Any, Any]]]]:
    """Read a list .wc of CORE-MATH into blocks, each with the line of its title, the title and its measured inputs.

    A block starts at a comment line.  A line with no hexadecimal double, as a NaN or an infinity, and a zero, holds
    no input.  O(lines).
    """
    blocks: list[tuple[int, str, list[tuple[float, str, Any, Any, Any]]]] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        text = line.strip()
        if not text:
            continue
        if text.startswith("#"):
            if not blocks or blocks[-1][2]:
                blocks.append((number, text, []))
            else:
                blocks[-1] = (blocks[-1][0], blocks[-1][1] + " / " + text, blocks[-1][2])
            continue
        try:
            x = float.fromhex(text.split()[0])
        except ValueError:
            continue
        if math.isfinite(x) and x != 0.0:
            blocks[-1][2].append((x, *measure_exp(x)))
    return blocks


def print_exp_list(blocks: list[tuple[int, str, list[tuple[float, str, Any, Any, Any]]]]) -> None:
    """Print the thresholds and the hardest inputs of each block, and the controls of the searches."""
    for number, title, entries in blocks:
        print(f"// line {number}: {title[:150]}")
        for kind in sorted({entry[1] for entry in entries}):
            part = [entry for entry in entries if entry[1] == kind]
            widest = max(part, key=lambda entry: entry[3])
            text = f"//   {kind}: {len(part)} inputs, the threshold 2^{log2_text(widest[3])} ulp"
            if kind in ("normal", "subnormal"):
                hardest = min(part, key=lambda entry: entry[2])
                relative = min(part, key=lambda entry: entry[4])
                text += (
                    f", the hardest 2^{log2_text(hardest[2])} ulp at {hardest[0].hex()},"
                    f" relative 2^{log2_text(relative[4])} at {relative[0].hex()}"
                )
            print(text)
    m42 = [entry for _, title, entries in blocks if "-m 42" in title for entry in entries if entry[1] == "normal"]
    for sign in (1, -1):
        counts: dict[int, int] = {}
        for entry in m42:
            if (entry[0] > 0) == (sign > 0):
                binade = math.frexp(abs(entry[0]))[1]
                counts[binade] = counts.get(binade, 0) + 1
        print(
            f"// -m 42, sign {sign:+d}, inputs of each binade: " + " ".join(f"{e}:{counts[e]}" for e in sorted(counts))
        )
    band = [entry for _, _, entries in blocks for entry in entries if -(2.0**-11) <= entry[0] < -(2.0**-12)]
    limits = " ".join(
        f"below 2^{limit}: {sum(1 for entry in band if entry[3] < mp.ldexp(1, limit))}"
        for limit in (-45, -44, -42, -35)
    )
    print(f"// [-2^-11, -2^-12), the union of the blocks: {len(band)} inputs, {limits}")
    subnormal = [entry for _, title, entries in blocks if "subnormal range with at least" in title for entry in entries]
    counts = {}
    for entry in subnormal:
        binade = int(mp.frexp(mp.exp(mp.mpf(entry[0])))[1])
        counts[binade] = counts.get(binade, 0) + 1
    print(
        "// subnormal block, inputs of each binade of the result: "
        + " ".join(f"{e}:{counts[e]}" for e in sorted(counts))
    )


def print_exp_own_check(blocks: list[tuple[int, str, list[tuple[float, str, Any, Any, Any]]]]) -> None:
    """Run the own check of 2^-54 <= |x| < 2^-30 on a pool, and compare its windows with the list."""
    positive = int(mp.floor((mp.exp(mp.mpf(COVERAGE_HIGH)) - 1) / mp.ldexp(1, -52) + 1))
    negative = int(mp.floor((1 - mp.exp(-mp.mpf(COVERAGE_HIGH))) / mp.ldexp(1, -53) + 1))
    tasks = [(1, start, min(start + COVERAGE_CHUNK, positive)) for start in range(0, positive, COVERAGE_CHUNK)]
    tasks += [(-1, start, min(start + COVERAGE_CHUNK, negative)) for start in range(0, negative, COVERAGE_CHUNK)]
    smallest: dict[tuple[int, int], tuple[Any, float]] = {}
    windows: list[tuple[int, int]] = []
    with multiprocessing.get_context("fork").Pool(min(32, os.cpu_count() or 1)) as pool:
        for part_smallest, part_windows in pool.imap(check_midpoints, tasks):
            for key, value in part_smallest.items():
                if key not in smallest or value[0] < smallest[key][0]:
                    smallest[key] = value
            windows.extend(part_windows)
    print(f"// own check of 2^-54 <= |x| < 2^-30: {positive} positive and {negative} negative midpoints")
    for key in sorted(smallest):
        distance, x = smallest[key]
        print(f"//   sign {key[0]:+d}, binade {key[1]}: the hardest 2^{log2_text(distance)} ulp at {x.hex()}")
    listed = {
        entry[0]
        for _, _, entries in blocks
        for entry in entries
        if COVERAGE_LOW <= abs(entry[0]) < COVERAGE_HIGH and entry[2] < mp.ldexp(1, COVERAGE_WINDOW)
    }
    for sign in (1, -1):
        own = sorted((abs(low), high) for low, high in windows if (low > 0) == (sign > 0))
        starts = [low for low, _ in own]
        mine = [x for x in listed if (x > 0) == (sign > 0)]
        outside = 0
        for x in mine:
            place = bisect.bisect_right(starts, bits_of(abs(x))) - 1
            if place < 0 or bits_of(abs(x)) > own[place][1]:
                outside += 1
        total = sum(high - low + 1 for low, high in own)
        print(
            f"//   sign {sign:+d}: {total} inputs within 2^{COVERAGE_WINDOW} ulp of a midpoint, {len(mine)} of them on"
            f" the list, and {outside} inputs of the list outside each window"
        )


def print_exp_coverage() -> None:
    """Print the coverage of the correct round of exp(double): the list of CORE-MATH, the own check and the edges.

    The comment of Exp.h copies the results.  The run takes some minutes on a pool of 32 processes.
    """
    found = subprocess.run(
        [sys.executable, "-S", str(SETUP), "install", "--print-bin", "core-math"],
        capture_output=True,
        text=True,
        check=False,
    )
    if found.returncode != 0 or not found.stdout.strip():
        raise SystemExit(
            "fp_tables: the pinned core-math is not installed.  Run: python3 utils/bin/setup install core-math"
        )
    blocks = read_exp_list(Path(found.stdout.strip()) / "src" / "binary64" / "exp" / "exp.wc")
    print_exp_list(blocks)
    print_exp_own_check(blocks)
    for name, edge in (
        ("+inf above", mp.log(mp.ldexp(1, 1024) - mp.ldexp(1, 970))),
        ("+0 below", mp.log(mp.ldexp(1, -1075))),
        ("a normal result above", mp.log(mp.ldexp(1, -1022))),
    ):
        for x in neighbors_of(edge):
            kind, to_mid, _, _ = measure_exp(x)
            print(f"// edge, {name}: {x.hex()} gives a {kind} result, 2^{log2_text(to_mid)} ulp from the boundary")


SUBCOMMANDS: dict[str, Callable[[], None]] = {
    "exp": print_exp,
    "exp-coverage": print_exp_coverage,
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
