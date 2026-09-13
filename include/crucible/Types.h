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
