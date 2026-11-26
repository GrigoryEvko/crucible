#pragma once

// The doors of bench_fp_sweep to its two oracles, the pins of utils/pins.txt:
//
//   correctly_rounded(function, x)   the correctly rounded result of the
//                                    pinned CORE-MATH, for a float and a
//                                    double
//   error_in_ulp(function, x, y)     |f(x) - y| in units of the last place of
//                                    a double at f(x), with f(x) of MPFR at
//                                    200 bits.  A subnormal f(x) has the last
//                                    place 2^-1074
//
// bench/fp_sweep_oracles.cpp holds the doors in a region of the C ABI of the
// two libraries.  Only the preset bench builds them (CRUCIBLE_FP_SWEEP), and
// the product links none of their code.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <cstdint>

namespace fp_sweep {

// The function of a sweep.
enum class Function : std::uint8_t {
    Exp,
    Log,
    Erf,
};

namespace side {

// The input of a function, and the result of the function under test.
struct Input final {};
struct Result final {};

}  // namespace side

[[nodiscard]] float correctly_rounded(Function function, float input) noexcept;
[[nodiscard]] double correctly_rounded(Function function, double input) noexcept;

[[nodiscard]] double error_in_ulp(Function function, ::fixy::Side<side::Input, double> input,
                                  ::fixy::Side<side::Result, double> result) noexcept;

}  // namespace fp_sweep
