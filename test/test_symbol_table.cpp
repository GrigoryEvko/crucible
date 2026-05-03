// Tests for SymbolTable — per-symbol metadata (kind, hints, ranges).

#include <crucible/Ops.h>
#include <crucible/SymbolTable.h>
#include <crucible/safety/Reflected.h>

#include "test_assert.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

using namespace crucible;

static void test_add_assigns_monotonic_ids() {
    SymbolTable t;
    auto a = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    auto b = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    auto c = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(a.raw() == 0);
    assert(b.raw() == 1);
    assert(c.raw() == 2);
    assert(t.size() == 3);
    std::printf("  test_add_monotonic:             PASSED\n");
}

static void test_default_ranges_by_kind() {
    SymbolTable t;
    auto s = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    assert(t.lower(s) == 2);                         // specialize_zero_one default
    assert(t.upper(s) == SymbolTable::kIntPosInf);
    assert(t.is_backed(s));
    assert(!t.has_hint(s));

    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(t.lower(u) == SymbolTable::kIntNegInf);
    assert(t.upper(u) == SymbolTable::kIntPosInf);

    auto f = t.add(SymKind::UNBACKED_FLOAT, ExprFlags::IS_REAL, false);
    assert(!t.is_backed(f));
    // Float ranges are bitcast infinities; reinterpret and check.
    const double lo = std::bit_cast<double>(t.lower(f));
    const double hi = std::bit_cast<double>(t.upper(f));
    assert(std::isinf(lo) && lo < 0);
    assert(std::isinf(hi) && hi > 0);
    std::printf("  test_default_ranges:            PASSED\n");
}

static void test_hint_set_clears_sentinel() {
    SymbolTable t;
    auto s = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    assert(!t.has_hint(s));
    assert(t.hint(s) == SymbolTable::kNoHint);
    t.set_hint(s, 42);
    assert(t.has_hint(s));
    assert(t.hint(s) == 42);
    std::printf("  test_set_hint:                  PASSED\n");
}

static void test_set_hint_float_round_trip() {
    SymbolTable t;
    auto f = t.add(SymKind::FLOAT, ExprFlags::IS_REAL);
    t.set_hint_float(f, 3.14159);
    assert(t.has_hint(f));
    // Bit-cast round trip is exact — compare the raw bits, not the double.
    assert(std::bit_cast<uint64_t>(t.hint_float(f))
           == std::bit_cast<uint64_t>(3.14159));
    std::printf("  test_set_hint_float:            PASSED\n");
}

static void test_tighten_range_only_narrows() {
    SymbolTable t;
    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);

    // Narrowing bounds sticks.
    t.tighten_range(u, 10, 100);
    assert(t.lower(u) == 10);
    assert(t.upper(u) == 100);

    // Widening attempts are rejected: lower=5 ≤ existing 10, keep 10.
    // upper=1000 > existing 100, keep 100.
    t.tighten_range(u, 5, 1000);
    assert(t.lower(u) == 10);
    assert(t.upper(u) == 100);

    // Further narrowing does apply.
    t.tighten_range(u, 20, 80);
    assert(t.lower(u) == 20);
    assert(t.upper(u) == 80);
    std::printf("  test_tighten_range:             PASSED\n");
}

static void test_size_like_flag() {
    SymbolTable t;
    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(!t.is_size_like(u));
    t.set_size_like(u);
    assert(t.is_size_like(u));
    std::printf("  test_size_like:                 PASSED\n");
}

static void test_range_predicates() {
    SymbolTable t;
    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(!t.is_positive(u));
    assert(!t.is_nonnegative(u));
    assert(!t.range_contains(u, 0, 100));

    t.tighten_range(u, 1, 50);
    assert(t.is_positive(u));
    assert(t.is_nonnegative(u));
    assert(t.range_contains(u, 0, 100));
    assert(!t.range_contains(u, 10, 20));  // upper (50) > 20

    // Zero-inclusive
    auto z = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    t.tighten_range(z, 0, 10);
    assert(!t.is_positive(z));    // 0 is not > 0
    assert(t.is_nonnegative(z));  // 0 is >= 0
    std::printf("  test_range_predicates:          PASSED\n");
}

static void test_kind_roundtrip() {
    SymbolTable t;
    assert(t.kind(t.add(SymKind::SIZE,         0)) == SymKind::SIZE);
    assert(t.kind(t.add(SymKind::FLOAT,        0)) == SymKind::FLOAT);
    assert(t.kind(t.add(SymKind::UNBACKED_INT, 0)) == SymKind::UNBACKED_INT);
    assert(t.kind(t.add(SymKind::UNBACKED_FLOAT, 0)) == SymKind::UNBACKED_FLOAT);
    std::printf("  test_kind:                      PASSED\n");
}

// Audit-fill for #1031 (sym_flags uint8_t → safety::Bits<SymFlags>).
//
// Three properties the per-flag tests above don't catch on their own:
//
//   1. Multi-flag coexistence under the typed surface — IS_BACKED,
//      HAS_HINT, and IS_SIZE_LIKE must be independently set and
//      independently readable on the SAME entry, not just one at a
//      time.  Bits<E> uses underlying-OR; a wrong specialization
//      could collapse two flags into one bit.
//   2. The is_backed=false branch leaves IS_BACKED clear so
//      synthetic / Vessel-injected symbols do not claim provenance
//      they don't have.
//   3. Runtime layout sanity: sizeof(SymbolEntry) at the call site
//      still equals the header static_assert, AND offsetof(sym_flags)
//      lives at byte 25 (3 * int64_t + SymKind + Bits<SymFlags>),
//      which is what the recipe-registry / Cipher serializers
//      already encode.
//
// Bonus: exercises safety::reflected::bits_to_string<SymFlags> from
// #1089 — the same Bits<SymFlags> surface that any future Cipher /
// diagnostics dump would print.
static void test_sym_flags_bits_typed_surface() {
    namespace ref = crucible::safety::reflected;

    // ── (1) + (2) Multi-flag coexistence + is_backed=false branch ─
    //
    // Add BOTH symbols up front so subsequent push_back reallocations
    // cannot invalidate the references we read below.  Capturing
    // `entries_` references between mutations is a vector-realloc
    // trap (push_back grows O(n) at capacity overflow); the
    // SymbolTable surface itself is unchanged, but tests must survive
    // it.
    SymbolTable t;
    auto s = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);            // is_backed=true → IS_BACKED set
    auto f = t.add(SymKind::UNBACKED_FLOAT, ExprFlags::IS_REAL,
                   /*is_backed=*/false);                              // → IS_BACKED clear
    t.set_hint(s, 17);                                                // → HAS_HINT set
    t.set_size_like(s);                                               // → IS_SIZE_LIKE set

    // Public-API readback.
    assert(t.is_backed(s));
    assert(t.has_hint(s));
    assert(t.is_size_like(s));
    assert(!t.is_backed(f));

    // Direct typed-surface readback — fetch references AFTER all
    // mutations so they cannot dangle.
    const auto& e_s = t[s];
    assert(e_s.sym_flags.test(SymFlags::IS_BACKED));
    assert(e_s.sym_flags.test(SymFlags::HAS_HINT));
    assert(e_s.sym_flags.test(SymFlags::IS_SIZE_LIKE));
    assert(e_s.sym_flags.popcount() == 3);

    const auto& e_f = t[f];
    assert(!e_f.sym_flags.test(SymFlags::IS_BACKED));
    assert(e_f.sym_flags.popcount() == 0);

    // ── (3) Runtime layout sanity ─────────────────────────────────
    static_assert(sizeof(SymbolEntry) == 32);
    static_assert(offsetof(SymbolEntry, sym_flags) == 25);
    SymbolEntry probe{};
    assert(reinterpret_cast<const char*>(&probe.sym_flags) -
           reinterpret_cast<const char*>(&probe) == 25);

    // ── (4) Reflection-driven diagnostic surface ──────────────────
    // bits_to_string<SymFlags> over the multi-flag entry.  Output
    // order = enum declaration order: IS_SIZE_LIKE, HAS_HINT, IS_BACKED.
    char buf[64] = {};
    auto n = ref::bits_to_string<SymFlags>(e_s.sym_flags, buf, sizeof(buf));
    const std::string_view want = "IS_SIZE_LIKE|HAS_HINT|IS_BACKED";
    assert(std::string_view{buf} == want);
    assert(n == want.size());

    // Empty bits → empty string + zero needed.
    char empty_buf[16] = {};
    auto n_empty = ref::bits_to_string<SymFlags>(e_f.sym_flags,
                                                 empty_buf, sizeof(empty_buf));
    assert(n_empty == 0);
    assert(empty_buf[0] == '\0');

    std::printf("  test_sym_flags_bits_typed_surface: PASSED\n");
}

int main() {
    test_add_assigns_monotonic_ids();
    test_default_ranges_by_kind();
    test_hint_set_clears_sentinel();
    test_set_hint_float_round_trip();
    test_tighten_range_only_narrows();
    test_size_like_flag();
    test_range_predicates();
    test_kind_roundtrip();
    test_sym_flags_bits_typed_surface();
    std::printf("test_symbol_table: 9 groups, all passed\n");
    return 0;
}
