#pragma once

// The bodies of the units of bench_core_fma (fma_bench_units.h).  Each unit
// includes this header, so each unit compiles them for its own target.  They
// are in an unnamed namespace, so each unit keeps its own copy.

#include <foundation/core/Float.h>
#include <foundation/core/Index.h>
#include <foundation/core/Region.h>

#include <cstddef>

namespace crucible::bench_fma {
namespace {

// The count of values in one triple: left, right and addend.
inline constexpr std::size_t kTriple = 3;

[[nodiscard]] double chain_of_fma_here(::foundation::core::View<double const> triples) noexcept {
    namespace core = ::foundation::core;
    double result = 0.0;
    for (core::View<double const, kTriple> const triple : triples.chunks<kTriple>()) {
        result = core::fp::fma(triple.at(core::index<0>()), triple.at(core::index<1>()), result);
    }
    return result;
}

[[nodiscard]] double sum_of_fma_here(::foundation::core::View<double const> triples) noexcept {
    namespace core = ::foundation::core;
    double sum = 0.0;
    for (core::View<double const, kTriple> const triple : triples.chunks<kTriple>()) {
        sum += core::fp::fma(triple.at(core::index<0>()), triple.at(core::index<1>()), triple.at(core::index<2>()));
    }
    return sum;
}

}  // namespace
}  // namespace crucible::bench_fma
