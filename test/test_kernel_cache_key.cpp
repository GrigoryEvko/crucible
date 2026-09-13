#include <crucible/Types.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using namespace crucible;

static void test_layout_invariants() {
    static_assert(sizeof(KernelCacheKey) == 16);
    static_assert(alignof(KernelCacheKey) == 8);
    static_assert(std::is_trivially_copyable_v<KernelCacheKey>);
    static_assert(std::is_standard_layout_v<KernelCacheKey>);

    static_assert(offsetof(KernelCacheKey, content_hash) == 0);
    static_assert(offsetof(KernelCacheKey, row_hash) == 8);

    std::printf("  test_layout_invariants:         PASSED\n");
}

static void test_default_is_zero() {
    KernelCacheKey k{};
    assert(k.is_zero());
    assert(!k.is_sentinel());
    assert(!static_cast<bool>(k.content_hash));
    assert(!static_cast<bool>(k.row_hash));
    assert(k.content_hash.raw() == 0);
    assert(k.row_hash.raw() == 0);

    static_assert(noexcept(KernelCacheKey{}));
    static_assert(std::is_nothrow_default_constructible_v<KernelCacheKey>);

    std::printf("  test_default_is_zero:           PASSED\n");
}

// UINT64_MAX is reserved as the empty-slot marker.  A real avalanche
// hash reaches it with probability near zero.
static void test_sentinel_state() {
    constexpr KernelCacheKey s = KernelCacheKey::sentinel();
    static_assert(s.is_sentinel());
    static_assert(!s.is_zero());
    static_assert(s.content_hash.is_sentinel());
    static_assert(s.row_hash.is_sentinel());
    static_assert(s.content_hash.raw() == UINT64_MAX);
    static_assert(s.row_hash.raw() == UINT64_MAX);

    constexpr KernelCacheKey d{};
    static_assert(s != d);
    static_assert(d != s);

    std::printf("  test_sentinel_state:            PASSED\n");
}

// A pure-row kernel and an IO-row kernel can share a region
// structurally, yet they must not share a cache slot.  Sharing hands
// the reader a pure result when only an IO result was computed.
static void test_per_axis_distinctness() {
    constexpr ContentHash content_a{0xC0FFEEBA'1234'5678ULL};
    constexpr ContentHash content_b{0xDEAD'BEEF'CAFE'BABEULL};
    constexpr RowHash row_pure{0x1111'1111'1111'1111ULL};
    constexpr RowHash row_io{0x2222'2222'2222'2222ULL};

    constexpr KernelCacheKey k_a_pure{content_a, row_pure};
    constexpr KernelCacheKey k_a_io{content_a, row_io};
    constexpr KernelCacheKey k_b_pure{content_b, row_pure};
    constexpr KernelCacheKey k_b_io{content_b, row_io};

    static_assert(k_a_pure != k_a_io, "row_hash axis must discriminate — Pure and IO "
                                      "rows of the same region are distinct cache keys.");
    static_assert(k_b_pure != k_b_io);

    static_assert(k_a_pure != k_b_pure, "content_hash axis must discriminate — different "
                                        "regions with the same row are distinct cache keys.");
    static_assert(k_a_io != k_b_io);

    static_assert(k_a_pure != k_b_io);

    constexpr KernelCacheKey k_a_pure_dup{content_a, row_pure};
    static_assert(k_a_pure == k_a_pure_dup);

    std::printf("  test_per_axis_distinctness:     PASSED\n");
}

static void test_lexicographic_ordering() {
    constexpr ContentHash content_lo{0x0000'0000'0000'0001ULL};
    constexpr ContentHash content_hi{0x0000'0000'0000'0002ULL};
    constexpr RowHash row_lo{0x0000'0000'0000'0001ULL};
    constexpr RowHash row_hi{0x0000'0000'0000'0002ULL};

    constexpr KernelCacheKey k_lolo{content_lo, row_lo};
    constexpr KernelCacheKey k_lohi{content_lo, row_hi};
    constexpr KernelCacheKey k_hilo{content_hi, row_lo};
    constexpr KernelCacheKey k_hihi{content_hi, row_hi};

    static_assert(k_lohi < k_hilo, "content_hash must be the major sort key.");
    static_assert((k_lohi <=> k_hilo) == std::strong_ordering::less);

    static_assert(k_lolo < k_lohi);
    static_assert(k_hilo < k_hihi);

    static_assert((k_lolo <=> k_lolo) == std::strong_ordering::equal);
    static_assert((k_lolo <=> k_hihi) == std::strong_ordering::less);
    static_assert((k_hihi <=> k_lolo) == std::strong_ordering::greater);

    std::printf("  test_lexicographic_ordering:    PASSED\n");
}

// A plain pair of uint64_t would let a caller pass the row value where
// the content value belongs, with no diagnostic.
static void test_axis_swap_rejected() {
    static_assert(!std::is_constructible_v<KernelCacheKey, RowHash, ContentHash>,
                  "Axis order is part of the type — RowHash-then-"
                  "ContentHash construction must be rejected.");

    static_assert(!std::is_constructible_v<KernelCacheKey, uint64_t, uint64_t>,
                  "Raw uint64_t pair must not silently brace-init a "
                  "key — both axes require explicit hash construction.");

    static_assert(!std::is_convertible_v<ContentHash, KernelCacheKey>);
    static_assert(!std::is_convertible_v<RowHash, KernelCacheKey>);

    std::printf("  test_axis_swap_rejected:        PASSED\n");
}

// The strong-hash macro already makes each hash a distinct type.  These
// assertions catch a later edit that widens the conversion surface.
static void test_rowhash_isolation() {
    static_assert(!std::is_convertible_v<RowHash, ContentHash>);
    static_assert(!std::is_convertible_v<RowHash, MerkleHash>);
    static_assert(!std::is_convertible_v<RowHash, SchemaHash>);
    static_assert(!std::is_convertible_v<RowHash, ShapeHash>);
    static_assert(!std::is_convertible_v<RowHash, ScopeHash>);
    static_assert(!std::is_convertible_v<RowHash, CallsiteHash>);
    static_assert(!std::is_convertible_v<RowHash, RecipeHash>);

    static_assert(!std::is_convertible_v<ContentHash, RowHash>);
    static_assert(!std::is_convertible_v<MerkleHash, RowHash>);
    static_assert(!std::is_convertible_v<SchemaHash, RowHash>);
    static_assert(!std::is_convertible_v<ShapeHash, RowHash>);
    static_assert(!std::is_convertible_v<ScopeHash, RowHash>);
    static_assert(!std::is_convertible_v<CallsiteHash, RowHash>);
    static_assert(!std::is_convertible_v<RecipeHash, RowHash>);

    // Construction works, conversion does not: the constructor is explicit.
    static_assert(!std::is_convertible_v<uint64_t, RowHash>);
    static_assert(std::is_constructible_v<RowHash, uint64_t>);

    // If RowHash drifts from the shape the macro produces, this trips first.
    static_assert(sizeof(RowHash) == sizeof(uint64_t));
    static_assert(std::is_trivially_copyable_v<RowHash>);
    static_assert(std::is_standard_layout_v<RowHash>);
    static_assert(std::is_nothrow_default_constructible_v<RowHash>);

    static_assert(std::is_constructible_v<KernelCacheKey, ContentHash, RowHash>);

    std::printf("  test_rowhash_isolation:         PASSED\n");
}

// The key travels as an SPSC payload.  A member that throws would leak
// the producer side of the ring.
static void test_full_noexcept() {
    static_assert(std::is_nothrow_default_constructible_v<KernelCacheKey>);
    static_assert(std::is_nothrow_copy_constructible_v<KernelCacheKey>);
    static_assert(std::is_nothrow_move_constructible_v<KernelCacheKey>);
    static_assert(std::is_nothrow_copy_assignable_v<KernelCacheKey>);
    static_assert(std::is_nothrow_move_assignable_v<KernelCacheKey>);
    static_assert(std::is_nothrow_destructible_v<KernelCacheKey>);

    constexpr KernelCacheKey k1{};
    constexpr KernelCacheKey k2{};
    static_assert(noexcept(k1.is_zero()));
    static_assert(noexcept(k1.is_sentinel()));
    static_assert(noexcept(KernelCacheKey::sentinel()));
    static_assert(noexcept(k1 <=> k2));
    static_assert(noexcept(k1 == k2));
    static_assert(noexcept(k1 != k2));

    std::printf("  test_full_noexcept:             PASSED\n");
}

// Callers often have one axis and want the other defaulted.
static void test_designated_init_forms() {
    constexpr KernelCacheKey k_full{
        .content_hash = ContentHash{42},
        .row_hash = RowHash{99},
    };
    static_assert(k_full.content_hash == ContentHash{42});
    static_assert(k_full.row_hash == RowHash{99});

    constexpr KernelCacheKey k_content_only{
        .content_hash = ContentHash{42},
    };
    static_assert(k_content_only.content_hash == ContentHash{42});
    static_assert(k_content_only.row_hash == RowHash{});
    static_assert(k_content_only.row_hash.raw() == 0);

    constexpr KernelCacheKey k_row_only{
        .row_hash = RowHash{99},
    };
    static_assert(k_row_only.content_hash == ContentHash{});
    static_assert(k_row_only.row_hash == RowHash{99});

    constexpr KernelCacheKey k_positional_one{ContentHash{42}};
    static_assert(k_positional_one.content_hash == ContentHash{42});
    static_assert(k_positional_one.row_hash.raw() == 0);

    std::printf("  test_designated_init:           PASSED\n");
}

// The constexpr assertions above could all hold while a runtime path
// miscompiles.  The volatile seeds stop the compiler folding these
// comparisons to true.
static void test_runtime_peer() {
    volatile uint64_t content_a_v = 0xC0FFEEBA'1234'5678ULL;
    volatile uint64_t content_b_v = 0xDEAD'BEEF'CAFE'BABEULL;
    volatile uint64_t row_pure_v = 0x1111'1111'1111'1111ULL;
    volatile uint64_t row_io_v = 0x2222'2222'2222'2222ULL;

    KernelCacheKey k_a_pure{ContentHash{content_a_v}, RowHash{row_pure_v}};
    KernelCacheKey k_a_io{ContentHash{content_a_v}, RowHash{row_io_v}};
    KernelCacheKey k_b_pure{ContentHash{content_b_v}, RowHash{row_pure_v}};

    assert(k_a_pure != k_a_io);
    assert(k_a_pure != k_b_pure);
    assert(k_a_pure < k_a_io);
    assert(k_a_pure < k_b_pure);

    KernelCacheKey k_a_pure_dup{ContentHash{content_a_v}, RowHash{row_pure_v}};
    assert(k_a_pure == k_a_pure_dup);

    auto bytes = std::bit_cast<std::array<uint64_t, 2>>(k_a_pure);
    assert(bytes[0] == content_a_v);
    assert(bytes[1] == row_pure_v);

    auto back = std::bit_cast<KernelCacheKey>(bytes);
    assert(back == k_a_pure);

    std::printf("  test_runtime_peer:              PASSED\n");
}

// A real hash can land on UINT64_MAX on one axis by collision.  Only
// the all-sentinel state marks an empty slot, so a single-axis
// collision must not read as empty.
static void test_partial_sentinel_not_full_sentinel() {
    constexpr KernelCacheKey k_partial_content{
        ContentHash::sentinel(),
        RowHash{0xDEADULL},
    };
    static_assert(!k_partial_content.is_sentinel(), "A key with only the content axis at sentinel is "
                                                    "NOT a full sentinel — both axes must coincide.");
    static_assert(!k_partial_content.is_zero());

    constexpr KernelCacheKey k_partial_row{
        ContentHash{0xBEEFULL},
        RowHash::sentinel(),
    };
    static_assert(!k_partial_row.is_sentinel());
    static_assert(!k_partial_row.is_zero());

    static_assert(k_partial_content != k_partial_row);
    static_assert(k_partial_content != KernelCacheKey::sentinel());
    static_assert(k_partial_row != KernelCacheKey::sentinel());

    std::printf("  test_partial_sentinel:          PASSED\n");
}

// The on-disk byte layout must match the in-memory layout, which is
// what lets a federation cache key cross process boundaries.
static void test_bit_cast_round_trip() {
    constexpr KernelCacheKey k_in{
        ContentHash{0xC0FFEEBA'1234'5678ULL},
        RowHash{0xDEAD'BEEF'5678'9ABCULL},
    };

    constexpr auto raw = std::bit_cast<std::array<uint64_t, 2>>(k_in);
    static_assert(raw[0] == 0xC0FFEEBA'1234'5678ULL);
    static_assert(raw[1] == 0xDEAD'BEEF'5678'9ABCULL);

    constexpr auto back = std::bit_cast<KernelCacheKey>(raw);
    static_assert(back == k_in);
    static_assert(back.content_hash == k_in.content_hash);
    static_assert(back.row_hash == k_in.row_hash);

    std::printf("  test_bit_cast_round_trip:       PASSED\n");
}

int main() {
    test_layout_invariants();
    test_default_is_zero();
    test_sentinel_state();
    test_per_axis_distinctness();
    test_lexicographic_ordering();
    test_axis_swap_rejected();
    test_rowhash_isolation();
    test_full_noexcept();
    test_designated_init_forms();
    test_runtime_peer();
    test_partial_sentinel_not_full_sentinel();
    test_bit_cast_round_trip();
    std::printf("test_kernel_cache_key: 12 groups, all passed\n");
    return 0;
}
