#pragma once

// The body of each unit of test_target_units.  Each unit includes this
// header, so each unit compiles these functions for its own target.  The
// functions are in an unnamed namespace, so each unit keeps its own copy,
// and the link cannot replace it with the copy of the other unit.  The probed
// bodies and objects are not in the unnamed namespace: they belong to the
// base, and the test reads which unit holds each of them.

#include "target_units.h"

#include <foundation/Quarantine.h>
#include <foundation/Simd.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Scan.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {
namespace {

// The count of the run of the sum.  It is not a multiple of 16, so the sum
// takes blocks of vectors and a tail of the scalar tree.
inline constexpr std::size_t target_unit_run = 1000;

// The bits of the tree sum of a fixed run, by the body of this unit.  The run
// holds floats of random signs and of 24 magnitudes, so each association of
// the sum gives other bits.  A linear congruential step draws the run, so the
// unit calls no helper of a test header.
[[nodiscard]] std::uint32_t sum_bits_of_this_unit() noexcept {
    ::foundation::core::FixedArray<float, target_unit_run> run{};
    std::uint32_t state = 0x2545F491u;
    for (float& value : run) {
        state = state * 1664525u + 1013904223u;
        std::uint32_t const exponent = 115u + (state >> 27) % 24u;
        std::uint32_t const sign = (state >> 26) & 1u;
        value = ::foundation::core::bit_cast<float>((sign << 31) | (exponent << 23) | (state & 0x7FFFFFu));
    }
    float const sum = ::foundation::core::sum_by_tree(::foundation::core::View<float const>{run.view()});
    return ::foundation::core::bit_cast<std::uint32_t>(sum);
}

// The record of this unit, as a constant.  Each address comes from the compile
// of this unit, so two units that share one body give one address.
[[nodiscard]] consteval TargetUnitBodies bodies_of_this_unit() noexcept {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    return TargetUnitBodies{
        .sum_by_tree_body = &::foundation::core::sum_by_tree,
        .sum_scan_body =
            &::foundation::core::scan_inclusive_sums<float, TargetProbeBrand, ::foundation::core::dynamic_extent>,
        .fixed_copy_body = &::foundation::core::copy<std::uint8_t, 64>,
        .vector_bytes = &::foundation::simd::native_vector_bytes,
        .sum_bits_body = &sum_bits_of_this_unit,
    };
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

}  // namespace
}  // namespace crucible::test
