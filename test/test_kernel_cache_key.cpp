// FOUND-I01: KernelCacheKey { ContentHash, RowHash } — composite
// federation-cache identity.
//
// Verifies the structural axioms of the new key type:
//   • TypeSafe   — neither raw uint64_t nor either single hash converts
//                  silently to a KernelCacheKey; a ContentHash↔RowHash
//                  swap is detected by the type system.
//   • InitSafe   — NSDMI gives every default-constructed key a fully
//                  specified zero state, both axes inclusive.
//   • MemSafe    — KernelCacheKey is trivially copyable, suitable for
//                  Arena memcpy and AoS storage in the open-addressing
//                  KernelCache slot table.
//   • DetSafe    — comparison is total and lexicographic on
//                  (content_hash, row_hash); replaying a sequence of
//                  inserts produces a deterministic ordering.
//
// The wiring of these keys into KernelCache::lookup (and the
// downstream L1 / L2 / L3 cache tier signature changes) is FOUND-I05/
// 06/07; this fixture proves the *value type* itself is sound BEFORE
// any production call site reads the new shape.

#include <crucible/Types.h>

#include "test_assert.h"

#include <array>
#include <bit>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <type_traits>

using namespace crucible;

// ─────────────────────────────────────────────────────────────────────
// Layout — exactly two persistent 64-bit hashes, no padding, 8B align.
// The KernelCache Entry is a 16-byte slot today (atomic<uint64_t>
// content_hash + atomic<CompiledKernel*> kernel — MerkleDag.h §979);
// the future row-extended slot is 16-byte key + 8-byte pointer = 24,
// which only fits cleanly if the key itself is exactly 16.
static void test_layout_invariants() {
    static_assert(sizeof(KernelCacheKey) == 16);
    static_assert(alignof(KernelCacheKey) == 8);
    static_assert(std::is_trivially_copyable_v<KernelCacheKey>);
    static_assert(std::is_standard_layout_v<KernelCacheKey>);

    // Field offsets — content axis comes first (major key), row axis
    // second (tie-breaker).  Pinning the offsets here lets serialize/
    // deserialize use offsetof if FOUND-I04+ ever needs to reach into
    // the bytes.
    static_assert(offsetof(KernelCacheKey, content_hash) == 0);
    static_assert(offsetof(KernelCacheKey, row_hash)     == 8);

    std::printf("  test_layout_invariants:         PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Default state — both axes zero.  Distinct from sentinel state.
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

// ─────────────────────────────────────────────────────────────────────
// Sentinel state — both axes UINT64_MAX.  Disjoint from real hashes
// (FNV-1a / fmix64 avalanche makes UINT64_MAX collision astronomically
// unlikely; we reserve it as the EMPTY-slot probe marker).
static void test_sentinel_state() {
    constexpr KernelCacheKey s = KernelCacheKey::sentinel();
    static_assert(s.is_sentinel());
    static_assert(!s.is_zero());
    static_assert(s.content_hash.is_sentinel());
    static_assert(s.row_hash.is_sentinel());
    static_assert(s.content_hash.raw() == UINT64_MAX);
    static_assert(s.row_hash.raw()     == UINT64_MAX);

    // Sentinel is distinct from default.
    constexpr KernelCacheKey d{};
    static_assert(s != d);
    static_assert(d != s);

    std::printf("  test_sentinel_state:            PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Per-axis distinctness — the entire reason the key is composite.
//
// (a) Same content_hash, different row_hash → distinct keys.
// (b) Different content_hash, same row_hash → distinct keys.
// (c) Both axes different → distinct keys (trivial).
// (d) Both axes equal → equal keys (trivial).
//
// (a) is the load-bearing case: a Pure-row kernel and an IO-row
// kernel may share a region structurally but MUST NOT share a cache
// slot — sharing breaks the federation contract by promising the
// cache reader a Pure result when only an IO result was computed.
static void test_per_axis_distinctness() {
    constexpr ContentHash content_a{0xC0FFEEBA'1234'5678ULL};
    constexpr ContentHash content_b{0xDEAD'BEEF'CAFE'BABEULL};
    constexpr RowHash     row_pure{0x1111'1111'1111'1111ULL};
    constexpr RowHash     row_io  {0x2222'2222'2222'2222ULL};

    constexpr KernelCacheKey k_a_pure{content_a, row_pure};
    constexpr KernelCacheKey k_a_io  {content_a, row_io};
    constexpr KernelCacheKey k_b_pure{content_b, row_pure};
    constexpr KernelCacheKey k_b_io  {content_b, row_io};

    // (a) row axis discriminates within the same content axis.
    static_assert(k_a_pure != k_a_io,
                  "row_hash axis must discriminate — Pure and IO "
                  "rows of the same region are distinct cache keys.");
    static_assert(k_b_pure != k_b_io);

    // (b) content axis discriminates within the same row axis.
    static_assert(k_a_pure != k_b_pure,
                  "content_hash axis must discriminate — different "
                  "regions with the same row are distinct cache keys.");
    static_assert(k_a_io != k_b_io);

    // (c) both axes different → still distinct.
    static_assert(k_a_pure != k_b_io);

    // (d) both axes equal → equal.
    constexpr KernelCacheKey k_a_pure_dup{content_a, row_pure};
    static_assert(k_a_pure == k_a_pure_dup);

    std::printf("  test_per_axis_distinctness:     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Lexicographic ordering on (content_hash, row_hash).  Content axis
// is the major key, row axis breaks ties.  Confirmed at compile time.
static void test_lexicographic_ordering() {
    constexpr ContentHash content_lo{0x0000'0000'0000'0001ULL};
    constexpr ContentHash content_hi{0x0000'0000'0000'0002ULL};
    constexpr RowHash     row_lo{0x0000'0000'0000'0001ULL};
    constexpr RowHash     row_hi{0x0000'0000'0000'0002ULL};

    constexpr KernelCacheKey k_lolo{content_lo, row_lo};
    constexpr KernelCacheKey k_lohi{content_lo, row_hi};
    constexpr KernelCacheKey k_hilo{content_hi, row_lo};
    constexpr KernelCacheKey k_hihi{content_hi, row_hi};

    // Content axis dominates: a lo-content key with hi-row is still
    // less than a hi-content key with lo-row.
    static_assert(k_lohi < k_hilo,
                  "content_hash must be the major sort key.");
    static_assert((k_lohi <=> k_hilo) == std::strong_ordering::less);

    // Within same content, row axis breaks ties.
    static_assert(k_lolo < k_lohi);
    static_assert(k_hilo < k_hihi);

    // Reflexivity and totality of operator<=>.
    static_assert((k_lolo <=> k_lolo) == std::strong_ordering::equal);
    static_assert((k_lolo <=> k_hihi) == std::strong_ordering::less);
    static_assert((k_hihi <=> k_lolo) == std::strong_ordering::greater);

    std::printf("  test_lexicographic_ordering:    PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// TypeSafe — the axis swap that motivates the strong-typed shape.
//
// A naive `struct { uint64_t a, b; }` would let a programmer pass
// (row_hash_value, content_hash_value) into a ctor expecting
// (content, row) — silent disaster.  KernelCacheKey takes
// ContentHash + RowHash; mixing the order is a hard compile error.
static void test_axis_swap_rejected() {
    // Brace-init with the wrong axis types must NOT compile.  Probed
    // structurally via SFINAE on a constraint check.
    static_assert(!std::is_constructible_v<KernelCacheKey, RowHash, ContentHash>,
                  "Axis order is part of the type — RowHash-then-"
                  "ContentHash construction must be rejected.");

    // Raw uint64_t pair must NOT silently become a key — both axes
    // are explicit-constructor-only strong hashes, so the implicit
    // conversion path is closed.
    static_assert(!std::is_constructible_v<KernelCacheKey, uint64_t, uint64_t>,
                  "Raw uint64_t pair must not silently brace-init a "
                  "key — both axes require explicit hash construction.");

    // KernelCacheKey itself is not implicitly convertible from either
    // single hash axis — neither is a complete identity.
    static_assert(!std::is_convertible_v<ContentHash, KernelCacheKey>);
    static_assert(!std::is_convertible_v<RowHash,     KernelCacheKey>);

    std::printf("  test_axis_swap_rejected:        PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Cross-strong-hash isolation — RowHash must not silently convert to
// any other Family-A persistent hash.  Each strong hash is its own
// type island; mixing them at a call site is a compile error.
//
// This audit closes the "I added a strong type but forgot to make it
// distinct from the other strong types" failure mode.  CRUCIBLE_STRONG_HASH
// already guarantees this structurally (each macro expansion produces
// a distinct unrelated struct), but pinning it as a test catches future
// edits that might accidentally widen the conversion surface.
static void test_rowhash_isolation() {
    // RowHash neither converts to nor from any other Family-A hash.
    static_assert(!std::is_convertible_v<RowHash, ContentHash>);
    static_assert(!std::is_convertible_v<RowHash, MerkleHash>);
    static_assert(!std::is_convertible_v<RowHash, SchemaHash>);
    static_assert(!std::is_convertible_v<RowHash, ShapeHash>);
    static_assert(!std::is_convertible_v<RowHash, ScopeHash>);
    static_assert(!std::is_convertible_v<RowHash, CallsiteHash>);
    static_assert(!std::is_convertible_v<RowHash, RecipeHash>);

    static_assert(!std::is_convertible_v<ContentHash,  RowHash>);
    static_assert(!std::is_convertible_v<MerkleHash,   RowHash>);
    static_assert(!std::is_convertible_v<SchemaHash,   RowHash>);
    static_assert(!std::is_convertible_v<ShapeHash,    RowHash>);
    static_assert(!std::is_convertible_v<ScopeHash,    RowHash>);
    static_assert(!std::is_convertible_v<CallsiteHash, RowHash>);
    static_assert(!std::is_convertible_v<RecipeHash,   RowHash>);

    // Raw uint64_t doesn't silently become a RowHash either — explicit
    // ctor only.  Construction works (ctor exists), conversion does not.
    static_assert(!std::is_convertible_v<uint64_t, RowHash>);
    static_assert(std::is_constructible_v<RowHash, uint64_t>);

    // Same shape as the other Family-A hashes — keeps the macro
    // expansion uniform.  If RowHash ever drifts (e.g. someone adds
    // arithmetic), this trips first.
    static_assert(sizeof(RowHash) == sizeof(uint64_t));
    static_assert(std::is_trivially_copyable_v<RowHash>);
    static_assert(std::is_standard_layout_v<RowHash>);
    static_assert(std::is_nothrow_default_constructible_v<RowHash>);

    // KernelCacheKey constructible from (ContentHash, RowHash) — the
    // ONLY two-arg shape that compiles.
    static_assert(std::is_constructible_v<KernelCacheKey,
                                          ContentHash, RowHash>);

    std::printf("  test_rowhash_isolation:         PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Full noexcept propagation — every member function is noexcept.  A
// future edit that accidentally throws (e.g. by adding a logging
// hook) would break SPSC-ring usage where the cache key is the SPSC
// payload and any throw would leak the producer side.
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

// ─────────────────────────────────────────────────────────────────────
// Designated-initializer construction — KernelCacheKey is an
// aggregate, so all of these forms must work.  This is the typical
// call-site shape when the caller has only one axis and wants the
// other defaulted.
static void test_designated_init_forms() {
    constexpr KernelCacheKey k_full{
        .content_hash = ContentHash{42},
        .row_hash     = RowHash{99},
    };
    static_assert(k_full.content_hash == ContentHash{42});
    static_assert(k_full.row_hash     == RowHash{99});

    constexpr KernelCacheKey k_content_only{
        .content_hash = ContentHash{42},
    };
    static_assert(k_content_only.content_hash == ContentHash{42});
    static_assert(k_content_only.row_hash     == RowHash{}); // default
    static_assert(k_content_only.row_hash.raw() == 0);

    constexpr KernelCacheKey k_row_only{
        .row_hash = RowHash{99},
    };
    static_assert(k_row_only.content_hash == ContentHash{}); // default
    static_assert(k_row_only.row_hash     == RowHash{99});

    // Positional brace-init with single argument also works (rest
    // defaulted via NSDMI).
    constexpr KernelCacheKey k_positional_one{ContentHash{42}};
    static_assert(k_positional_one.content_hash == ContentHash{42});
    static_assert(k_positional_one.row_hash.raw() == 0);

    std::printf("  test_designated_init:           PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Runtime peer for the constexpr claims above — ensures the
// constexpr static_asserts aren't masking a runtime miscompile via
// some constexpr-only fast path.  Volatile-anchored to defeat
// constant-fold-into-true.
//
// This is the "runtime smoke test" discipline (see
// feedback_algebra_runtime_smoke_test_discipline.md) applied to a
// type-level proof.  Same axioms, dynamic args, ABI-visible code.
static void test_runtime_peer() {
    volatile uint64_t content_a_v = 0xC0FFEEBA'1234'5678ULL;
    volatile uint64_t content_b_v = 0xDEAD'BEEF'CAFE'BABEULL;
    volatile uint64_t row_pure_v  = 0x1111'1111'1111'1111ULL;
    volatile uint64_t row_io_v    = 0x2222'2222'2222'2222ULL;

    KernelCacheKey k_a_pure{ContentHash{content_a_v}, RowHash{row_pure_v}};
    KernelCacheKey k_a_io  {ContentHash{content_a_v}, RowHash{row_io_v}};
    KernelCacheKey k_b_pure{ContentHash{content_b_v}, RowHash{row_pure_v}};

    assert(k_a_pure != k_a_io);     // row-axis discrimination
    assert(k_a_pure != k_b_pure);   // content-axis discrimination
    assert(k_a_pure < k_a_io);      // row axis tie-break with same content
    assert(k_a_pure < k_b_pure);    // content axis dominates

    KernelCacheKey k_a_pure_dup{ContentHash{content_a_v}, RowHash{row_pure_v}};
    assert(k_a_pure == k_a_pure_dup);

    // Round-trip via bit_cast — the layout invariant in action.
    auto bytes = std::bit_cast<std::array<uint64_t, 2>>(k_a_pure);
    assert(bytes[0] == content_a_v);  // content axis at offset 0
    assert(bytes[1] == row_pure_v);   // row axis at offset 8

    auto back = std::bit_cast<KernelCacheKey>(bytes);
    assert(back == k_a_pure);

    std::printf("  test_runtime_peer:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Sentinel arithmetic — used by the open-addressing probe loop.
// Mixed-axis sentinels (one axis sentinel, the other not) are NOT
// "is_sentinel()" — only the all-sentinel state is.  This matters
// because a real region could in theory produce ContentHash::sentinel
// (UINT64_MAX) or RowHash::sentinel by collision (probability ≈
// 2^-64), and the cache must not confuse a single-axis collision
// with the EMPTY-slot marker.
static void test_partial_sentinel_not_full_sentinel() {
    constexpr KernelCacheKey k_partial_content{
        ContentHash::sentinel(),
        RowHash{0xDEADULL},
    };
    static_assert(!k_partial_content.is_sentinel(),
                  "A key with only the content axis at sentinel is "
                  "NOT a full sentinel — both axes must coincide.");
    static_assert(!k_partial_content.is_zero());

    constexpr KernelCacheKey k_partial_row{
        ContentHash{0xBEEFULL},
        RowHash::sentinel(),
    };
    static_assert(!k_partial_row.is_sentinel());
    static_assert(!k_partial_row.is_zero());

    // And these two partials are distinct from each other and from
    // the full sentinel.
    static_assert(k_partial_content != k_partial_row);
    static_assert(k_partial_content != KernelCacheKey::sentinel());
    static_assert(k_partial_row     != KernelCacheKey::sentinel());

    std::printf("  test_partial_sentinel:          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// Bit-cast round trip — KernelCacheKey ↔ pair of uint64_t.  Confirms
// the on-the-wire / on-disk byte layout matches the in-memory layout
// exactly, which is what makes the federation cache key portable
// across processes within a CDAG_VERSION window.
static void test_bit_cast_round_trip() {
    constexpr KernelCacheKey k_in{
        ContentHash{0xC0FFEEBA'1234'5678ULL},
        RowHash    {0xDEAD'BEEF'5678'9ABCULL},
    };

    constexpr auto raw = std::bit_cast<std::array<uint64_t, 2>>(k_in);
    static_assert(raw[0] == 0xC0FFEEBA'1234'5678ULL);
    static_assert(raw[1] == 0xDEAD'BEEF'5678'9ABCULL);

    constexpr auto back = std::bit_cast<KernelCacheKey>(raw);
    static_assert(back == k_in);
    static_assert(back.content_hash == k_in.content_hash);
    static_assert(back.row_hash     == k_in.row_hash);

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
