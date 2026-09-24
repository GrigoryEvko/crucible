// Including the header from a translation unit is what forces its own
// static_asserts to be evaluated under the project warnings-as-errors
// flags.  The runtime witnesses cover each of the four overflow
// disciplines in turn.

#include <crucible/fixy/wrap/Checked.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace fw = ::crucible::fixy::wrap;

// These repeat claims the header already makes, from a consumer
// translation unit.  A build-configuration divergence that leaves a
// symbol visible inside the header and not outside it shows up here.

static_assert(fw::safe_capacity<8u, 16u> == 128u);
static_assert(fw::safe_array_bytes<std::uint64_t, 8u> == 64u);
static_assert(!fw::checked_add<std::uint8_t>(200u, 100u).has_value());

static void test_runtime_checked_arithmetic() {
    auto r1 = fw::checked_add<std::uint32_t>(10u, 20u);
    if (!r1.has_value() || *r1 != 30u) std::abort();

    auto r2 = fw::checked_sub<std::int32_t>(10, 30);
    if (!r2.has_value() || *r2 != -20) std::abort();

    auto r3 = fw::checked_mul<std::uint16_t>(200u, 100u);
    if (!r3.has_value() || *r3 != 20000u) std::abort();

    auto r4 = fw::checked_add<std::uint8_t>(200u, 100u);
    if (r4.has_value()) std::abort();

    auto r5 = fw::checked_mul<std::uint16_t>(300u, 300u);
    if (r5.has_value()) std::abort();

    auto r6 = fw::checked_add<std::int32_t>(INT32_MAX, 1);
    if (r6.has_value()) std::abort();
}

static void test_runtime_checked_guard_paths() {
    if (fw::checked_div<int>(10, 0).has_value()) std::abort();
    if (fw::checked_div<std::int32_t>(INT32_MIN, -1).has_value()) std::abort();
    auto rd = fw::checked_div<int>(20, 4);
    if (!rd.has_value() || *rd != 5) std::abort();

    // The remainder of INT_MIN by -1 is zero, where the quotient of the
    // same pair overflows.
    if (fw::checked_mod<int>(10, 0).has_value()) std::abort();
    auto rm = fw::checked_mod<std::int32_t>(INT32_MIN, -1);
    if (!rm.has_value() || *rm != 0) std::abort();

    if (fw::checked_neg<std::int32_t>(INT32_MIN).has_value()) std::abort();
    if (fw::checked_abs<std::int32_t>(INT32_MIN).has_value()) std::abort();
    auto ra = fw::checked_abs<std::int32_t>(-42);
    if (!ra.has_value() || *ra != 42) std::abort();
}

static void test_runtime_checked_shifts() {
    if (fw::checked_shl<std::uint32_t>(1u, 32).has_value()) std::abort();
    if (fw::checked_shl<std::uint32_t>(1u, -1).has_value()) std::abort();

    // Shifting a negative signed value left is refused rather than left
    // to the standard's discretion.
    if (fw::checked_shl<std::int32_t>(-1, 1).has_value()) std::abort();

    auto rs = fw::checked_shl<std::uint32_t>(1u, 4);
    if (!rs.has_value() || *rs != 16u) std::abort();

    if (fw::checked_shr<std::uint32_t>(8u, 32).has_value()) std::abort();
    auto rr = fw::checked_shr<std::uint32_t>(16u, 2);
    if (!rr.has_value() || *rr != 4u) std::abort();
}

static void test_runtime_wrapping_arithmetic() {
    // 300 wraps modulo 256 to 44.
    if (fw::wrapping_add<std::uint8_t>(200u, 100u) != 44u) std::abort();
    // -10 wraps modulo 256 to 246.
    if (fw::wrapping_sub<std::uint8_t>(10u, 20u) != 246u) std::abort();
    // 400 wraps modulo 256 to 144.
    if (fw::wrapping_mul<std::uint8_t>(20u, 20u) != 144u) std::abort();

    if (fw::wrapping_add<std::uint32_t>(10u, 20u) != 30u) std::abort();

    // The signed wrap is well defined here because the implementation
    // goes through the overflow builtins, which bare signed addition
    // does not.
    if (fw::wrapping_add<std::int32_t>(INT32_MAX, 1) != INT32_MIN) std::abort();
}

// Overflow aborts the process, so only the happy path is exercisable
// from inside a running test.
static void test_runtime_trapping_arithmetic() {
    if (fw::trapping_add<std::uint32_t>(10u, 20u) != 30u) std::abort();
    if (fw::trapping_sub<std::uint32_t>(30u, 10u) != 20u) std::abort();
    if (fw::trapping_mul<std::uint32_t>(6u, 7u) != 42u) std::abort();
    if (fw::trapping_div<int>(20, 4) != 5) std::abort();
}

static void test_runtime_saturating_arithmetic() {
    // 300 clamps to the byte maximum of 255.
    if (fw::saturating_add<std::uint8_t>(200u, 100u) != 255u) std::abort();
    // -10 clamps to the unsigned minimum of 0.
    if (fw::saturating_sub<std::uint8_t>(10u, 20u) != 0u) std::abort();
    // 90000 clamps to the 16-bit maximum of 65535.
    if (fw::saturating_mul<std::uint16_t>(300u, 300u) != 65535u) std::abort();

    if (fw::saturating_add<std::uint32_t>(10u, 20u) != 30u) std::abort();

    if (fw::saturating_add<std::int32_t>(INT32_MAX, 1) != INT32_MAX) std::abort();
    if (fw::saturating_sub<std::int32_t>(INT32_MIN, 1) != INT32_MIN) std::abort();
}

static void test_runtime_compile_time_arithmetic() {
    // Each of these is an inline constexpr global, so reading one at
    // runtime witnesses that the alias forwarded to the right variable.
    if (fw::safe_add<std::uint32_t, 10u, 20u> != 30u) std::abort();
    if (fw::safe_capacity<8u, 16u> != 128u) std::abort();
    if (fw::safe_byte_budget<256u, 64u> != 16384u) std::abort();
    if (fw::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u> != 15u) std::abort();
    if (fw::safe_array_bytes<std::uint64_t, 8u> != 64u) std::abort();
    if (fw::safe_struct_bytes<std::uint64_t, std::uint32_t> != 12u) std::abort();
    if (fw::safe_size_sum<10u, 20u> != 30u) std::abort();
    if (fw::safe_size_diff<30u, 10u> != 20u) std::abort();
}

static void test_runtime_budget_primitives() {
    if (!fw::bytes_fit_v<64u, 20u>) std::abort();
    if (!fw::bytes_fit_v<64u, 64u>) std::abort();  // exact fit
    if (fw::bytes_fit_v<64u, 65u>) std::abort();  // over by 1

    // ensure_bytes_fit is consteval, so the call inside the lambda is
    // the real check and the branch below is only a readback.
    constexpr auto witness = []() consteval {
        fw::ensure_bytes_fit<64u, fw::safe_struct_bytes<std::uint64_t, std::uint64_t>>();
        return true;
    }();
    if (!witness) std::abort();
}

int main() {
    test_runtime_checked_arithmetic();
    test_runtime_checked_guard_paths();
    test_runtime_checked_shifts();
    test_runtime_wrapping_arithmetic();
    test_runtime_trapping_arithmetic();
    test_runtime_saturating_arithmetic();
    test_runtime_compile_time_arithmetic();
    test_runtime_budget_primitives();
    std::printf("test_fixy_wrap_checked: 8/8 runtime witnesses passed\n");
    return 0;
}
