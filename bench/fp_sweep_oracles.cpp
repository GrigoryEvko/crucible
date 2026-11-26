// The doors of bench_fp_sweep to the pinned CORE-MATH and the pinned MPFR
// (bench/fp_sweep_oracles.h).  The library core_math_reference of
// bench/CMakeLists.txt holds the six C files of CORE-MATH, and the static
// library of MPFR comes from its configure script, against the pinned GMP.

#include "fp_sweep_oracles.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: CORE-MATH and MPFR are C libraries, the oracles of the sweep of fixy/fp")

#include <mpfr.h>

extern "C" {
float cr_expf(float);
float cr_logf(float);
float cr_erff(float);
double cr_exp(double);
double cr_log(double);
double cr_erf(double);
}

namespace fp_sweep {

float correctly_rounded(Function function, float input) noexcept {
    switch (function) {
        case Function::Exp:
            return cr_expf(input);
        case Function::Log:
            return cr_logf(input);
        case Function::Erf:
            return cr_erff(input);
        default:
            ::fixy::unreachable();
    }
}

double correctly_rounded(Function function, double input) noexcept {
    switch (function) {
        case Function::Exp:
            return cr_exp(input);
        case Function::Log:
            return cr_log(input);
        case Function::Erf:
            return cr_erf(input);
        default:
            ::fixy::unreachable();
    }
}

double error_in_ulp(Function function, ::fixy::Side<side::Input, double> input,
                    ::fixy::Side<side::Result, double> result) noexcept {
    // The precision of the exact value: 200 bits hold the result of a double
    // input far past the error that the sweep measures.
    mpfr_t argument;
    mpfr_t exact;
    mpfr_t difference;
    mpfr_init2(argument, 53);
    mpfr_init2(exact, 200);
    mpfr_init2(difference, 200);
    mpfr_set_d(argument, input.value(), MPFR_RNDN);
    switch (function) {
        case Function::Exp:
            mpfr_exp(exact, argument, MPFR_RNDN);
            break;
        case Function::Log:
            mpfr_log(exact, argument, MPFR_RNDN);
            break;
        case Function::Erf:
            mpfr_erf(exact, argument, MPFR_RNDN);
            break;
        default:
            ::fixy::unreachable();
    }
    double error = 0.0;
    if (mpfr_zero_p(exact) == 0) {
        mpfr_sub_d(difference, exact, result.value(), MPFR_RNDN);
        long last_place = mpfr_get_exp(exact) - 53;
        if (last_place < -1074) last_place = -1074;
        mpfr_mul_2si(difference, difference, -last_place, MPFR_RNDN);
        error = mpfr_get_d(difference, MPFR_RNDN);
    } else if (__builtin_bit_cast(std::uint64_t, result.value()) << 1 != 0) {
        error = ::fixy::infinity_of<double>;
    }
    mpfr_clear(argument);
    mpfr_clear(exact);
    mpfr_clear(difference);
    return error < 0.0 ? -error : error;
}

}  // namespace fp_sweep

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
