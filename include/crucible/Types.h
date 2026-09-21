#pragma once

#include <crucible/Platform.h>
#include <crucible/fixy/wrap/Refined.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible {

// The number of bytes one element of a type occupies.
//
// A bare byte count is interchangeable with a slot count, a log index or a
// running total, all of which also fit in eight bits and mean something else
// entirely. This does not convert to any of them. The domain is zero, for the
// undefined type, and the powers of two up to sixteen, so anything above
// sixteen fails at construction rather than propagating.
struct ElementBytes {
    fixy::wrap::Refined<fixy::wrap::bounded_above<uint8_t{16}>, uint8_t> value_{uint8_t{0}};

    constexpr ElementBytes() noexcept = default;
    explicit constexpr ElementBytes(uint8_t v) noexcept : value_{v} {}

    [[nodiscard]] constexpr uint8_t raw() const noexcept { return value_.value(); }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return value_.value() == 0; }

    auto operator<=>(const ElementBytes&) const = default;

    // Widens to a size, so a total is not truncated. The multiplication is
    // unchecked. A caller that cannot bound the count needs a checked one.
    [[nodiscard]] constexpr std::size_t times(std::size_t n) const noexcept { return std::size_t{value_.value()} * n; }
};
static_assert(sizeof(ElementBytes) == sizeof(uint8_t), "ElementBytes must be layout-identical to uint8_t");

// The ordinals mirror the foreign runtime's exactly, so a value crosses the
// boundary as a plain cast in either direction.
enum class ScalarType : int8_t {
    Byte = 0,
    Char = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Half = 5,
    Float = 6,
    Double = 7,
    ComplexHalf = 8,
    ComplexFloat = 9,
    ComplexDouble = 10,
    Bool = 11,
    BFloat16 = 15,
    Float8_e5m2 = 23,
    Float8_e4m3fn = 24,
    Float8_e5m2fnuz = 25,
    Float8_e4m3fnuz = 26,
    Undefined = -1,
};

// Only the undefined type has a size of zero. The postcondition carries that
// to call sites where the type is known, so size arithmetic there needs no
// guard against dividing by zero. A caller that accepts the undefined type
// handles the zero itself.
CRUCIBLE_CONST constexpr ElementBytes element_size(ScalarType const t) noexcept
    post(r : t == ScalarType::Undefined || !r.is_zero()) {
    switch (t) {
        case ScalarType::Bool:
        case ScalarType::Byte:
        case ScalarType::Char:
        case ScalarType::Float8_e5m2:
        case ScalarType::Float8_e4m3fn:
        case ScalarType::Float8_e5m2fnuz:
        case ScalarType::Float8_e4m3fnuz:
            return ElementBytes{1};
        case ScalarType::Short:
        case ScalarType::Half:
        case ScalarType::BFloat16:
            return ElementBytes{2};
        case ScalarType::Int:
        case ScalarType::Float:
        case ScalarType::ComplexHalf:
            return ElementBytes{4};
        case ScalarType::Long:
        case ScalarType::Double:
        case ScalarType::ComplexFloat:
            return ElementBytes{8};
        case ScalarType::ComplexDouble:
            return ElementBytes{16};
        case ScalarType::Undefined:
            return ElementBytes{0};
        default:
            std::unreachable();
    }
}

// The ordinals mirror the foreign runtime's exactly.
enum class DeviceType : int8_t {
    CPU = 0,
    CUDA = 1,
    MKLDNN = 2,
    HIP = 6,
    XLA = 9,
    MPS = 13,
    Meta = 14,
    PrivateUse1 = 20,
};

// The ordinals mirror the foreign runtime's exactly.
enum class Layout : int8_t {
    Strided = 0,
    Sparse = 1,
    SparseCsr = 2,
    SparseCsc = 3,
    SparseBsr = 4,
    SparseBsc = 5,
};

// Distinct index types that cannot be passed for one another. Arithmetic is
// deliberately absent: a caller unwraps, computes, and wraps the result back,
// which makes every place one index is derived from another one visible.

#define CRUCIBLE_STRONG_ID(Name)                                                                  \
    struct Name {                                                                                 \
    private:                                                                                      \
        /* Private, so the explicit constructor is the only way in. A public */                   \
        /* field would let a plain assignment bypass it and rewrite an */                         \
        /* identifier in place. */                                                                \
        uint32_t v;                                                                               \
                                                                                                  \
    public:                                                                                       \
        constexpr Name() noexcept : v(UINT32_MAX) {}                                              \
        constexpr explicit Name(uint32_t val) noexcept : v(val) {}                                \
        /* The named form for building one identifier kind out of another, */                     \
        /* which is where a silent mix-up happens. Naming it makes every */                       \
        /* such crossing findable by one search. */                                               \
        [[nodiscard]] static constexpr Name from_raw(uint32_t val) noexcept { return Name{val}; } \
        [[nodiscard]] static constexpr Name none() noexcept { return Name{UINT32_MAX}; }          \
        [[nodiscard]] constexpr bool is_valid() const noexcept { return v != UINT32_MAX; }        \
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_valid(); }    \
        [[nodiscard]] constexpr uint32_t raw() const noexcept { return v; }                       \
        constexpr auto operator<=>(const Name&) const noexcept = default;                         \
    };                                                                                            \
    static_assert(sizeof(Name) == sizeof(uint32_t))

CRUCIBLE_STRONG_ID(OpIndex);
CRUCIBLE_STRONG_ID(SlotId);
CRUCIBLE_STRONG_ID(NodeId);
CRUCIBLE_STRONG_ID(SymbolId);
CRUCIBLE_STRONG_ID(MetaIndex);

#undef CRUCIBLE_STRONG_ID

// Distinct hash types that cannot be passed for one another. Zero means unset.
// Arithmetic is absent for the same reason as on the index types above.

#define CRUCIBLE_STRONG_HASH(Name)                                                                \
    struct Name {                                                                                 \
    private:                                                                                      \
        uint64_t v;                                                                               \
                                                                                                  \
    public:                                                                                       \
        constexpr Name() noexcept : v(0) {}                                                       \
        constexpr explicit Name(uint64_t val) noexcept : v(val) {}                                \
        /* The named form for building one hash kind out of another, so that */                   \
        /* every such crossing is findable by one search. */                                      \
        [[nodiscard]] static constexpr Name from_raw(uint64_t val) noexcept { return Name{val}; } \
        [[nodiscard]] constexpr uint64_t raw() const noexcept { return v; }                       \
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return v != 0; }        \
        /* An end-of-region marker. The hash functions spread their output   \
     * uniformly, so this one value is reserved rather than produced. */                    \
        [[nodiscard]] static constexpr Name sentinel() noexcept { return Name{UINT64_MAX}; }      \
        [[nodiscard]] constexpr bool is_sentinel() const noexcept { return v == UINT64_MAX; }     \
        constexpr auto operator<=>(const Name&) const noexcept = default;                         \
    };                                                                                            \
    static_assert(sizeof(Name) == sizeof(uint64_t))

CRUCIBLE_STRONG_HASH(SchemaHash);  // identity of an operation
CRUCIBLE_STRONG_HASH(ShapeHash);  // geometry of an operation's inputs
CRUCIBLE_STRONG_HASH(ScopeHash);  // path through the module hierarchy
CRUCIBLE_STRONG_HASH(CallsiteHash);  // identity of a source location
CRUCIBLE_STRONG_HASH(ContentHash);  // structural identity of one region
CRUCIBLE_STRONG_HASH(MerkleHash);  // identity of a subtree, descendants included
CRUCIBLE_STRONG_HASH(RecipeHash);  // identity of a numerical recipe
CRUCIBLE_STRONG_HASH(RowHash);  // identity of an effect row

#undef CRUCIBLE_STRONG_HASH

namespace hash_family {
struct FamilyA {};
struct FamilyB {};
}  // namespace hash_family

// Which family a hash belongs to. The primary template is left undefined so
// that a new hash type without a specialisation fails to compile rather than
// silently defaulting into one of the families. The concepts below let a
// consumer say which family it accepts without naming every member.
template <class HashT>
struct hash_family_of;

// One specialisation per hash above, in the same order, so a new hash has an
// obvious place to go.
template <>
struct hash_family_of<SchemaHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<ShapeHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<ScopeHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<CallsiteHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<ContentHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<MerkleHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<RecipeHash> {
    using family = hash_family::FamilyA;
};
template <>
struct hash_family_of<RowHash> {
    using family = hash_family::FamilyA;
};

template <class HashT>
using hash_family_of_t = typename hash_family_of<HashT>::family;

template <class HashT>
concept IsFamilyA = std::is_same_v<hash_family_of_t<HashT>, hash_family::FamilyA>;

template <class HashT>
concept IsFamilyB = std::is_same_v<hash_family_of_t<HashT>, hash_family::FamilyB>;

// A new hash type with no family specialisation fails to instantiate these,
// and a hash that changes family fails the one that names it.
static_assert(IsFamilyA<SchemaHash>, "SchemaHash must be persistent: an operation's identity is compared "
                                     "across processes.");
static_assert(IsFamilyA<ShapeHash>, "ShapeHash must be persistent: tensor geometry is compared bit for bit "
                                    "across a replay.");
static_assert(IsFamilyA<ScopeHash>, "ScopeHash must be persistent: a module path identity is stored.");
static_assert(IsFamilyA<CallsiteHash>, "CallsiteHash must be persistent: a source-location identity is "
                                       "pinned in stored expectations.");
static_assert(IsFamilyA<ContentHash>, "ContentHash must be persistent: a region's structural identity keys "
                                      "stored objects and compiled kernels.");
static_assert(IsFamilyA<MerkleHash>, "MerkleHash must be persistent: a subtree identity is stored and "
                                     "compared across vendors.");
static_assert(IsFamilyA<RecipeHash>, "RecipeHash must be persistent: a numerical recipe's identity is "
                                     "shared between installations.");
static_assert(IsFamilyA<RowHash>, "RowHash must be persistent: an effect row's identity is half of a "
                                  "compiled-kernel lookup key.");

// The two families are disjoint.
static_assert(!IsFamilyB<ContentHash>, "a persistent hash must not also be process-local: the separation "
                                       "of the two families is what makes either safe.");

// The two families differ in what they promise, and the difference is what
// makes each one usable.
//
// A persistent hash is byte-stable across processes, machines and builds
// within one format version. Stored keys, shared artefacts and replay
// determinism all rest on that, so a single-bit change to how one is computed
// is a format break and needs a version bump. An expectation pinned for one is
// only trustworthy if it was taken from serialised bytes, since the bytes are
// what the promise covers, not the in-memory layout that produced them.
//
// A process-local hash is deliberately not stable. It mixes in address
// entropy, which a modern loader randomises per process, to get a cheap
// intern-table probe. The same structural input hashes differently in two
// processes. Such a value must never be persisted, pinned in a stored
// expectation, or folded into a persistent hash. Anything needing a stable
// identity for the same structure must be computed by walking that structure
// without consulting any address.

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(OpIndex);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SlotId);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(NodeId);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SymbolId);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(MetaIndex);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SchemaHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(ShapeHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(ScopeHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(CallsiteHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(ContentHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(MerkleHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(RecipeHash);
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(RowHash);

// Two axes, neither of which identifies a compiled kernel on its own. One says
// what the computation does. The other says what effects it is allowed to
// have. Two regions that agree on the first and differ on the second are the
// same computation under different regimes and must occupy different cache
// slots: one of them may be shareable between installations while the other,
// carrying an effect, is not, and letting them share a slot breaks that
// silently. Both are persistent hashes, so the whole key is stable across
// processes for as long as the format version holds.
//
// The name points at the wrong consumer. The in-process KernelCache never uses
// this type. That cache takes the two hashes as separate parameters
// (MerkleDag.h:895) and holds three separate atomics for each slot
// (MerkleDag.h:700). It keeps them apart on purpose, because the hot lookup
// reads one field at a time in probe order (MerkleDag.h:710-714). Do not pack
// this type into that slot. Three static_assert statements pin the slot at 24
// bytes (MerkleDag.h:706-708).
//
// Every consumer of this type puts a key on the federation wire:
//   - ComputationCacheFederation.h:130 builds one from a function and a row
//   - FederationProtocol.h:226 writes one, and :303 reads one back
//   - Cipher.h:524 builds one for a session event.
//
// The in-process cache and the wire want opposite properties from the row
// half. The difference decides the effect of a row collision.
//
// For the cache the row half is the discriminator, and it must be unique.
// MerkleDag.h:1381 supplies a row-blind region content hash and a row, so one
// content hash occupies one slot for each discipline (MerkleDag.h:669-673).
// The probe finds a slot from the content alone (MerkleDag.h:902) and then
// filters on an exact row compare (MerkleDag.h:920-922). A row mismatch
// continues the probe and ends at a miss, which is the intent. A row collision
// returns the kernel of another discipline, which is a defect.
//
// For the wire the row half repeats what the content half already holds.
// ComputationCache.h:144 folds the row into the content hash. The purpose of
// that fold is family separation and not discrimination
// (ComputationCache.h:130-131, :141-143). A row collision alone causes no
// damage there, because the content halves already differ.
//
// The fail directions differ too. The content half has a check after the match
// on both paths. The cache refuses a zero content hash (MerkleDag.h:897) and
// reserves UINT64_MAX (MerkleDag.h:932-934). The wire hashes the payload again
// and compares the content half (FederationProtocol.h:20-22, :272-273).
// Nothing checks the row half after a match on either path. The row half rests
// on injectivity alone.
//
// test_row_hash_distinctness.cpp supplies that injectivity at compile time. It
// asserts pairwise distinctness across a 44-entry wrapper by stance matrix, it
// refuses a collision with a reserved value, and it pins the fold anchor. A
// collision in that matrix stops the build.
//
// A check after the match in the cache probe would add no guarantee, and it is
// not available at that site. The probe already compares the full 64-bit row.
// The cache receives the row as an opaque hash, and no second witness of the
// row identity reaches it. Such a witness needs a wider parameter list and a
// wider slot, and the 24-byte pins above refuse both.
//
// The cost, measured on cpu88 of the bench host over 12 runs of
// bench_kernel_cache, worst value of each run: the live lookup gives p50
// 1.39 ns, p99 1.39 ns, p99.9 1.74 ns and max 2.55 ns, at approximately 3.6
// cycles. The worst coefficient of variation is 2.2%, and the spread of p50
// across runs is 3.0%. A second acquire load and compare for each probe is a
// large part of 3.6 cycles.
//
// The row-sibling scenario of the same bench gives a coefficient of variation
// of 45%, which makes it not valid as a latency number. It still shows the
// shape: 128 rows for one content hash form one probe chain, because the slot
// index uses the content alone.
//
// The zero refusal of the wire does not cover the row half. is_zero() below
// needs both halves at zero. So an entry with a non-zero content hash and
// RowHash{0} passes the refusal on write (FederationProtocol.h:231-233) and on
// read (FederationProtocol.h:307-309). This is correct. RowHash{0} is the
// bare-type baseline and a valid key (MerkleDag.h:672-673), and it is the row
// value with the most traffic in the shipped code (MerkleDag.h:1381).
struct KernelCacheKey {
    ContentHash content_hash{};
    RowHash row_hash{};

    // Orders on content first and breaks ties on the row.
    auto operator<=>(const KernelCacheKey&) const noexcept = default;

    // Both axes at their default. This normally means unset rather than a real
    // region, but only the sentinel below is guaranteed never to be produced.
    [[nodiscard]] constexpr bool is_zero() const noexcept { return !content_hash && !row_hash; }

    // The value no hash function produces, which is what an empty probe slot
    // is marked with.
    [[nodiscard]] static constexpr KernelCacheKey sentinel() noexcept {
        return KernelCacheKey{ContentHash::sentinel(), RowHash::sentinel()};
    }

    [[nodiscard]] constexpr bool is_sentinel() const noexcept {
        return content_hash.is_sentinel() && row_hash.is_sentinel();
    }
};

static_assert(sizeof(KernelCacheKey) == 16, "KernelCacheKey must be exactly two 64-bit hashes with no padding.");
static_assert(alignof(KernelCacheKey) == 8, "KernelCacheKey must be 8-byte aligned to stay compatible with a "
                                            "paired atomic on the supported architectures.");

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(KernelCacheKey);

}  // namespace crucible
