#include <crucible/Ops.h>
#include <crucible/SymbolTable.h>
#include <crucible/safety/_Reflected.h>

#include "test_assert.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

using namespace crucible;

static void test_add_assigns_monotonic_ids() {
    SymbolTable t;
    auto a = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    auto b = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    auto c = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    static_assert(std::is_same_v<decltype(a), InternalSymbolId>);
    static_assert(sizeof(InternalSymbolId) == sizeof(SymbolId));
    assert(a.value().raw() == 0);
    assert(b.value().raw() == 1);
    assert(c.value().raw() == 2);
    assert(t.size() == 3);
    std::printf("  test_add_monotonic:             PASSED\n");
}

static void test_default_ranges_by_kind() {
    SymbolTable t;
    auto s = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    assert(t.lower(s.value()) == 2);  // specialize_zero_one default
    assert(t.upper(s.value()) == SymbolTable::kIntPosInf);
    assert(t.is_backed(s.value()));
    assert(!t.has_hint(s.value()));

    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(t.lower(u.value()) == SymbolTable::kIntNegInf);
    assert(t.upper(u.value()) == SymbolTable::kIntPosInf);

    auto f = t.add(SymKind::UNBACKED_FLOAT, ExprFlags::IS_REAL, false);
    assert(!t.is_backed(f.value()));
    // A float range stores its infinities as the bit patterns of the
    // integer bounds, so reading them back needs a cast.
    const double lo = std::bit_cast<double>(t.lower(f.value()));
    const double hi = std::bit_cast<double>(t.upper(f.value()));
    assert(std::isinf(lo) && lo < 0);
    assert(std::isinf(hi) && hi > 0);
    std::printf("  test_default_ranges:            PASSED\n");
}

static void test_hint_set_clears_sentinel() {
    SymbolTable t;
    auto s = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);
    assert(!t.has_hint(s.value()));
    assert(t.hint(s.value()) == SymbolTable::kNoHint);
    t.set_hint(s.value(), 42);
    assert(t.has_hint(s.value()));
    assert(t.hint(s.value()) == 42);
    std::printf("  test_set_hint:                  PASSED\n");
}

static void test_set_hint_float_round_trip() {
    SymbolTable t;
    auto f = t.add(SymKind::FLOAT, ExprFlags::IS_REAL);
    t.set_hint_float(f.value(), 3.14159);
    assert(t.has_hint(f.value()));
    // The round trip is exact, so the raw bits are the right comparison
    // and an epsilon would be the wrong one.
    assert(std::bit_cast<uint64_t>(t.hint_float(f.value())) == std::bit_cast<uint64_t>(3.14159));
    std::printf("  test_set_hint_float:            PASSED\n");
}

static void test_tighten_range_only_narrows() {
    SymbolTable t;
    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);

    t.tighten_range(u.value(), 10, 100);
    assert(t.lower(u.value()) == 10);
    assert(t.upper(u.value()) == 100);

    // Each bound moves only inwards, so a lower of 5 and an upper of
    // 1000 both leave the existing range untouched.
    t.tighten_range(u.value(), 5, 1000);
    assert(t.lower(u.value()) == 10);
    assert(t.upper(u.value()) == 100);

    t.tighten_range(u.value(), 20, 80);
    assert(t.lower(u.value()) == 20);
    assert(t.upper(u.value()) == 80);
    std::printf("  test_tighten_range:             PASSED\n");
}

static void test_size_like_flag() {
    SymbolTable t;
    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(!t.is_size_like(u.value()));
    t.set_size_like(u.value());
    assert(t.is_size_like(u.value()));
    std::printf("  test_size_like:                 PASSED\n");
}

static void test_range_predicates() {
    SymbolTable t;
    auto u = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    assert(!t.is_positive(u.value()));
    assert(!t.is_nonnegative(u.value()));
    assert(!t.range_contains(u.value(), 0, 100));

    t.tighten_range(u.value(), 1, 50);
    assert(t.is_positive(u.value()));
    assert(t.is_nonnegative(u.value()));
    assert(t.range_contains(u.value(), 0, 100));
    assert(!t.range_contains(u.value(), 10, 20));  // upper (50) > 20

    auto z = t.add(SymKind::UNBACKED_INT, ExprFlags::IS_INTEGER);
    t.tighten_range(z.value(), 0, 10);
    assert(!t.is_positive(z.value()));  // 0 is not > 0
    assert(t.is_nonnegative(z.value()));  // 0 is >= 0
    std::printf("  test_range_predicates:          PASSED\n");
}

static void test_kind_roundtrip() {
    SymbolTable t;
    assert(t.kind(t.add(SymKind::SIZE, 0).value()) == SymKind::SIZE);
    assert(t.kind(t.add(SymKind::FLOAT, 0).value()) == SymKind::FLOAT);
    assert(t.kind(t.add(SymKind::UNBACKED_INT, 0).value()) == SymKind::UNBACKED_INT);
    assert(t.kind(t.add(SymKind::UNBACKED_FLOAT, 0).value()) == SymKind::UNBACKED_FLOAT);
    std::printf("  test_kind:                      PASSED\n");
}

// Three properties that the per-flag tests above cannot catch on their
// own:
//
//   1. Three flags set on one entry must stay independently readable.
//      The flag set ORs underlying values, so a wrong enumerator could
//      collapse two of them onto a single bit.
//   2. Passing is_backed as false must leave the backed flag clear, so
//      that a synthetic symbol does not claim provenance it lacks.
//   3. The flag byte sits at offset 25, which is what the serializers
//      already encode, and the entry is 32 bytes wide.
static void test_sym_flags_bits_typed_surface() {
    namespace ref = crucible::safety::reflected;

    // Both symbols are added before anything reads a reference, because
    // a later add can reallocate the entry storage and leave an earlier
    // reference dangling.
    SymbolTable t;
    auto s = t.add(SymKind::SIZE, ExprFlags::IS_INTEGER);  // backed
    auto f = t.add(SymKind::UNBACKED_FLOAT, ExprFlags::IS_REAL,
                   /*is_backed=*/false);  // not backed
    t.set_hint(s.value(), 17);
    t.set_size_like(s.value());

    assert(t.is_backed(s.value()));
    assert(t.has_hint(s.value()));
    assert(t.is_size_like(s.value()));
    assert(!t.is_backed(f.value()));

    const auto& e_s = t[s.value()];
    assert(e_s.sym_flags.test(SymFlags::IS_BACKED));
    assert(e_s.sym_flags.test(SymFlags::HAS_HINT));
    assert(e_s.sym_flags.test(SymFlags::IS_SIZE_LIKE));
    assert(e_s.sym_flags.popcount() == 3);

    const auto& e_f = t[f.value()];
    assert(!e_f.sym_flags.test(SymFlags::IS_BACKED));
    assert(e_f.sym_flags.popcount() == 0);

    static_assert(sizeof(SymbolEntry) == 32);
    static_assert(offsetof(SymbolEntry, sym_flags) == 25);
    SymbolEntry probe{};
    assert(std::bit_cast<std::uintptr_t>(&probe.sym_flags) - std::bit_cast<std::uintptr_t>(&probe) == 25);

    // The printed order follows enumerator declaration order, which is
    // what fixes the expected string below.
    char buf[64] = {};
    auto n = ref::bits_to_string<SymFlags>(e_s.sym_flags, buf, sizeof(buf));
    const std::string_view want = "IS_SIZE_LIKE|HAS_HINT|IS_BACKED";
    assert(std::string_view{buf} == want);
    assert(n == want.size());

    char empty_buf[16] = {};
    auto n_empty = ref::bits_to_string<SymFlags>(e_f.sym_flags, empty_buf, sizeof(empty_buf));
    assert(n_empty == 0);
    assert(empty_buf[0] == '\0');

    std::printf("  test_sym_flags_bits_typed_surface: PASSED\n");
}

[[gnu::cold]] int main() {
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
