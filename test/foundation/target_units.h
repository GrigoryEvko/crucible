#pragma once

// The probes of the two units of test_target_units.  The build compiles one
// unit for the baseline target of x86-64 and one unit for the target of
// x86-64-v4, and links the two into one program.  Each unit gives a constant
// record of the addresses of the probed bodies and objects of the base, as
// its own compile sees them.  The record is constant data, so a read of it
// runs no instruction of the unit.

#include <foundation/Quarantine.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")

// The addresses that one unit gives:
//   - tree_sum_body, sum_scan_body, fixed_copy_body: three bodies whose source
//     reads the vector width of the target
//   - vector_bytes: the object of the vector width that the target sets
//   - sum_bits_body: the bits of the tree sum of the run of the test, by the
//     body of the unit.  The unit of x86-64-v4 runs only on such a processor.
struct TargetUnitBodies {
    float (*tree_sum_body)(::foundation::core::View<float const>) noexcept = nullptr;
    ::foundation::core::Result<::foundation::core::Unit, ::foundation::core::LengthMismatch> (*sum_scan_body)(
        ::foundation::core::View<float const>, ::foundation::core::View<float>) noexcept = nullptr;
    void (*fixed_copy_body)(::foundation::core::View<std::uint8_t, 64>,
                            ::foundation::core::View<std::uint8_t const, 64>) noexcept = nullptr;
    std::size_t const* vector_bytes = nullptr;
    std::uint32_t (*sum_bits_body)() noexcept = nullptr;
};

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The record of the unit of the baseline target, and of the unit of
// x86-64-v4.
extern TargetUnitBodies const baseline_unit_bodies;
extern TargetUnitBodies const wide_unit_bodies;

}  // namespace crucible::test
