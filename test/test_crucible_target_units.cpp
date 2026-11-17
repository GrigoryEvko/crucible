// Two units of one program, built for two targets, keep their own body of
// each function of the product headers whose source reads the vector width
// of the target: crucible/SwissTable.h, crucible/PhiloxSimd.h,
// crucible/cntp/Fec.h and crucible/cntp/Fountain.h.  The unit
// crucible_target_unit_baseline.cpp compiles for x86-64-v3 and the unit
// crucible_target_unit_wide.cpp for x86-64-v4, the two shipped levels.  Each
// gives a record of its compile as constant data, so the checks of the
// records run no instruction of the wide unit.  If the two units shared one
// body, the link could keep the body of AVX-512 for the unit of x86-64-v3.
// Only a processor of x86-64-v4 runs the kernels of the wide unit, and the
// kernels of the two units give the same bits.

#include "crucible_target_units.h"

#include "test_assert.h"

#include <cstdint>

namespace {

using ::crucible::test::baseline_crucible_unit;
using ::crucible::test::wide_crucible_unit;

// Each unit finds the probed names of each header in its own target
// namespace.
void test_each_header_is_in_the_target_namespace() {
    std::uint64_t const each_home = ::crucible::test::swiss_home_bit | ::crucible::test::philox_home_bit
                                  | ::crucible::test::fec_home_bit | ::crucible::test::fountain_home_bit;
    assert(baseline_crucible_unit.homes == each_home);
    assert(wide_crucible_unit.homes == each_home);
}

// The kernel of the codec is a different function in the two units.
void test_each_unit_keeps_its_kernel() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body tells which unit of the link holds it")
    bool const is_mul_xor_shared = baseline_crucible_unit.mul_xor_body == wide_crucible_unit.mul_xor_body;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(!is_mul_xor_shared);
}

// The kernels of each unit give the same bits.  The wide unit runs only on a
// processor of x86-64-v4.
[[nodiscard]] bool test_the_kernels_agree() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body tells which unit of the link holds it")
    std::uint64_t const baseline_bits = baseline_crucible_unit.kernel_bits_body();
    bool const is_wide_host = __builtin_cpu_supports("x86-64-v4");
    std::uint64_t const wide_bits = is_wide_host ? wide_crucible_unit.kernel_bits_body() : baseline_bits;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(wide_bits == baseline_bits);
    return is_wide_host;
}

}  // namespace

int main() {
    test_each_header_is_in_the_target_namespace();
    test_each_unit_keeps_its_kernel();
    if (test_the_kernels_agree()) {
        ::crucible::test::pass("test_crucible_target_units: two units of two targets keep their own bodies, and "
                               "their kernels agree\n");
    } else {
        ::crucible::test::pass("test_crucible_target_units: two units of two targets keep their own bodies.  The "
                               "host has no x86-64-v4, so the kernels of the wide unit did not run\n");
    }
    return 0;
}
