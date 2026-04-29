// Direct tests for Types.h — strong-ID and strong-hash newtype macros.
//
// Covers the TypeSafe axiom: strong IDs cannot be silently swapped,
// have deterministic layout, propagate noexcept, and compare via
// operator<=>.  These are the foundation of every public Crucible
// API surface.

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
    static_assert(sizeof(SlotId)  == 4);
    static_assert(sizeof(NodeId)  == 4);
    static_assert(sizeof(SymbolId) == 4);
    static_assert(sizeof(MetaIndex) == 4);
    static_assert(sizeof(SchemaHash) == 8);
    static_assert(sizeof(ShapeHash)  == 8);
    static_assert(sizeof(ScopeHash)  == 8);
    static_assert(sizeof(CallsiteHash) == 8);
    static_assert(sizeof(ContentHash)  == 8);
    static_assert(sizeof(MerkleHash)   == 8);

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

    // No implicit conversion from uint32_t — enforce at compile time.
    static_assert(!std::is_convertible_v<uint32_t, OpIndex>);
    static_assert(!std::is_convertible_v<OpIndex, uint32_t>);
    static_assert(std::is_constructible_v<OpIndex, uint32_t>);

    // Cross-type: no implicit conversion between strong IDs.
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
    // Default hash = 0, sentinel hash = UINT64_MAX, they're distinct.
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
    // Strong IDs' default and explicit ctors must be noexcept so
    // containing types propagate the noexcept guarantee through
    // default-construction chains.  This is the fix for the
    // -Wnoexcept warning cascade.
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
    // Per #129, element_size returns ElementBytes strong type — raw
    // integer literals don't implicitly compare; use ElementBytes{N}
    // or .raw() at the comparison site.
    assert(element_size(ScalarType::Bool)          == ElementBytes{1});
    assert(element_size(ScalarType::Byte)          == ElementBytes{1});
    assert(element_size(ScalarType::Half)          == ElementBytes{2});
    assert(element_size(ScalarType::BFloat16)      == ElementBytes{2});
    assert(element_size(ScalarType::Int)           == ElementBytes{4});
    assert(element_size(ScalarType::Float)         == ElementBytes{4});
    assert(element_size(ScalarType::Long)          == ElementBytes{8});
    assert(element_size(ScalarType::Double)        == ElementBytes{8});
    assert(element_size(ScalarType::ComplexFloat)  == ElementBytes{8});
    assert(element_size(ScalarType::ComplexDouble) == ElementBytes{16});
    assert(element_size(ScalarType::Float8_e4m3fn) == ElementBytes{1});
    assert(element_size(ScalarType::Undefined).is_zero());
    std::printf("  test_element_size:              PASSED\n");
}

static void test_bit_cast_round_trip() {
    // Underlying bit representation must be the raw integer value — so
    // bit_cast from/to the integer underlying type round-trips cleanly.
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
