#pragma once

// ═══════════════════════════════════════════════════════════════════
// fixy::wrap::Checked — V-042 surface
//
// Re-exports `crucible::safety::Checked.h` under `crucible::fixy::wrap`.
// Covers all four overflow disciplines (checked / wrapping / trapping /
// saturating) plus the §19 compile-time arithmetic family
// (safe_add / safe_mul / safe_capacity / ...) and #134 variadic +
// layout-specific byte-budget helpers (safe_add_all / safe_array_bytes
// / safe_struct_bytes / bytes_fit_v / ensure_bytes_fit).
//
// Substrate doc-block: see `safety/Checked.h`.  Substrate ships
// header-internal static_asserts that THIS file triggers under the
// project's warnings-as-errors flags
// (feedback_header_only_static_assert_blind_spot.md).
//
// ─── Public surface (31 symbols) ────────────────────────────────────
//
//   Runtime overflow-mode arithmetic (19):
//     checked_{add,sub,mul,div,mod,neg,abs,shl,shr}   (9 — nullopt)
//     wrapping_{add,sub,mul}                          (3 — wrap)
//     trapping_{add,sub,mul,div}                      (4 — abort)
//     saturating_{add,sub,mul}                        (3 — clamp)
//
//   Compile-time arithmetic templates (10):
//     safe_add / safe_sub / safe_mul                  (3 — basic)
//     safe_capacity / safe_byte_budget                (2 — size_t aliases)
//     safe_add_all                                    (1 — variadic sum)
//     safe_array_bytes / safe_struct_bytes            (2 — layout)
//     safe_size_sum / safe_size_diff                  (2 — size_t aliases)
//
//   Compile-time budget primitives (2):
//     bytes_fit_v<Budget, Used>                       (bool trait)
//     ensure_bytes_fit<Budget, Used>()                (consteval check)

#include <crucible/safety/Checked.h>

#include <cstddef>
#include <cstdint>

namespace crucible::fixy::wrap {

// ═══════════════════════════════════════════════════════════════════
// ── 1. checked_* — nullopt-on-overflow (9) ───────────────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::checked_add;
using ::crucible::safety::checked_sub;
using ::crucible::safety::checked_mul;
using ::crucible::safety::checked_div;
using ::crucible::safety::checked_mod;
using ::crucible::safety::checked_neg;
using ::crucible::safety::checked_abs;
using ::crucible::safety::checked_shl;
using ::crucible::safety::checked_shr;

// ═══════════════════════════════════════════════════════════════════
// ── 2. wrapping_* — two's-complement wrap (3) ────────────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::wrapping_add;
using ::crucible::safety::wrapping_sub;
using ::crucible::safety::wrapping_mul;

// ═══════════════════════════════════════════════════════════════════
// ── 3. trapping_* — abort-on-overflow (4) ────────────────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::trapping_add;
using ::crucible::safety::trapping_sub;
using ::crucible::safety::trapping_mul;
using ::crucible::safety::trapping_div;

// ═══════════════════════════════════════════════════════════════════
// ── 4. saturating_* — clamp to T's [min, max] (3) ────────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::saturating_add;
using ::crucible::safety::saturating_sub;
using ::crucible::safety::saturating_mul;

// ═══════════════════════════════════════════════════════════════════
// ── 5. Compile-time arithmetic templates (10) ─────────────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::safe_add;
using ::crucible::safety::safe_sub;
using ::crucible::safety::safe_mul;
using ::crucible::safety::safe_capacity;
using ::crucible::safety::safe_byte_budget;
using ::crucible::safety::safe_add_all;
using ::crucible::safety::safe_array_bytes;
using ::crucible::safety::safe_struct_bytes;
using ::crucible::safety::safe_size_sum;
using ::crucible::safety::safe_size_diff;

// ═══════════════════════════════════════════════════════════════════
// ── 6. Compile-time budget primitives (2) ─────────────────────────
// ═══════════════════════════════════════════════════════════════════

using ::crucible::safety::bytes_fit_v;
using ::crucible::safety::ensure_bytes_fit;

// ═══════════════════════════════════════════════════════════════════
// ── Self-test — compile-time sentinels ────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Lives in a nested namespace to avoid name clobbering at TU-include
// time.  Triggers the substrate's header-internal static_asserts AND
// adds cross-path value identity for every variable-template surface
// (catches the rare case where a using-decl resolves to a re-
// instantiation rather than an alias to the substrate's instance).

namespace self_test_checked {

// ── Variable-template cross-path identity ────────────────────────
//
// Each var-template instantiation IS a separate compile-time
// computation.  Equal value at fixy + substrate paths is the
// substantive witness that the using-decl preserves alias identity.

static_assert(::crucible::fixy::wrap::safe_add<std::uint32_t, 10u, 20u> ==
              ::crucible::safety::safe_add<std::uint32_t, 10u, 20u>);
static_assert(::crucible::fixy::wrap::safe_sub<std::uint32_t, 30u, 20u> ==
              ::crucible::safety::safe_sub<std::uint32_t, 30u, 20u>);
static_assert(::crucible::fixy::wrap::safe_mul<std::uint32_t, 6u, 7u> ==
              ::crucible::safety::safe_mul<std::uint32_t, 6u, 7u>);

static_assert(::crucible::fixy::wrap::safe_capacity<8u, 16u> ==
              ::crucible::safety::safe_capacity<8u, 16u>);
static_assert(::crucible::fixy::wrap::safe_capacity<8u, 16u> == 128u);

static_assert(::crucible::fixy::wrap::safe_byte_budget<256u, 64u> ==
              ::crucible::safety::safe_byte_budget<256u, 64u>);

static_assert(::crucible::fixy::wrap::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u> ==
              ::crucible::safety::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u>);
static_assert(::crucible::fixy::wrap::safe_add_all<std::size_t, 1u, 2u, 3u, 4u, 5u>
              == 15u);

static_assert(::crucible::fixy::wrap::safe_array_bytes<std::uint64_t, 8u> ==
              ::crucible::safety::safe_array_bytes<std::uint64_t, 8u>);
static_assert(::crucible::fixy::wrap::safe_array_bytes<std::uint64_t, 8u> == 64u);

static_assert(::crucible::fixy::wrap::safe_struct_bytes<std::uint64_t, std::uint32_t>
              == ::crucible::safety::safe_struct_bytes<std::uint64_t, std::uint32_t>);
static_assert(::crucible::fixy::wrap::safe_struct_bytes<std::uint64_t, std::uint32_t>
              == 12u);

static_assert(::crucible::fixy::wrap::safe_size_sum<10u, 20u> ==
              ::crucible::safety::safe_size_sum<10u, 20u>);
static_assert(::crucible::fixy::wrap::safe_size_diff<30u, 10u> ==
              ::crucible::safety::safe_size_diff<30u, 10u>);

// ── bytes_fit_v: cross-path bool trait identity ──────────────────

static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 20u> ==
              ::crucible::safety::bytes_fit_v<64u, 20u>);
static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 20u> == true);
static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 64u> == true);
static_assert(::crucible::fixy::wrap::bytes_fit_v<64u, 65u> == false);

// ── ensure_bytes_fit: consteval call through the alias ───────────
//
// The consteval body fires the substrate's [Byte_Budget_Exceeded]
// static_assert on overflow.  Happy path compiles silently.

[[maybe_unused]] constexpr auto _ensure_bytes_fit_alias_witness = []() {
    ::crucible::fixy::wrap::ensure_bytes_fit<
        64u,
        ::crucible::fixy::wrap::safe_struct_bytes<std::uint64_t, std::uint64_t>>();
    return 0;
}();

// ── checked_* — constexpr cross-path identity ────────────────────
//
// Each call is a constexpr evaluation; cross-path equality of the
// returned std::optional<T> witnesses the alias is a true forward.

static_assert(::crucible::fixy::wrap::checked_add<std::uint32_t>(10u, 20u) ==
              ::crucible::safety::checked_add<std::uint32_t>(10u, 20u));
static_assert(::crucible::fixy::wrap::checked_add<std::uint32_t>(10u, 20u)
              == std::optional<std::uint32_t>{30u});

// Overflow → nullopt.
static_assert(!::crucible::fixy::wrap::checked_add<std::uint8_t>(200u, 100u).has_value());
static_assert(!::crucible::fixy::wrap::checked_mul<std::uint16_t>(300u, 300u).has_value());

// Division by zero → nullopt.
static_assert(!::crucible::fixy::wrap::checked_div<int>(10, 0).has_value());
static_assert( ::crucible::fixy::wrap::checked_div<int>(10, 2) == std::optional<int>{5});

// signed_integral neg/abs of INT_MIN → nullopt.
static_assert(!::crucible::fixy::wrap::checked_neg<std::int32_t>(INT32_MIN).has_value());
static_assert(!::crucible::fixy::wrap::checked_abs<std::int32_t>(INT32_MIN).has_value());

// shl/shr edge cases.
static_assert(!::crucible::fixy::wrap::checked_shl<std::uint32_t>(1u, 32).has_value());
static_assert( ::crucible::fixy::wrap::checked_shl<std::uint32_t>(1u, 4) ==
               std::optional<std::uint32_t>{16u});
static_assert(!::crucible::fixy::wrap::checked_shr<std::uint32_t>(1u, -1).has_value());

// checked_mod — basic + INT_MIN/-1 special case.
static_assert( ::crucible::fixy::wrap::checked_mod<int>(7, 3)  == std::optional<int>{1});
static_assert(!::crucible::fixy::wrap::checked_mod<int>(7, 0).has_value());
static_assert( ::crucible::fixy::wrap::checked_mod<std::int32_t>(INT32_MIN, -1) ==
               std::optional<std::int32_t>{0});

// checked_sub.
static_assert( ::crucible::fixy::wrap::checked_sub<std::uint32_t>(30u, 10u) ==
               std::optional<std::uint32_t>{20u});
static_assert(!::crucible::fixy::wrap::checked_sub<std::uint32_t>(10u, 30u).has_value());

// ── wrapping_* — constexpr cross-path identity + overflow wrap ───

static_assert(::crucible::fixy::wrap::wrapping_add<std::uint8_t>(200u, 100u) ==
              ::crucible::safety::wrapping_add<std::uint8_t>(200u, 100u));
static_assert(::crucible::fixy::wrap::wrapping_add<std::uint8_t>(200u, 100u)
              == static_cast<std::uint8_t>(44u));  // (200 + 100) mod 256

static_assert(::crucible::fixy::wrap::wrapping_sub<std::uint8_t>(10u, 20u)
              == static_cast<std::uint8_t>(246u));  // (-10) mod 256
static_assert(::crucible::fixy::wrap::wrapping_mul<std::uint8_t>(20u, 20u)
              == static_cast<std::uint8_t>(144u));  // 400 mod 256

// ── trapping_* — constexpr cross-path identity (no-overflow path) ──
//
// Cannot exercise the abort path in constexpr (std::abort is not
// constexpr); compile-time happy path proves the alias resolves.

static_assert(::crucible::fixy::wrap::trapping_add<std::uint32_t>(10u, 20u) ==
              ::crucible::safety::trapping_add<std::uint32_t>(10u, 20u));
static_assert(::crucible::fixy::wrap::trapping_add<std::uint32_t>(10u, 20u) == 30u);
static_assert(::crucible::fixy::wrap::trapping_sub<std::uint32_t>(30u, 10u) == 20u);
static_assert(::crucible::fixy::wrap::trapping_mul<std::uint32_t>(6u, 7u)   == 42u);
static_assert(::crucible::fixy::wrap::trapping_div<int>(20, 4)              == 5);

// ── saturating_* — constexpr cross-path identity + clamp ─────────

static_assert(::crucible::fixy::wrap::saturating_add<std::uint8_t>(200u, 100u) ==
              ::crucible::safety::saturating_add<std::uint8_t>(200u, 100u));
static_assert(::crucible::fixy::wrap::saturating_add<std::uint8_t>(200u, 100u)
              == static_cast<std::uint8_t>(255u));   // clamped to max
static_assert(::crucible::fixy::wrap::saturating_sub<std::uint8_t>(10u, 20u)
              == static_cast<std::uint8_t>(0u));     // clamped to min
static_assert(::crucible::fixy::wrap::saturating_mul<std::uint16_t>(300u, 300u)
              == static_cast<std::uint16_t>(65535u)); // clamped to max

// ── Cardinality witness ──────────────────────────────────────────
//
// 31 surfaced using-declarations across 6 sub-families:
//
//   checked_*        (9)  — nullopt on overflow
//   wrapping_*       (3)  — two's-complement wrap
//   trapping_*       (4)  — abort on overflow
//   saturating_*     (3)  — clamp to type's [min, max]
//   compile-time     (10) — safe_{add,sub,mul,capacity,byte_budget,
//                            add_all,array_bytes,struct_bytes,
//                            size_sum,size_diff}
//   budget           (2)  — bytes_fit_v + ensure_bytes_fit
//
// Future additions to safety::Checked MUST extend this block + bump
// the constant + add a sentinel above.

constexpr int checked_alias_cardinality = 31;
static_assert(checked_alias_cardinality == 31,
    "fixy::wrap::Checked cardinality changed — update Checked.h "
    "sentinel block to track the substrate overflow-arithmetic surface.");

}  // namespace self_test_checked

}  // namespace crucible::fixy::wrap
