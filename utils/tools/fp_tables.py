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


def mpf_of(value: Fraction) -> Any:
    """Return a Fraction as an mpmath value, rounded at the working precision."""
    return mp.mpf(value.numerator) / value.denominator


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
    # The rounds of q: b = RN(tail + tail r_hi), c = RN(r_lo + r_lo r_hi), the pairs of P', P',
    # q1 = RN(r_hi^2 P' + b) and q = RN(q1 + c).  c joins after the polynomial, so r_lo is off the longest chain.
    # A level with no fma computes c = RN(r_lo + RN(r_lo r_hi)), and the budget takes the larger of the two forms.
    b_most = tail_most * (1 + reach) * (1 + u)
    product_most = low_most * reach * (1 + u)
    c_round = max(u * low_most * (1 + reach), u * low_most * reach + u * (low_most + product_most))
    c_most = (low_most + product_most) * (1 + u)
    low_pair = c2 + c3 * reach
    high_pair = c4 + c5 * reach
    square_most = reach**2 * (1 + u)
    poly_most = (low_pair + high_pair * square_most) * (1 + u) ** 2
    poly_round = u * low_pair + u * reach**2 * high_pair * (1 + u) + reach**2 * u * high_pair + u * poly_most
    q1_most = (b_most + square_most * poly_most) * (1 + u)
    q_most = (q1_most + c_most) * (1 + u)
    terms = [
        ("tau r_lo (dropped)", tail_most * low_most),
        ("tau r^2 P' (dropped)", tail_most * g_most),
        ("(tau - tail) (1 + r_hi) (table)", tail_error * (1 + reach)),
        (
            "g(r_hi + r_lo) - g(r_hi) - r_hi r_lo (dropped)",
            low_most * (c2_gap * reach + 3 * c3 * reach**2 + 4 * c4 * reach**3 + 5 * c5 * reach**4)
            + low_most**2 * g_second / 2,
        ),
        ("round of b", u * b_most),
        ("rounds of c", c_round),
        ("round of r_hi^2, times P'", u * reach**2 * poly_most),
        ("rounds of P', times r_hi^2", reach**2 * poly_round),
        ("round of q1", u * q1_most),
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


def double_of_bits(bits: int) -> float:
    """Return the double of a bit pattern."""
    return float(struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0])


# ── log, double ────────────────────────────────────────────────────
#
# The fast path takes x = 2^e y with y in [0x1.6ap-1, 0x1.6ap0), as the table method of ARM does: t = bits(x) -
# bits(0x1.6ap-1), e = t >> 52 with its sign, and the row i = (t >> 43) mod 512.  The rows split the range of y
# into steps of 2^-10 below 1 and of 2^-9 above 1, which are the steps of 2^43 in the bits.  A 10-bit inverse
# r_i of each row gives an exact z = r_i y - 1.  The path near 1 takes r = x - 1.  The slow path takes the
# reduction of the fast path and a second one: k2 = round(z 2^16) and r2 nearest 1 / (1 + k2 2^-16).

# The start of the range of y, and the counts of the rows below 1 and above 1.
LOG_START = Fraction(724, 1024)
LOG_ROWS_BELOW = 300
LOG_ROWS_ABOVE = 212
LOG_SECOND_STEP = 16
# The path near 1: 1 - 2^-9 <= x < 1 + 2^-9.
LOG_NEAR = Fraction(1, 2**9)


def is_exact_reduction(inverse: Fraction, low: Fraction, high: Fraction) -> bool:
    """Return whether z = fma(r, y, -1) is exact for each double y in [low, high).

    y = M 2^-52 for y >= 1 and M 2^-53 below 1.  r y - 1 = N 2^-s for an integer N in steps of the numerator R
    of r, so N has the trailing zeros of R at least.  The fma is exact when |N| < 2^(53 + those zeros).
    """
    unit = Fraction(1, 2**52) if low >= 1 else Fraction(1, 2**53)
    numerator = inverse.numerator
    zeros = (numerator & -numerator).bit_length() - 1
    largest = max(abs(inverse * low - 1), abs(inverse * high - 1)) * inverse.denominator / unit
    return largest < 2 ** (53 + zeros)


def significant_bits(value: Fraction) -> int:
    """Return the number of significant bits of a dyadic fraction: its odd numerator part has that many."""
    numerator = abs(value.numerator)
    if numerator == 0:
        return 0
    return (numerator >> ((numerator & -numerator).bit_length() - 1)).bit_length()


def ten_bit_inverse(low: Fraction, high: Fraction) -> Fraction:
    """Return the inverse of at most 10 significant bits with the smallest max |r y - 1| on [low, high].

    The candidates are the two neighbors of 2 / (low + high) at 10 and at 9 significant bits, and the inverse
    must give an exact z (is_exact_reduction).
    """
    best = Fraction(0)
    worst_best = Fraction(10)
    ideal = 2 / (low + high)
    exponent = 0 if ideal < 1 else 1
    for bits in (10, 9):
        scale = 2 ** (bits - exponent)
        for numerator in (math.floor(ideal * scale), math.ceil(ideal * scale)):
            candidate = Fraction(numerator, scale)
            worst = max(abs(candidate * low - 1), abs(candidate * high - 1))
            if worst < worst_best and is_exact_reduction(candidate, low, high):
                best, worst_best = candidate, worst
    if best == 0:
        raise SystemExit(f"fp_tables: no inverse of 10 bits gives an exact reduction on [{low}, {high})")
    return best


def log_intervals() -> list[tuple[Fraction, Fraction]]:
    """Return the interval [low, high) of y of each row of the first table, in the order of the rows."""
    below = [(LOG_START + Fraction(step, 1024), LOG_START + Fraction(step + 1, 1024)) for step in range(LOG_ROWS_BELOW)]
    above = [(1 + Fraction(step, 512), 1 + Fraction(step + 1, 512)) for step in range(LOG_ROWS_ABOVE)]
    if below[-1][1] != 1 or above[-1][1] != 2 * LOG_START:
        raise SystemExit("fp_tables: the rows of the first table do not split [0x1.6ap-1, 0x1.6ap0)")
    return below + above


def log_first_table() -> list[tuple[tuple[Fraction, Fraction], Fraction, float, float, float]]:
    """Return ((low, high), r_i, l1, l2, l3) for each row: -log(r_i) = l1 + l2 + l3 with l1 a multiple of 2^-42.

    r_i = 1 for the three rows from 1 - 2^-9 to 1 + 2^-9, so -log(r_i) = 0 there.  O(512) logs.
    """
    rows = []
    for low, high in log_intervals():
        is_near = 1 - LOG_NEAR <= low < 1 + LOG_NEAR
        inverse = Fraction(1) if is_near else ten_bit_inverse(low, high)
        value = -mp.log(mp.mpf(inverse.numerator) / inverse.denominator)
        part1 = round_to_multiple(value, -42)
        part2 = round_double(value - part1)
        part3 = round_double(value - part1 - mp.mpf(part2))
        rows.append(((low, high), inverse, round_double(part1), part2, part3))
    return rows


def log_reduction_width(
    rows: list[tuple[tuple[Fraction, Fraction], Fraction, float, float, float]],
) -> tuple[Fraction, bool]:
    """Return the largest |r_i y - 1| over each interval, and whether each z = fma(r_i, y, -1) is exact.

    y = M 2^-52 for y >= 1 and M 2^-53 below 1, with an integer M.  r_i y - 1 is an integer N over 2^(52 + q),
    with r_i = R 2^-q.  The fma is exact when each such |N| is below 2^53.  O(512).
    """
    widest = Fraction(0)
    is_exact = True
    for (low, high), inverse, _, _, _ in rows:
        widest = max(widest, abs(inverse * low - 1), abs(inverse * high - 1))
        is_exact = is_exact and is_exact_reduction(inverse, low, high)
    return widest, is_exact


def rigorous_log1p_error(
    coefficients: Sequence[float | Fraction], bound: Fraction, relative: bool, terms: int = 40, pieces: int = 256
) -> Fraction:
    """Return a bound of |log1p(z) - p(z)|, or of it over |log1p(z)|, for each |z| <= bound <= 1/4.

    The coefficients of p come first at degree 0.  log1p is its Taylor sum to `terms` terms plus a rest of at
    most bound^(terms + 1) / ((terms + 1) (1 - bound)).  For the relative bound, (T - p) / z is a polynomial, as
    p(0) = 0, and |log1p(z) / z| >= 1 - bound / 2.  Exact.  O(pieces * terms^2).
    """
    taylor = [Fraction(0)] + [Fraction((-1) ** (n + 1), n) for n in range(1, terms + 1)]
    difference = [taylor[n] - (fraction_of(coefficients[n]) if n < len(coefficients) else 0) for n in range(terms + 1)]
    rest = bound ** (terms + 1) / ((terms + 1) * (1 - bound))
    if not relative:
        return sup_bound(difference, -bound, bound, pieces) + rest
    if difference[0] != 0:
        raise SystemExit("fp_tables: a relative bound of log1p needs p(0) = 0")
    return (sup_bound(difference[1:], -bound, bound, pieces) + rest / bound) / (1 - bound / 2)


def log_second_table(width: Fraction) -> tuple[list[tuple[int, float, float, float, float]], Fraction, Fraction]:
    """Return the rows (k2, r2, m1, m2, m3) of the second table, the largest |z2| and the largest table rest.

    k2 = round(z 2^16) for |z| <= width, r2 is the double nearest 1 / (1 + k2 2^-16), and -log(r2) is
    m1 + m2 + m3 in three doubles.  The largest |z2| = |r2 (1 + z) - 1| comes from the two ends of the interval
    of z of each k2.  O(2 width 2^16) logs.
    """
    step = Fraction(1, 2**LOG_SECOND_STEP)
    count = math.ceil(width / step)
    rows = []
    second_width = Fraction(0)
    second_error = Fraction(0)
    for k2 in range(-count, count + 1):
        inverse = round_double(1 / (1 + mp.mpf(k2) / 2**LOG_SECOND_STEP))
        low = max(-width, (k2 - Fraction(1, 2)) * step)
        high = min(width, (k2 + Fraction(1, 2)) * step)
        exact_inverse = Fraction(inverse)
        second_width = max(second_width, abs(exact_inverse * (1 + low) - 1), abs(exact_inverse * (1 + high) - 1))
        value = -mp.log(mp.mpf(inverse))
        part1 = round_double(value)
        part2 = round_double(value - mp.mpf(part1))
        part3 = round_double(value - mp.mpf(part1) - mp.mpf(part2))
        rest = abs(value - mp.mpf(part1) - mp.mpf(part2) - mp.mpf(part3))
        if rest != 0:
            second_error = max(second_error, dyadic_above(rest))
        rows.append((k2, inverse, part1, part2, part3))
    return rows, second_width, second_error


def print_log_block(
    ln2_parts: Sequence[float],
    fast: Sequence[float],
    near: Sequence[float],
    slow_parts: Sequence[float],
    rows: list[tuple[tuple[Fraction, Fraction], Fraction, float, float, float]],
    second_rows: list[tuple[int, float, float, float, float]],
) -> None:
    """Print the block of constants and tables of Log.h, from `// clang-format off` to `// clang-format on`.

    The block in Log.h is this output, line for line, so a diff of the two shows each change.
    """
    ln2_high, ln2_low, ln2_middle, ln2_third = ln2_parts
    first, last = second_rows[0][0], second_rows[-1][0]
    lines = [
        "// clang-format off",
        "// ln2 = kLogLn2High + kLogLn2Low for the fast path, and kLogLn2High + kLogLn2Middle + kLogLn2Third for",
        "// the slow path.  kLogLn2High has 42 bits and kLogLn2Middle 40 bits, so their products with e are exact.",
        f"inline constexpr double kLogLn2High = {hex_double(ln2_high)};",
        f"inline constexpr double kLogLn2Low = {hex_double(ln2_low)};",
        f"inline constexpr double kLogLn2Middle = {hex_double(ln2_middle)};",
        f"inline constexpr double kLogLn2Third = {hex_double(ln2_third)};",
        "// The fast polynomial: log1p(z) - z = z^2 (P2 + P3 z + P4 z^2 + P5 z^3 + P6 z^4).",
        *(f"inline constexpr double kLogP{index} = {hex_double(value)};" for index, value in enumerate(fast, 2)),
        "// The polynomial near 1: log1p(r) - r + r^2/2 = r^3 (B1 + B2 r + B3 r^2 + B4 r^3 + B5 r^4).",
        *(f"inline constexpr double kLogB{index} = {hex_double(value)};" for index, value in enumerate(near, 1)),
        "// The Taylor coefficients (-1)^(n+1) / n of the slow path, in two doubles to n = 5.",
        *(f"inline constexpr double kLogSlow{name} = {hex_double(value)};" for name, value in slow_parts),
        "// For each row i of y, steps of 2^-10 from 0x1.6ap-1 to 1 and steps of 2^-9 from 1 to 0x1.6ap0: the",
        "// inverse r_i of 10 or fewer significant bits (1 for the three rows from 1 - 2^-9 to 1 + 2^-9), then",
        "// -log(r_i) in three doubles, the first a multiple of 2^-42.",
        f"inline constexpr LogEntry kLogFirst[{len(rows)}] = {{",
        *(
            f"    {{{hex_double(float(inverse))}, {hex_double(part1)}, {hex_double(part2)}, {hex_double(part3)}}},"
            for _, inverse, part1, part2, part3 in rows
        ),
        "};",
        f"// For each k2 from {first} to {last}: r2, the double nearest 1 / (1 + k2 2^-16), then -log(r2) in three",
        "// doubles.",
        f"inline constexpr LogEntry kLogSecond[{len(second_rows)}] = {{",
        *(
            f"    {{{hex_double(inverse)}, {hex_double(part1)}, {hex_double(part2)}, {hex_double(part3)}}},"
            for _, inverse, part1, part2, part3 in second_rows
        ),
        "};",
        "// clang-format on",
    ]
    print("\n".join(lines))


def print_log() -> None:
    """Print the constants, the polynomials and the three error budgets of the double log of Log.h."""
    u = Fraction(1, 2**53)
    rows = log_first_table()
    width, is_exact = log_reduction_width(rows)
    smallest_first = min(abs(Fraction(row[2]) + Fraction(row[3])) for row in rows if row[1] != 1)
    table_error = max(
        dyadic_above(abs(-mp.log(mp.mpf(row[1].numerator) / row[1].denominator) - mp.mpf(row[2]) - mp.mpf(row[3])))
        for row in rows
        if row[1] != 1
    )
    table_rest3 = max(
        dyadic_above(
            abs(
                -mp.log(mp.mpf(row[1].numerator) / row[1].denominator)
                - mp.mpf(row[2])
                - mp.mpf(row[3])
                - mp.mpf(row[4])
            )
        )
        for row in rows
        if row[1] != 1
    )
    print(
        f"// first reduction: |z| <= 2^{log2_of_fraction(width)}, z exact: {is_exact},"
        f" smallest |l1 + l2| off 1: 2^{log2_of_fraction(smallest_first)},"
        f" |-log r - l1 - l2| <= 2^{log2_of_fraction(table_error)}, with l3: 2^{log2_of_fraction(table_rest3)}"
    )
    # The fast path takes FastTwoSum(e ln2_high + l1, z): for e = 0 off 1, the |l1| of each row must be at least
    # each |z| of the row.  O(512).
    smallest_ratio = min(
        abs(Fraction(part1)) / max(abs(inverse * low - 1), abs(inverse * high - 1))
        for (low, high), inverse, part1, _, _ in rows
        if inverse != 1
    )
    if not is_exact or smallest_ratio < 1:
        raise SystemExit("fp_tables: the first reduction is not exact, or a row has |l1| below its |z|")
    print(f"// each row off 1 has |l1| >= {float(smallest_ratio):.3f} times its largest |z|")
    # e != 0: |log x| >= |e| ln2 - |log y| >= |e| (ln2 - the largest |log y|), so the errors in e count over
    # that, with the factor exponent_factor >= 1 / (ln2 - the largest |log y|).
    largest_log_y = dyadic_above(max(-mp.log(mpf_of(LOG_START)), mp.log(mpf_of(2 * LOG_START))))
    exponent_factor = dyadic_above(1 / (mp.log(2) - mpf_of(largest_log_y)))
    print(
        f"// |log y| <= 2^{log2_of_fraction(largest_log_y)}, so |log x| >= |e| / {float(exponent_factor):.6f} for"
        " e != 0"
    )
    ln2 = mp.log(2)
    ln2_a = round_to_multiple(ln2, -42)
    ln2_low = round_double(ln2 - ln2_a)
    ln2_gap = dyadic_above(abs(ln2 - ln2_a - mp.mpf(ln2_low)))
    ln2_b = round_to_multiple(ln2 - ln2_a, -84)
    ln2_c = round_double(ln2 - ln2_a - ln2_b)
    ln2_rest3 = dyadic_above(abs(ln2 - ln2_a - ln2_b - mp.mpf(ln2_c)))
    print(f"// ln2 = {hex_double(round_double(ln2_a))} + {hex_double(ln2_low)}, the rest 2^{log2_of_fraction(ln2_gap)}")
    print(
        f"// ln2 in three parts for the slow path: {hex_double(round_double(ln2_a))} + {hex_double(round_double(ln2_b))}"
        f" + {hex_double(ln2_c)}, the rest 2^{log2_of_fraction(ln2_rest3)}"
    )
    reach = width * (1 + Fraction(1, 2**20))

    def log_tail(point: Any) -> Any:
        if point == 0:
            return mp.mpf(-1) / 2
        return (mp.log1p(point) - point) / point**2

    def log_weight(point: Any) -> Any:
        return point**2

    fast = fit_and_print("fast P", log_tail, log_weight, -mpf_of(reach), mpf_of(reach), 4, 2)
    fast_error = rigorous_log1p_error([0.0, 1.0, *fast], reach, relative=False)
    print(f"// rigorous absolute bound of the fast polynomial: 2^{log2_of_fraction(fast_error)}")

    def near_tail(point: Any) -> Any:
        if point == 0:
            return mp.mpf(1) / 3
        return (mp.log1p(point) - point + point**2 / 2) / point**3

    def near_weight(point: Any) -> Any:
        if point == 0:
            return mp.mpf(0)
        return abs(point**3 / mp.log1p(point))

    near_reach = LOG_NEAR
    near = fit_and_print("near B", near_tail, near_weight, -mpf_of(near_reach), mpf_of(near_reach), 4, 1)
    near_error = rigorous_log1p_error([0.0, 1.0, -0.5, *near], near_reach, relative=True)
    print(f"// rigorous relative bound of the near polynomial: 2^{log2_of_fraction(near_error)}")
    second_rows, second_width, second_error = log_second_table(width)
    print(
        f"// second reduction: {len(second_rows)} entries, |z2| <= 2^{log2_of_fraction(second_width)},"
        f" table rest <= 2^{log2_of_fraction(second_error)}"
    )
    slow_reach = second_width * (1 + Fraction(1, 2**20))
    slow_terms: list[Fraction] = [Fraction(0), Fraction(1), Fraction(-1, 2)]
    slow_parts: list[tuple[str, float]] = []
    for order in range(3, 10):
        exact = Fraction((-1) ** (order + 1), order)
        high_part = float(exact)
        low_part = float(exact - Fraction(high_part)) if order in (3, 5) else 0.0
        slow_terms.append(Fraction(high_part) + Fraction(low_part))
        if order in (3, 5):
            slow_parts += [(f"C{order}High", high_part), (f"C{order}Low", low_part)]
        else:
            slow_parts.append((f"C{order}", high_part))
    slow_error = rigorous_log1p_error(slow_terms, slow_reach, relative=True)
    print(f"// rigorous relative bound of the slow polynomial (degree 9): 2^{log2_of_fraction(slow_error)}")
    exponent_bits = [significant_bits(Fraction(value)) for value in (round_double(ln2_a), round_double(ln2_b))]
    if exponent_bits[0] > 42 or exponent_bits[1] > 42:
        raise SystemExit(f"fp_tables: a product e ln2 part is not exact: {exponent_bits} bits")
    print(f"// ln2 parts: {exponent_bits[0]} and {exponent_bits[1]} significant bits, |e| <= 1074 has 11")
    log_fast_budget(fast, reach, fast_error, rows, u, ln2_low, ln2_gap, table_error)
    log_near_budget(near, near_reach, near_error, u)
    log_slow_budget(
        slow_terms,
        slow_error,
        second_rows,
        (second_width, slow_reach, width),
        (table_rest3, second_error, ln2_rest3),
        (u, exponent_factor),
    )
    print_log_block(
        (round_double(ln2_a), ln2_low, round_double(ln2_b), ln2_c), fast, near, slow_parts, rows, second_rows
    )


def log_fast_budget(
    coefficients: Sequence[float],
    reach: Fraction,
    poly_error: Fraction,
    rows: list[tuple[tuple[Fraction, Fraction], Fraction, float, float, float]],
    u: Fraction,
    ln2_low: float,
    ln2_gap: Fraction,
    table_error: Fraction,
) -> None:
    """Print the absolute error budget of the fast path, in the order of its operations.  Exact.

    p = RN(RN(z2 a) + z4 c), the Estrin form, with a = RN(P2 + P3 z), z2 = RN(z^2), z4 = RN(z2^2) and
    c = RN(RN(P4 + P5 z) + P6 z2).  l = RN(p + RN(l + RN(e ln2_low + l2))): e ln2_low + l2 does not wait for z,
    and the sum of the low parts does not wait for p, so the add of p is the one step after the polynomial.  Each
    bound is of an absolute value, and each error is against the exact value of its step.
    """
    p2, p3, p4, p5, p6 = (abs(Fraction(value)) for value in coefficients)
    e_most = Fraction(1075)
    square = reach**2
    a_exact = p2 + p3 * reach
    a_most = a_exact * (1 + u)
    b_exact = p4 + p5 * reach
    z2_most = square * (1 + u)
    err_z2 = u * square
    c_exact = b_exact * (1 + u) + p6 * z2_most
    err_c = u * b_exact + p6 * err_z2 + u * c_exact
    c_most = c_exact * (1 + u)
    err_z4 = err_z2 * (z2_most + square) + u * z2_most**2
    z4_most = z2_most**2 * (1 + u)
    err_t = err_z2 * a_most + square * u * a_exact + u * z2_most * a_most
    p_exact = z2_most * a_most * (1 + u) + z4_most * c_most
    err_poly = err_t + err_z4 * c_most + square**2 * err_c + u * p_exact
    p_most = p_exact * (1 + u)
    l2_most = max(abs(Fraction(row[3])) for row in rows)
    l_most = Fraction(1, 2**44)
    la_most = (e_most * abs(Fraction(ln2_low)) + l2_most) * (1 + u)
    lb_most = (l_most + la_most) * (1 + u)
    lc_most = (p_most + lb_most) * (1 + u)
    terms = [
        ("the polynomial (rigorous)", poly_error),
        ("the rounds of the polynomial", err_poly),
        ("the table: -log r - l1 - l2", table_error),
        ("e (ln2 - the two parts), |e| <= 1075", e_most * ln2_gap),
        ("round of e ln2_low + l2", u * la_most / (1 + u)),
        ("round of l + (e ln2_low + l2)", u * lb_most),
        ("round of ph + (...)", u * lc_most),
    ]
    total = print_terms("log fast path: the absolute errors of h + l", terms)
    needed = total + u * (lc_most + total)
    print(
        f"// log eps_fast = 2^{log2_of_fraction(total)} absolute, the test needs a margin of 2^{log2_of_fraction(needed)}"
    )


def log_near_budget(coefficients: Sequence[float], reach: Fraction, poly_error: Fraction, u: Fraction) -> None:
    """Print the relative error budget of the path near 1, in the order of its operations.  Exact.

    r = x - 1 is exact, r^2 is an exact pair (q_hi, q_lo), R is a Horner sum of B1..B5 in r, c = RN(RN(r q_hi) R),
    (h, l0) = Fast2Sum(r, -q_hi / 2), and l = RN(l0 + RN(-q_lo / 2 + c)).  Each term is relative to |r|, and
    |log1p(r)| >= |r| (1 - reach / 2).
    """
    magnitudes = [abs(Fraction(value)) for value in coefficients]
    horner = magnitudes[-1]
    horner_error = Fraction(0)
    for value in reversed(magnitudes[:-1]):
        horner = (value + reach * horner) * (1 + u)
        horner_error = reach * horner_error + u * horner
    cube = reach**2 * (1 + u) ** 2
    c_most = cube * horner * (1 + u)
    terms_over_r = [
        ("the rounds of R, times r^2", reach**2 * horner_error),
        ("the rounds of r q_hi and of c", reach**2 * horner * (2 * u + u * u) + u * c_most),
        ("round of -q_lo/2 + c", u * (u * reach / 2 + c_most)),
        ("round of l0 + (...)", u * (u + u * reach + c_most) * (1 + u)),
    ]
    rounds = print_terms("log near path: the rounding errors, over |r|", terms_over_r)
    total = poly_error + rounds / (1 - reach / 2)
    l_most = (u + u * reach + c_most) * (1 + u) ** 2
    print(
        f"// log eps_near = 2^{log2_of_fraction(total)} relative (polynomial 2^{log2_of_fraction(poly_error)}),"
        f" the test eps |r| needs eps >= 2^{log2_of_fraction(total * (1 + reach) + u * (l_most + total))}"
    )


def log_slow_budget(
    slow_terms: Sequence[Fraction],
    poly_error: Fraction,
    second_rows: list[tuple[int, float, float, float, float]],
    widths: tuple[Fraction, Fraction, Fraction],
    rests: tuple[Fraction, Fraction, Fraction],
    units: tuple[Fraction, Fraction],
) -> None:
    """Print the relative error budget of the slow path, in the order of its operations.  Exact.

    widths holds the largest |z2|, the bound R of |z_hi + w| (it is a little larger) and the largest |z1|.
    rests holds the rests of the first table and of the second table, and of ln2 in three parts.  units holds
    u and the factor F of the regime e != 0, with |log x| >= |e| / F.  The path, with
    x = 2^e y, |e| <= 1074, and z1 = r1 y - 1 exact:
      - k2 = round(z1 2^16), and z2 = (r2 - 1) + r2 z1 = z_hi + w + t exactly: r2 - 1 is exact, r2 z1 is an exact
        pair (p_hi, p_lo), (s_hi, s_lo) = TwoSum(r2 - 1, p_hi), (m, t) = TwoSum(s_lo, p_lo), and (z_hi, w) =
        TwoSum(s_hi, m).  So z_hi = RN(z_hi + w), |w| <= u |z_hi|, and |t| <= u |m| <= u^2 (1 + u)^2 (|z2| +
        (1 + 2u) |r2 z1|).
      - log1p(z2) = z2 - z2^2/2 + f(z2), with f(z) = z^3 Q(z).  z2^2/2 takes z_hi^2 and z_hi w as exact pairs,
        z_hi t and w^2 with one round each, and drops w t and t^2/2.  f(z_hi + w) is a Horner sum in pairs: Q
        has c3 and c5 in pairs, c4 = -1/4, and c6..c9 in doubles on z_hi.  f(z2) - f(z_hi + w) = t xi^2 / (1 + xi)
        for a xi between the two, because f'(z) = z^2 / (1 + z), and the path adds RN(RN(z_hi^2) t) for it.
      - The sum: h starts at e ln2_high + l1, which is exact: two multiples of 2^-42 below 2^10.  13 terms go
        through (h, s) = TwoSum(h, v), (m, s') = TwoSum(m, s), l = RN(l + s'), and 7 small terms go to l with
        one round each.  FastTwoSum(h, m) and the TwoSum of its low part with l are exact.  So the sum is exact
        except the 20 rounds of l.  Each partial sum h is at most B |log x| (B is below), so |m| <= 13 u B
        |log x| (1 + u)^14, and each round of l is at most u times the bound of |l|.
      - e ln2 = e ln2_high + e ln2_middle + e ln2_third: the first two products are exact (42 and 40 bits times
        11), and the third is an exact pair: its high part is a term of h, its low part a term of l.
    The errors count over |log x| in four regimes:
      - near 1 (r1 = 1, e = 0) with k2 = 0: z2 = z1, t = w = 0, and |log x| >= |z2| (1 - 2^-10).
      - near 1 with k2 != 0: |z1| > 2^-17, |log x| >= |z1| (1 - 2^-10), |z2| <= the largest |z2|, and |t| <=
        t_near |z1|, with the largest |r2| of the entries |k2| <= 128.
      - off 1 with e = 0: |log x| >= log(1 + 2^-9) > 1/513.
      - e != 0: |log x| >= |e| ln2 - |log y| >= |e| / F, as F (ln2 - the largest |log y|) >= 1.
    """
    second_width, reach, first_width = widths
    first_rest, second_error, ln2_rest = rests
    u, exponent_factor = units
    add_dd = 3 * u * u / (1 - 4 * u)
    times_dd = 5 * u * u
    times_fp = 2 * u * u
    c = [abs(value) for value in slow_terms]
    horner = c[9]
    horner_error = Fraction(0)
    for value in (c[8], c[7], c[6]):
        horner = (value + reach * horner) * (1 + u)
        horner_error = reach * horner_error + u * horner
    a_most = c[5] + reach * horner
    a_error = add_dd * a_most * (1 + u) + times_fp * reach * horner + reach * horner_error
    for value in (c[4], c[3]):
        product = reach * a_most * (1 + u)
        a_most_next = value + product
        a_error = add_dd * a_most_next * (1 + u) + times_dd * product + reach * a_error
        a_most = a_most_next * (1 + u)
    cube_error = 2 * times_dd * reach**3 * (1 + 4 * u)
    w_error = times_dd * reach**3 * a_most + reach**3 * a_error + cube_error * a_most
    # H reads z_hi only, so it drops w, |w| <= u (1 + u) |z2|, times |H'| <= c7 + 2 c8 R + 3 c9 R^2, which the
    # two cubes scale by |z2|^6.
    slope = c[7] + 2 * c[8] * reach + 3 * c[9] * reach**2
    over_z = [
        ("f(z_hi + w): the double-word rounds", w_error / reach),
        ("f(z_hi + w): H on z_hi, which drops w", u * (1 + u) * slope * reach**6),
        ("the round of w^2, the part in z2", u**3 * (1 + u) ** 2 * reach / 2),
        ("the polynomial, over |log1p| <= |z2| / (1 - R)", poly_error / (1 - reach)),
    ]
    relative_z = print_terms("log slow path: the errors over |z2|", over_z)
    # The bounds of t, and the absolute errors in t: each is at most t G.
    near_count = round(LOG_NEAR * 2**LOG_SECOND_STEP)
    r2_most = max(abs(Fraction(row[1])) for row in second_rows)
    r2_near = max(abs(Fraction(row[1])) for row in second_rows if abs(row[0]) <= near_count)
    t_most = u * u * (1 + u) ** 2 * (second_width + (1 + 2 * u) * r2_most * first_width)
    t_near = u * u * (1 + u) ** 2 * (second_width * 2**17 + (1 + 2 * u) * r2_near)
    z_hi_most = (1 + u) * (second_width + t_most)
    delta = u * z_hi_most + t_most
    in_t = [
        ("the round of z_hi t", u * z_hi_most),
        ("w t, which z2^2/2 drops", u * z_hi_most),
        ("t^2/2, which z2^2/2 drops", t_most / 2),
        ("the round of w^2, the part in t", u**3 * (1 + u) ** 2 * (2 * second_width + t_most) / 2),
        ("f(z2) - f(z_hi + w) - z_hi^2 t", ((2 * z_hi_most + delta) * delta + z_hi_most**2 * reach) / (1 - reach)),
        ("z_hi^2 - RN(z_hi^2), times t", u * z_hi_most**2),
        ("the round of RN(z_hi^2) t", u * (1 + u) * z_hi_most**2),
    ]
    t_coefficient = print_terms("log slow path: the errors in t, over |t|", in_t)
    print(f"// |t| <= 2^{log2_of_fraction(t_most)}, and |t| <= 2^{log2_of_fraction(t_near)} |z1| near 1 with k2 != 0")
    # The partial sums: h0 = e ln2_high + l1 is log x less the rest of the terms, at most |log1p(z1)| + |e (ln2 -
    # ln2_high)| + |l2 + l3|, and |l2 + l3| <= 2^-43 plus the rest, as l1 is the round to a multiple of 2^-42.
    # Near 1, the first partial sum m1 = -log r2 is at most |log x| + |log1p(z2)|, and each later one is log x
    # less terms of 2^-34 or less.
    first_tail = first_width / (1 - first_width) + 1075 * Fraction(1, 2**43) + Fraction(1, 2**42)
    near_tail = second_width / (1 - second_width) / (Fraction(1, 2**17) * (1 - Fraction(1, 2**10)))
    most_partial = max(1 + first_tail * exponent_factor, 1 + first_tail * 513, 1 + near_tail) + Fraction(1, 2**30)
    if most_partial >= 3:
        raise SystemExit(f"fp_tables: a partial sum of the slow log can be 2^{log2_of_fraction(most_partial)} |log x|")
    two_level, direct = 13, 7
    middle_most = two_level * u * most_partial * (1 + u) ** (two_level + 1)
    t_relative = max(t_near / (1 - Fraction(1, 2**10)), 513 * t_most)
    direct_most = (
        u * u * (1 + u) ** 3 * most_partial
        + t_relative * (1 + (1 + u) * z_hi_most + (1 + u) ** 2 * z_hi_most**2)
        + 4 * u * u * (1 + u) * z_hi_most
        + u * Fraction(1, 2**85) * exponent_factor
    )
    low_most = (two_level * u * middle_most + direct_most) * (1 + u) ** (two_level + direct)
    sum_rounds = (two_level + direct) * u * low_most
    print(
        f"// the sum: each partial sum h is at most 2^{log2_of_fraction(most_partial)} |log x|, |l| <="
        f" 2^{log2_of_fraction(low_most)} |log x|, and its 20 rounds cost 2^{log2_of_fraction(sum_rounds)} |log x|"
    )
    reduced = 1 - Fraction(1, 2**10)
    at_one = relative_z / reduced + sum_rounds
    near_one = (relative_z * second_width * 2**17 + t_near * t_coefficient + second_error * 2**17) / reduced
    near_one += sum_rounds
    absolute = relative_z * reach + t_most * t_coefficient + first_rest + second_error
    off_one = 513 * absolute + sum_rounds
    exponent = exponent_factor * (absolute + ln2_rest) + sum_rounds
    regimes = [
        ("near 1, k2 = 0", at_one),
        ("near 1, k2 != 0", near_one),
        ("off 1, e = 0: the absolute errors over 1/513", off_one),
        ("e != 0: the absolute errors and e (ln2 - its three parts), over |e| / F", exponent),
    ]
    print("// log slow path: the relative error in each regime")
    for title, value in regimes:
        print(f"//   {title}: 2^{log2_of_fraction(value)}")
    worst = max(value for _, value in regimes)
    print(f"// log eps_acc = 2^{log2_of_fraction(worst)} relative")


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
