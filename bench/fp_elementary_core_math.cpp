// The door of bench_fp_elementary to the correctly rounded exp, log and erf of
// the pinned CORE-MATH (utils/pins.txt).  The library core_math_reference of
// bench/CMakeLists.txt compiles its six C files at -O2 -march=native.  Each
// call is a call of the library.

#include "fp_elementary_rows.h"

#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: CORE-MATH is a C library, a reference of the bench of fixy/fp")

extern "C" {
float cr_expf(float);
float cr_logf(float);
float cr_erff(float);
double cr_exp(double);
double cr_log(double);
double cr_erf(double);
}

namespace bench_fp_elementary::core_math {

namespace {

struct ExpDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return cr_exp(value); }
};
struct LogDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return cr_log(value); }
};
struct ErfDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return cr_erf(value); }
};
struct ExpFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return cr_expf(value); }
};
struct LogFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return cr_logf(value); }
};
struct ErfFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return cr_erff(value); }
};

}  // namespace

std::uint64_t xor_of(Function function, ::fixy::View<double const> inputs) noexcept {
    switch (function) {
        case Function::Exp:
            return xor_of_results(ExpDouble{}, inputs);
        case Function::Log:
            return xor_of_results(LogDouble{}, inputs);
        case Function::Erf:
            return xor_of_results(ErfDouble{}, inputs);
        default:
            ::fixy::unreachable();
    }
}

std::uint32_t xor_of(Function function, ::fixy::View<float const> inputs) noexcept {
    switch (function) {
        case Function::Exp:
            return xor_of_results(ExpFloat{}, inputs);
        case Function::Log:
            return xor_of_results(LogFloat{}, inputs);
        case Function::Erf:
            return xor_of_results(ErfFloat{}, inputs);
        default:
            ::fixy::unreachable();
    }
}

double chain_of(Function function, ::fixy::View<double const> inputs, double zero) noexcept {
    switch (function) {
        case Function::Exp:
            return chain_of_results(ExpDouble{}, inputs, zero);
        case Function::Log:
            return chain_of_results(LogDouble{}, inputs, zero);
        case Function::Erf:
            return chain_of_results(ErfDouble{}, inputs, zero);
        default:
            ::fixy::unreachable();
    }
}

float chain_of(Function function, ::fixy::View<float const> inputs, float zero) noexcept {
    switch (function) {
        case Function::Exp:
            return chain_of_results(ExpFloat{}, inputs, zero);
        case Function::Log:
            return chain_of_results(LogFloat{}, inputs, zero);
        case Function::Erf:
            return chain_of_results(ErfFloat{}, inputs, zero);
        default:
            ::fixy::unreachable();
    }
}

}  // namespace bench_fp_elementary::core_math

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
