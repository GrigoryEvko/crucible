// The door of bench_fp_elementary to exp, log and erf of the C library of the
// host (glibc libm).  On x86-64 glibc picks a body of each function at load
// time, by the features of the core: the bodies with fma on a core of
// x86-64-v3 or above.  Each call is a call of the library.

#include "fp_elementary_rows.h"

#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the reference rows call exp, log and erf of the C library of the host")

#include <cmath>

namespace bench_fp_elementary::glibc {

namespace {

struct ExpDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return std::exp(value); }
};
struct LogDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return std::log(value); }
};
struct ErfDouble {
    [[nodiscard]] double operator()(double value) const noexcept { return std::erf(value); }
};
struct ExpFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return std::exp(value); }
};
struct LogFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return std::log(value); }
};
struct ErfFloat {
    [[nodiscard]] float operator()(float value) const noexcept { return std::erf(value); }
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

}  // namespace bench_fp_elementary::glibc

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
