// The door of bench_fp_elementary to the scalar exp, log and erf of the pinned
// SLEEF (utils/pins.txt), in the accuracy class of 1.0 ulp (the suffix u10).
// The library sleef_reference of bench/CMakeLists.txt comes from the CMake of
// SLEEF.  Each call is a call of the library.  The unit declares the six
// functions as sleef.h of SLEEF 3.9.0 declares them, so the unit needs no
// include directory in the build directory.

#include "fp_elementary_rows.h"

#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: SLEEF is a C library, a reference of the bench of fixy/fp")

extern "C" {
double Sleef_exp_u10(double);
double Sleef_log_u10(double);
double Sleef_erf_u10(double);
float Sleef_expf_u10(float);
float Sleef_logf_u10(float);
float Sleef_erff_u10(float);
}

namespace bench_fp_elementary::sleef {

namespace {

struct ExpDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return Sleef_exp_u10(value); }
};
struct LogDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return Sleef_log_u10(value); }
};
struct ErfDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return Sleef_erf_u10(value); }
};
struct ExpFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return Sleef_expf_u10(value); }
};
struct LogFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return Sleef_logf_u10(value); }
};
struct ErfFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return Sleef_erff_u10(value); }
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

}  // namespace bench_fp_elementary::sleef

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
