// ── test_fixy_wrap_checked — V-042 sentinel TU
//
// Forces evaluation of the header-internal static_asserts in
// `include/crucible/fixy/wrap/Checked.h` under the project's warnings-
// as-errors flags (per feedback_header_only_static_assert_blind_spot.md),
// and adds runtime witnesses across each of the four overflow disciplines
// (checked / wrapping / trapping / saturating).

#include <crucible/fixy/wrap/Checked.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace fw = ::crucible::fixy::wrap;

// ═══════════════════════════════════════════════════════════════════
// ── Compile-time witnesses (re-state at TU scope) ────────────────
// ═══════════════════════════════════════════════════════════════════
//
// The substrate's identity static_asserts already live in the umbrella.
// We add a few more here to witness reach from outside the safety/
// tree — drift from header-internal to consumer-visible would catch
// a build-config divergence.

static_assert(fw::safe_capacity<8u, 16u> == 128u);
static_assert(fw::safe_array_bytes<std::uint64_t, 8u> == 64u);
static_assert(!fw::checked_add<std::uint8_t>(200u, 100u).has_value());

// ═══════════════════════════════════════════════════════════════════
// ── Runtime witnesses ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// 1. checked_add / sub / mul — no overflow + overflow paths.
static void test_runtime_checked_arithmetic() {
    // No overflow → engaged optional with expected value.
    auto r1 = fw::checked_add<std::uint32_t>(10u, 20u);
    if (!r1.has_value() || *r1 != 30u) std::abort();

    auto r2 = fw::checked_sub<std::int32_t>(10, 30);
    if (!r2.has_value() || *r2 != -20) std::abort();

    auto r3 = fw::checked_mul<std::uint16_t>(200u, 100u);
    if (!r3.has_value() || *r3 != 20000u) std::abort();

    // Overflow → nullopt.
    auto r4 = fw::checked_add<std::uint8_t>(200u, 100u);
    if (r4.has_value()) std::abort();

    auto r5 = fw::checked_mul<std::uint16_t>(300u, 300u);
    if (r5.has_value()) std::abort();

    // Signed overflow — INT_MAX + 1.
    auto r6 = fw::checked_add<std::int32_t>(INT32_MAX, 1);
    if (r6.has_value()) std::abort();
}

// 2. checked_div / mod / neg / abs — guard cases.
static void test_runtime_checked_guard_paths() {
    // div: by-zero, INT_MIN/-1.
    if (fw::checked_div<int>(10, 0).has_value()) std::abort();
    if (fw::checked_div<std::int32_t>(INT32_MIN, -1).has_value()) std::abort();
    auto rd = fw::checked_div<int>(20, 4);
    if (!rd.has_value() || *rd != 5) std::abort();

    // mod: by-zero, INT_MIN/-1 returns 0.
    if (fw::checked_mod<int>(10, 0).has_value()) std::abort();
    auto rm = fw::checked_mod<std::int32_t>(INT32_MIN, -1);
    if (!rm.has_value() || *rm != 0) std::abort();

    // neg/abs of INT_MIN — overflow.
    if (fw::checked_neg<std::int32_t>(INT32_MIN).has_value()) std::abort();
    if (fw::checked_abs<std::int32_t>(INT32_MIN).has_value()) std::abort();
    auto ra = fw::checked_abs<std::int32_t>(-42);
    if (!ra.has_value() || *ra != 42) std::abort();
}

// 3. checked_shl / shr — invalid shifts.
static void test_runtime_checked_shifts() {
    // shl by ≥ bit-width.
    if (fw::checked_shl<std::uint32_t>(1u, 32).has_value()) std::abort();
    if (fw::checked_shl<std::uint32_t>(1u, -1).has_value()) std::abort();

    // shl of negative signed — UB-prone, rejected.
    if (fw::checked_shl<std::int32_t>(-1, 1).has_value()) std::abort();

    // Happy path.
    auto rs = fw::checked_shl<std::uint32_t>(1u, 4);
    if (!rs.has_value() || *rs != 16u) std::abort();

    // shr.
    if (fw::checked_shr<std::uint32_t>(8u, 32).has_value()) std::abort();
    auto rr = fw::checked_shr<std::uint32_t>(16u, 2);
    if (!rr.has_value() || *rr != 4u) std::abort();
}

// 4. wrapping_* — two's-complement wrap.
static void test_runtime_wrapping_arithmetic() {
    // 200 + 100 = 300, wraps mod 256 → 44.
    if (fw::wrapping_add<std::uint8_t>(200u, 100u) != 44u) std::abort();
    // 10 - 20 = -10, wraps mod 256 → 246.
    if (fw::wrapping_sub<std::uint8_t>(10u, 20u) != 246u) std::abort();
    // 20 * 20 = 400, wraps mod 256 → 144.
    if (fw::wrapping_mul<std::uint8_t>(20u, 20u) != 144u) std::abort();

    // Happy path — no wrap.
    if (fw::wrapping_add<std::uint32_t>(10u, 20u) != 30u) std::abort();

    // Signed wrap — INT_MAX + 1 → INT_MIN (well-defined under
    // __builtin_*_overflow, NOT under bare signed addition).
    if (fw::wrapping_add<std::int32_t>(INT32_MAX, 1) != INT32_MIN) std::abort();
}

// 5. trapping_* — happy path only (cannot exercise abort in a test).
static void test_runtime_trapping_arithmetic() {
    if (fw::trapping_add<std::uint32_t>(10u, 20u) != 30u) std::abort();
    if (fw::trapping_sub<std::uint32_t>(30u, 10u) != 20u) std::abort();
    if (fw::trapping_mul<std::uint32_t>(6u, 7u)   != 42u) std::abort();
    if (fw::trapping_div<int>(20, 4)              != 5)   std::abort();
    // Note: the abort path on overflow is exercised by neg-compile /
    // death-test harness elsewhere — this TU only proves the alias
    // resolves and the happy path returns the expected value.
}

// 6. saturating_* — clamp to type's [min, max].
static void test_runtime_saturating_arithmetic() {
    // u8: 200 + 100 = 300, clamps to 255.
    if (fw::saturating_add<std::uint8_t>(200u, 100u) != 255u) std::abort();
    // u8: 10 - 20 = -10, clamps to 0.
    if (fw::saturating_sub<std::uint8_t>(10u, 20u) != 0u)    std::abort();
    // u16: 300 * 300 = 90000, clamps to 65535.
    if (fw::saturating_mul<std::uint16_t>(300u, 300u) != 65535u) std::abort();

    // No overflow — passes through.
    if (fw::saturating_add<std::uint32_t>(10u, 20u) != 30u) std::abort();

    // Signed clamp: INT_MAX + 1 clamps to INT_MAX.
    if (fw::saturating_add<std::int32_t>(INT32_MAX, 1) != INT32_MAX) std::abort();
    // INT_MIN - 1 clamps to INT_MIN.
    if (fw::saturating_sub<std::int32_t>(INT32_MIN, 1) != INT32_MIN) std::abort();
}

// 7. Compile-time arithmetic — readback at runtime through alias.
static void test_runtime_compile_time_arithmetic() {
    // Variable-template instantiations are inline constexpr globals;
    // reading them at runtime witnesses the alias forwarded correctly.
    if (fw::safe_add<std::uint32_t, 10u, 20u> != 30u) std::abort();
    if (fw::safe_capacity<8u, 16u> != 128u) std::abort();
    if (fw::safe_byte_budget<256u, 64u> != 16384u) std::abort();
    if (fw::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u> != 15u) std::abort();
    if (fw::safe_array_bytes<std::uint64_t, 8u> != 64u) std::abort();
    if (fw::safe_struct_bytes<std::uint64_t, std::uint32_t> != 12u) std::abort();
    if (fw::safe_size_sum<10u, 20u> != 30u) std::abort();
    if (fw::safe_size_diff<30u, 10u> != 20u) std::abort();
}

// 8. bytes_fit_v + ensure_bytes_fit — budget primitives.
static void test_runtime_budget_primitives() {
    // bytes_fit_v: bool trait.
    if (!fw::bytes_fit_v<64u, 20u>) std::abort();
    if (!fw::bytes_fit_v<64u, 64u>) std::abort();  // exact fit
    if ( fw::bytes_fit_v<64u, 65u>) std::abort();  // over by 1

    // ensure_bytes_fit is consteval — the call here is a compile-time
    // check.  Runtime branch is just a sanity readback.
    constexpr auto witness = []() consteval {
        fw::ensure_bytes_fit<
            64u,
            fw::safe_struct_bytes<std::uint64_t, std::uint64_t>>();
        return true;
    }();
    if (!witness) std::abort();
}

// ═══════════════════════════════════════════════════════════════════
// ── Driver ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

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
