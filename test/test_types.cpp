#include <crucible/Types.h>

#include "test_assert.h"

#include <bit>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using namespace crucible;

static void test_layout_and_trivial_relocatability() {
    static_assert(sizeof(OpIndex) == 4);
    static_assert(sizeof(SlotId) == 4);
    static_assert(sizeof(NodeId) == 4);
    static_assert(sizeof(SymbolId) == 4);
    static_assert(sizeof(MetaIndex) == 4);
    static_assert(sizeof(SchemaHash) == 8);
    static_assert(sizeof(ShapeHash) == 8);
    static_assert(sizeof(ScopeHash) == 8);
    static_assert(sizeof(CallsiteHash) == 8);
    static_assert(sizeof(ContentHash) == 8);
    static_assert(sizeof(MerkleHash) == 8);

    static_assert(std::is_trivially_copyable_v<OpIndex>);
    static_assert(std::is_trivially_copyable_v<SchemaHash>);
    static_assert(std::is_standard_layout_v<OpIndex>);
    static_assert(std::is_standard_layout_v<SchemaHash>);
    std::printf("  test_layout:                    PASSED\n");
}

static void test_default_ctor_is_sentinel() {
    OpIndex o{};
    assert(!o.is_valid());
    assert(o.raw() == UINT32_MAX);
    assert(!static_cast<bool>(o));
    assert(o == OpIndex::none());

    SchemaHash h{};
    assert(!static_cast<bool>(h));
    assert(h.raw() == 0);

    std::printf("  test_default_sentinel:          PASSED\n");
}

static void test_explicit_construction() {
    OpIndex o{42};
    assert(o.raw() == 42);
    assert(o.is_valid());
    assert(static_cast<bool>(o));

    static_assert(!std::is_convertible_v<uint32_t, OpIndex>);
    static_assert(!std::is_convertible_v<OpIndex, uint32_t>);
    static_assert(std::is_constructible_v<OpIndex, uint32_t>);

    static_assert(!std::is_convertible_v<OpIndex, SlotId>);
    static_assert(!std::is_convertible_v<SlotId, OpIndex>);
    static_assert(!std::is_convertible_v<SchemaHash, ShapeHash>);

    std::printf("  test_explicit_construction:     PASSED\n");
}

static void test_three_way_compare() {
    OpIndex a{1}, b{2}, a2{1};
    assert(a < b);
    assert(b > a);
    assert(a == a2);
    assert(a != b);
    assert((a <=> b) == std::strong_ordering::less);
    assert((a <=> a2) == std::strong_ordering::equal);
    std::printf("  test_compare:                   PASSED\n");
}

static void test_hash_sentinel_distinct_from_default() {
    SchemaHash def{};
    SchemaHash sent = SchemaHash::sentinel();
    assert(def != sent);
    assert(!def.is_sentinel());
    assert(sent.is_sentinel());
    assert(def.raw() == 0);
    assert(sent.raw() == UINT64_MAX);
    std::printf("  test_hash_sentinel:             PASSED\n");
}

static void test_noexcept_ctors_propagate() {
    // Both constructors must be noexcept, or every containing type loses
    // its own noexcept guarantee through the default-construction chain.
    static_assert(std::is_nothrow_default_constructible_v<OpIndex>);
    static_assert(std::is_nothrow_default_constructible_v<SchemaHash>);
    static_assert(noexcept(OpIndex{}));
    static_assert(noexcept(OpIndex{42u}));
    static_assert(noexcept(SchemaHash{}));
    static_assert(noexcept(SchemaHash{0xDEADBEEFULL}));
    static_assert(noexcept(OpIndex::none()));
    static_assert(noexcept(SchemaHash::sentinel()));
    std::printf("  test_noexcept:                  PASSED\n");
}

static void test_scalar_type_element_sizes() {
    // element_size returns a strong type, so a bare integer literal does not
    // compare against it. The expected sizes are written as ElementBytes.
    assert(element_size(ScalarType::Bool) == ElementBytes{1});
    assert(element_size(ScalarType::Byte) == ElementBytes{1});
    assert(element_size(ScalarType::Half) == ElementBytes{2});
    assert(element_size(ScalarType::BFloat16) == ElementBytes{2});
    assert(element_size(ScalarType::Int) == ElementBytes{4});
    assert(element_size(ScalarType::Float) == ElementBytes{4});
    assert(element_size(ScalarType::Long) == ElementBytes{8});
    assert(element_size(ScalarType::Double) == ElementBytes{8});
    assert(element_size(ScalarType::ComplexFloat) == ElementBytes{8});
    assert(element_size(ScalarType::ComplexDouble) == ElementBytes{16});
    assert(element_size(ScalarType::Float8_e4m3fn) == ElementBytes{1});
    assert(element_size(ScalarType::Undefined).is_zero());
    std::printf("  test_element_size:              PASSED\n");
}

static void test_bit_cast_round_trip() {
    // The representation is exactly the wrapped integer, so a bit_cast to
    // that integer and back is the identity.
    OpIndex in{0x1234'5678u};
    auto raw = std::bit_cast<uint32_t>(in);
    assert(raw == 0x1234'5678u);
    auto back = std::bit_cast<OpIndex>(raw);
    assert(back == in);

    SchemaHash h_in{0xDEAD'BEEF'CAFE'BABEULL};
    auto h_raw = std::bit_cast<uint64_t>(h_in);
    assert(h_raw == 0xDEAD'BEEF'CAFE'BABEULL);
    std::printf("  test_bit_cast:                  PASSED\n");
}

int main() {
    test_layout_and_trivial_relocatability();
    test_default_ctor_is_sentinel();
    test_explicit_construction();
    test_three_way_compare();
    test_hash_sentinel_distinct_from_default();
    test_noexcept_ctors_propagate();
    test_scalar_type_element_sizes();
    test_bit_cast_round_trip();
    std::printf("test_types: 8 groups, all passed\n");
    return 0;
}
