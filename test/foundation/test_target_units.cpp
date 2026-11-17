// Two units of one program, built for two targets, keep their own body of
// each function of the base whose source reads the vector width of the
// target, and of the out-of-line exact form of fp::fma, and their own object
// of each constant that the target sets.  The
// unit target_unit_baseline.cpp compiles for x86-64 and the unit
// target_unit_wide.cpp for x86-64-v4.  Each gives the addresses of its compile
// as constant data, so the checks of the addresses run no instruction of the
// wide unit.  If the two units shared one body, the link could keep the body
// of AVX-512 for the unit of the baseline target.  Only a processor of
// x86-64-v4 runs the sum of the wide unit, and the two sums have the same
// bits (THE LANES of foundation/core/Reduce.h).

#include "target_units.h"

#include <foundation/os/X86Level.h>

#include "../test_assert.h"

#include <cstddef>
#include <cstdint>

namespace {

using ::crucible::test::baseline_unit_bodies;
using ::crucible::test::wide_unit_bodies;

// The three bodies whose source reads the vector width, and the out-of-line
// exact form of fp::fma, are different functions in the two units.
void test_each_unit_keeps_its_bodies() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    bool const is_sum_by_tree_shared = baseline_unit_bodies.sum_by_tree_body == wide_unit_bodies.sum_by_tree_body;
    bool const is_sum_scan_shared = baseline_unit_bodies.sum_scan_body == wide_unit_bodies.sum_scan_body;
    bool const is_fixed_copy_shared = baseline_unit_bodies.fixed_copy_body == wide_unit_bodies.fixed_copy_body;
    bool const is_fma_exact_shared = baseline_unit_bodies.fma_exact_body == wide_unit_bodies.fma_exact_body;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(!is_sum_by_tree_shared);
    assert(!is_sum_scan_shared);
    assert(!is_fixed_copy_shared);
    assert(!is_fma_exact_shared);
}

// Each unit reads the vector width of its own target: 16 bytes for x86-64,
// and 64 bytes for x86-64-v4.
void test_each_unit_keeps_its_constants() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    std::size_t const baseline_vector = *baseline_unit_bodies.vector_bytes;
    std::size_t const wide_vector = *wide_unit_bodies.vector_bytes;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(baseline_vector == 16);
    assert(wide_vector == 64);
}

// The sum of each unit has the same bits.  The wide unit runs only on a
// processor of x86-64-v4.
[[nodiscard]] bool test_the_sums_agree() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the address of a body or an object tells which unit of the link holds it")
    std::uint32_t const baseline_bits = baseline_unit_bodies.sum_bits_body();
    bool const is_wide_host =
        ::foundation::os::x86_level_of(::foundation::os::read_x86_feature_words()) == ::foundation::os::X86Level::v4;
    std::uint32_t const wide_bits = is_wide_host ? wide_unit_bodies.sum_bits_body() : baseline_bits;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(wide_bits == baseline_bits);
    return is_wide_host;
}

}  // namespace

int main() {
    test_each_unit_keeps_its_bodies();
    test_each_unit_keeps_its_constants();
    if (test_the_sums_agree()) {
        ::crucible::test::pass("test_target_units: two units of two targets keep their own bodies, and their "
                               "sums agree\n");
    } else {
        ::crucible::test::pass("test_target_units: two units of two targets keep their own bodies.  The host has no "
                               "x86-64-v4, so the sum of the wide unit did not run\n");
    }
    return 0;
}
