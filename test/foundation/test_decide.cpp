// SPDX-License-Identifier: Apache-2.0
//
// Value-level coverage for the decide predicate library: every
// predicate is asserted both to accept and to reject concrete
// inputs. The compile-failure direction, where a predicate trips a
// precondition at consteval, is pinned by separate negative-compile
// fixtures.
//
// The strong hash types of crucible/Types.h are in a chain header, so the
// sentinel cells use a local stand-in with the same sentinel API.

#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

namespace {

namespace dc = foundation::decide;

static_assert(dc::no_overflow_sum<uint32_t>(0, 0));
static_assert(dc::no_overflow_sum<int32_t>(0, 0));

// Each pair straddles a representable limit by one.
static_assert(dc::no_overflow_sum<uint8_t>(1, 254));
static_assert(!dc::no_overflow_sum<uint8_t>(1, 255));
static_assert(dc::no_overflow_sum<uint32_t>(0xFFFFFFFEu, 1u));
static_assert(!dc::no_overflow_sum<uint32_t>(std::numeric_limits<uint32_t>::max(), 1u));

static_assert(dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::max() - 1, 1));
static_assert(!dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::max(), 1));
static_assert(dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min() + 1, -1));
static_assert(!dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min(), -1));

static_assert(dc::no_overflow_sum<int32_t>(-100, 50));
static_assert(dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min(),
                                           std::numeric_limits<int32_t>::max()));  // -1, fits

static_assert(dc::no_overflow_sum<uint64_t>(0xFFFFFFFFFFFFFFFEull, 1ull));
static_assert(!dc::no_overflow_sum<uint64_t>(std::numeric_limits<uint64_t>::max(), 1ull));
static_assert(!dc::no_overflow_sum<int64_t>(std::numeric_limits<int64_t>::min(), -1));

[[nodiscard]] constexpr uint64_t safe_add_u64(uint64_t a, uint64_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_sum(a, b));
    return a + b;
}

[[nodiscard]] constexpr int32_t safe_add_i32(int32_t a, int32_t b) noexcept {
    CRUCIBLE_PRE(dc::no_overflow_sum(a, b));
    return a + b;
}

static_assert(safe_add_u64(40, 2) == 42);
static_assert(safe_add_i32(-100, 50) == -50);
static_assert(safe_add_i32(std::numeric_limits<int32_t>::max() - 1, 1) == std::numeric_limits<int32_t>::max());

static_assert(dc::is_power_of_two_le<uint32_t>(1u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(2u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(4u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(8u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(16u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(32u, 64u));
static_assert(dc::is_power_of_two_le<uint32_t>(64u, 64u));

static_assert(!dc::is_power_of_two_le<uint32_t>(128u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(256u, 64u));
static_assert(!dc::is_power_of_two_le<uint64_t>(uint64_t{1} << 32, uint64_t{1} << 30));

static_assert(!dc::is_power_of_two_le<uint32_t>(3u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(5u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(6u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(7u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(10u, 64u));
static_assert(!dc::is_power_of_two_le<uint32_t>(12u, 64u));
// 48 defeats a test that only asks whether x is even and within bound.
static_assert(!dc::is_power_of_two_le<uint32_t>(48u, 64u));

static_assert(!dc::is_power_of_two_le<uint32_t>(0u, 64u));
static_assert(!dc::is_power_of_two_le<uint64_t>(0ull, 1024ull));

static_assert(!dc::is_power_of_two_le<int32_t>(-1, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(-128, 64));
// INT_MIN is the input a missing positivity guard would take to
// undefined behaviour on x - 1.
static_assert(!dc::is_power_of_two_le<int32_t>(std::numeric_limits<int32_t>::min(), 64));

static_assert(dc::is_power_of_two_le<int32_t>(1, 64));
static_assert(dc::is_power_of_two_le<int32_t>(64, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(48, 64));
static_assert(!dc::is_power_of_two_le<int32_t>(128, 64));

static_assert(!dc::is_power_of_two_le<uint32_t>(1u, 0u));
static_assert(!dc::is_power_of_two_le<int32_t>(1, -1));

static_assert(dc::is_power_of_two_le<uint64_t>(uint64_t{1} << 63, std::numeric_limits<uint64_t>::max()));

// The three admissible SIMD group widths, each of which has to pass
// at bound 64.
constexpr std::size_t valid_widths[] = {16, 32, 64};
template <std::size_t... Is>
constexpr bool all_valid_widths(std::index_sequence<Is...>) noexcept {
    return (... && dc::is_power_of_two_le<std::size_t>(valid_widths[Is], 64));
}
static_assert(all_valid_widths(std::make_index_sequence<3>{}));

[[nodiscard]] constexpr std::size_t safe_table_capacity(std::size_t w) noexcept {
    CRUCIBLE_PRE(dc::is_power_of_two_le<std::size_t>(w, 64));
    return w;
}

static_assert(safe_table_capacity(16) == 16);
static_assert(safe_table_capacity(32) == 32);
static_assert(safe_table_capacity(64) == 64);

namespace cl = foundation::algebra::lattices;

// Three unrelated chain-tier enums, to show the predicate depends only
// on the shared convention that a stronger guarantee takes a higher
// ordinal. The CipherTierTag chain runs Cold, Warm, Hot.
static_assert(dc::tier_replaces(cl::CipherTierTag::Cold, cl::CipherTierTag::Cold));
static_assert(dc::tier_replaces(cl::CipherTierTag::Warm, cl::CipherTierTag::Warm));
static_assert(dc::tier_replaces(cl::CipherTierTag::Hot, cl::CipherTierTag::Hot));

static_assert(dc::tier_replaces(cl::CipherTierTag::Warm, cl::CipherTierTag::Cold));
static_assert(dc::tier_replaces(cl::CipherTierTag::Hot, cl::CipherTierTag::Cold));
static_assert(dc::tier_replaces(cl::CipherTierTag::Hot, cl::CipherTierTag::Warm));

static_assert(!dc::tier_replaces(cl::CipherTierTag::Cold, cl::CipherTierTag::Warm));
static_assert(!dc::tier_replaces(cl::CipherTierTag::Cold, cl::CipherTierTag::Hot));
static_assert(!dc::tier_replaces(cl::CipherTierTag::Warm, cl::CipherTierTag::Hot));

// The HotPathTier chain also runs Cold, Warm, Hot, where Cold
// tolerates blocking and Hot forbids it.
static_assert(dc::tier_replaces(cl::HotPathTier::Hot, cl::HotPathTier::Cold));
static_assert(dc::tier_replaces(cl::HotPathTier::Hot, cl::HotPathTier::Hot));
static_assert(!dc::tier_replaces(cl::HotPathTier::Cold, cl::HotPathTier::Hot));
static_assert(!dc::tier_replaces(cl::HotPathTier::Warm, cl::HotPathTier::Hot));
static_assert(!dc::tier_replaces(cl::HotPathTier::Cold, cl::HotPathTier::Warm));

// The DetSafeTier chain has seven elements, with Pure at the top and
// NonDeterministicSyscall at the bottom.
static_assert(dc::tier_replaces(cl::DetSafeTier::Pure, cl::DetSafeTier::NonDeterministicSyscall));
static_assert(dc::tier_replaces(cl::DetSafeTier::Pure, cl::DetSafeTier::PhiloxRng));
static_assert(dc::tier_replaces(cl::DetSafeTier::Pure, cl::DetSafeTier::Pure));
static_assert(dc::tier_replaces(cl::DetSafeTier::PhiloxRng, cl::DetSafeTier::MonotonicClockRead));
static_assert(dc::tier_replaces(cl::DetSafeTier::MonotonicClockRead, cl::DetSafeTier::WallClockRead));

// The three downgrades that break replay determinism, and so the
// ones worth pinning: entropy in place of purity, a wall clock in
// place of a seeded generator, and a monotonic clock in place of
// purity. A monotonic clock is ordered within one run and still
// differs between runs.
static_assert(!dc::tier_replaces(cl::DetSafeTier::EntropyRead, cl::DetSafeTier::Pure));
static_assert(!dc::tier_replaces(cl::DetSafeTier::WallClockRead, cl::DetSafeTier::PhiloxRng));
static_assert(!dc::tier_replaces(cl::DetSafeTier::MonotonicClockRead, cl::DetSafeTier::Pure));
static_assert(!dc::tier_replaces(cl::DetSafeTier::NonDeterministicSyscall, cl::DetSafeTier::FilesystemMtime));

// A strictly-greater implementation would fail every one of these.
static_assert(dc::tier_replaces(cl::DetSafeTier::NonDeterministicSyscall, cl::DetSafeTier::NonDeterministicSyscall));
static_assert(dc::tier_replaces(cl::DetSafeTier::FilesystemMtime, cl::DetSafeTier::FilesystemMtime));
static_assert(dc::tier_replaces(cl::DetSafeTier::EntropyRead, cl::DetSafeTier::EntropyRead));
static_assert(dc::tier_replaces(cl::DetSafeTier::WallClockRead, cl::DetSafeTier::WallClockRead));
static_assert(dc::tier_replaces(cl::DetSafeTier::MonotonicClockRead, cl::DetSafeTier::MonotonicClockRead));
static_assert(dc::tier_replaces(cl::DetSafeTier::PhiloxRng, cl::DetSafeTier::PhiloxRng));

[[nodiscard]] constexpr bool admit_kernel_storage(cl::CipherTierTag candidate_storage,
                                                  cl::CipherTierTag required_storage) noexcept {
    CRUCIBLE_PRE(dc::tier_replaces(candidate_storage, required_storage));
    return true;
}

static_assert(admit_kernel_storage(cl::CipherTierTag::Hot, cl::CipherTierTag::Hot));
static_assert(admit_kernel_storage(cl::CipherTierTag::Hot, cl::CipherTierTag::Cold));
static_assert(admit_kernel_storage(cl::CipherTierTag::Warm, cl::CipherTierTag::Cold));

[[nodiscard]] constexpr bool admit_kernel_determinism(cl::DetSafeTier candidate, cl::DetSafeTier required) noexcept {
    CRUCIBLE_PRE(dc::tier_replaces(candidate, required));
    return true;
}

static_assert(admit_kernel_determinism(cl::DetSafeTier::Pure, cl::DetSafeTier::PhiloxRng));
static_assert(admit_kernel_determinism(cl::DetSafeTier::Pure, cl::DetSafeTier::Pure));
static_assert(admit_kernel_determinism(cl::DetSafeTier::PhiloxRng, cl::DetSafeTier::WallClockRead));

static_assert(dc::implies(true, true));
static_assert(!dc::implies(true, false));
static_assert(dc::implies(false, true));
static_assert(dc::implies(false, false));

// Each assertion below pins one material-implication identity.
//
//   1. implies(p, true) is true for every p.
//   2. implies(false, q) is true for every q.
//   3. !implies(p, q) holds exactly when p holds and q does not.
//   4. implies(p, q) equals !p || q.
//   5. Modus ponens: implies(p, q) together with p gives q.
static_assert(dc::implies(true, true) && dc::implies(false, true));  // (1)
static_assert(dc::implies(false, true) && dc::implies(false, false));  // (2)
static_assert((!dc::implies(true, false)) == (true && !false));  // (3)

// (4)
static_assert(dc::implies(true, false) == (!true || false));
static_assert(dc::implies(false, true) == (!false || true));

// (5) Encoded as "if the implication and the antecedent both hold,
//     the consequent holds", which is why the assertions read as a
//     negated conjunction against a constant.
static_assert(!(dc::implies(true, true) && true) || true);
static_assert(!(dc::implies(true, false) && true) || false);

[[nodiscard]] constexpr bool admit_when(bool guard, bool inner) noexcept {
    CRUCIBLE_PRE(dc::implies(guard, inner));
    return true;
}
static_assert(admit_when(true, true));
static_assert(admit_when(false, true));
static_assert(admit_when(false, false));

static_assert(dc::in_range(7, 7, 7));
static_assert(!dc::in_range(6, 7, 7));
static_assert(!dc::in_range(8, 7, 7));

static_assert(dc::in_range(0, 0, 10));
static_assert(dc::in_range(10, 0, 10));
static_assert(!dc::in_range(11, 0, 10));
// The under-low witness shifts the interval to [1, 10] rather than
// testing x = -1, because -1 wraps for unsigned T.
static_assert(!dc::in_range(0, 1, 10));

static_assert(dc::in_range<std::uint8_t>(64, 0, 255));
static_assert(dc::in_range<std::uint16_t>(1024, 0, 65535));
static_assert(dc::in_range<std::uint32_t>(0xCAFEBABE, 0, 0xFFFFFFFF));
static_assert(dc::in_range<std::uint64_t>(0xDEADBEEFCAFEBABEULL, 0ULL, std::numeric_limits<std::uint64_t>::max()));

static_assert(dc::in_range<std::int32_t>(0, -100, 100));
static_assert(dc::in_range<std::int32_t>(-100, -100, 100));
static_assert(dc::in_range<std::int32_t>(100, -100, 100));
static_assert(!dc::in_range<std::int32_t>(-101, -100, 100));
static_assert(!dc::in_range<std::int32_t>(101, -100, 100));

static_assert(!dc::in_range(5, 10, 0));
static_assert(!dc::in_range(10, 10, 0));  // x == lo, but lo > hi
static_assert(!dc::in_range(0, 10, 0));  // x == hi, but lo > hi

static_assert(dc::in_range<std::uint64_t>(0ULL, 0ULL, std::numeric_limits<std::uint64_t>::max()));
static_assert(dc::in_range<std::uint64_t>(std::numeric_limits<std::uint64_t>::max(), 0ULL,
                                          std::numeric_limits<std::uint64_t>::max()));

template <std::size_t Capacity>
[[nodiscard]] constexpr bool admit_index(std::size_t i) noexcept {
    static_assert(Capacity > 0, "test setup invariant: cap > 0");
    CRUCIBLE_PRE(dc::in_range(i, std::size_t{0}, Capacity - 1));
    return true;
}
static_assert(admit_index<8>(0));
static_assert(admit_index<8>(7));
static_assert(admit_index<8>(4));

static_assert(!dc::is_non_zero(std::uint8_t{0}));
static_assert(!dc::is_non_zero(std::uint16_t{0}));
static_assert(!dc::is_non_zero(std::uint32_t{0}));
static_assert(!dc::is_non_zero(std::uint64_t{0}));
static_assert(dc::is_non_zero(std::uint8_t{1}));
static_assert(dc::is_non_zero(std::uint64_t{0xCAFEBABEDEADBEEFULL}));
static_assert(dc::is_non_zero(std::numeric_limits<std::uint32_t>::max()));

static_assert(!dc::is_non_zero(std::int8_t{0}));
static_assert(!dc::is_non_zero(std::int32_t{0}));
static_assert(dc::is_non_zero(std::int8_t{-1}));
static_assert(dc::is_non_zero(std::int32_t{-1}));
static_assert(dc::is_non_zero(std::int64_t{1}));
static_assert(dc::is_non_zero(std::numeric_limits<std::int32_t>::min()));
static_assert(dc::is_non_zero(std::numeric_limits<std::int64_t>::max()));

// A local stand-in for the two-word identifier type the predicate is
// cited on, defined here so that the test takes no dependency on it.
struct AggregateZero {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
    auto operator<=>(AggregateZero const&) const noexcept = default;
};

static_assert(!dc::is_non_zero(AggregateZero{}));
static_assert(!dc::is_non_zero(AggregateZero{0, 0}));
static_assert(dc::is_non_zero(AggregateZero{0, 1}));
static_assert(dc::is_non_zero(AggregateZero{1, 0}));
static_assert(dc::is_non_zero(AggregateZero{0xDEADBEEF, 0xCAFEBABE}));

[[nodiscard]] constexpr std::uint64_t hash_aggregate(AggregateZero const& a) noexcept {
    CRUCIBLE_PRE(dc::is_non_zero(a));
    return a.hi ^ a.lo;
}

static_assert(hash_aggregate(AggregateZero{1, 2}) == (1u ^ 2u));
static_assert(hash_aggregate(AggregateZero{0xDEADBEEF, 0}) == 0xDEADBEEFu);

// A local stand-in for the strong hash types the predicate is cited on
// (crucible/Types.h, CRUCIBLE_STRONG_HASH): zero-initialized, a
// reserved all-ones sentinel, and the is_sentinel query the predicate
// requires.  Defined here so that the test takes no dependency on the
// chain header.
struct StrongHash {
private:
    std::uint64_t v;

public:
    constexpr StrongHash() noexcept : v(0) {}
    constexpr explicit StrongHash(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] static constexpr StrongHash sentinel() noexcept { return StrongHash{UINT64_MAX}; }
    [[nodiscard]] constexpr bool is_sentinel() const noexcept { return v == UINT64_MAX; }
    constexpr auto operator<=>(const StrongHash&) const noexcept = default;
};

static_assert(!dc::not_sentinel_hash(StrongHash::sentinel()));
static_assert(dc::not_sentinel_hash(StrongHash{}));
static_assert(dc::not_sentinel_hash(StrongHash{0xDEADBEEFULL}));

// A default-constructed hash is zero, which is not the sentinel.
// Excluding zero is the job of the companion non-zero test below.
static_assert(dc::not_sentinel_hash(StrongHash{1ULL}));
static_assert(dc::not_sentinel_hash(StrongHash{0xCAFEBABEULL}));
static_assert(dc::not_sentinel_hash(StrongHash{0xFEEDFACEULL}));
static_assert(dc::not_sentinel_hash(StrongHash{StrongHash::sentinel().raw() - 1}));

// A usable cache key has to be both non-zero and not the sentinel.
// Neither test alone suffices.
[[nodiscard]] constexpr bool is_admissible_cache_key(StrongHash const& h) noexcept {
    return dc::is_non_zero(h) && dc::not_sentinel_hash(h);
}

// zero: the non-zero test rejects
static_assert(!is_admissible_cache_key(StrongHash{}));
// sentinel: the sentinel test rejects
static_assert(!is_admissible_cache_key(StrongHash::sentinel()));
// neither: both tests pass
static_assert(is_admissible_cache_key(StrongHash{1ULL}));
static_assert(is_admissible_cache_key(StrongHash{0xCAFEBABEULL}));
static_assert(is_admissible_cache_key(StrongHash{StrongHash::sentinel().raw() - 1}));

static_assert(!dc::positive(std::uint8_t{0}));
static_assert(!dc::positive(std::uint16_t{0}));
static_assert(!dc::positive(std::uint32_t{0}));
static_assert(!dc::positive(std::uint64_t{0}));
static_assert(dc::positive(std::uint8_t{1}));
static_assert(dc::positive(std::uint32_t{42}));
static_assert(dc::positive(std::uint64_t{0xCAFEBABEDEADBEEFULL}));
static_assert(dc::positive(std::numeric_limits<std::uint8_t>::max()));
static_assert(dc::positive(std::numeric_limits<std::uint64_t>::max()));

// The negative witnesses are what separate this predicate from the
// non-zero test, which admits them.
static_assert(!dc::positive(std::int8_t{0}));
static_assert(!dc::positive(std::int32_t{0}));
static_assert(!dc::positive(std::int64_t{0}));
static_assert(!dc::positive(std::int8_t{-1}));
static_assert(!dc::positive(std::int32_t{-1}));
static_assert(!dc::positive(std::int64_t{-1}));
static_assert(!dc::positive(std::numeric_limits<std::int8_t>::min()));
static_assert(!dc::positive(std::numeric_limits<std::int32_t>::min()));
static_assert(!dc::positive(std::numeric_limits<std::int64_t>::min()));
static_assert(dc::positive(std::int8_t{1}));
static_assert(dc::positive(std::int32_t{1}));
static_assert(dc::positive(std::int64_t{1}));
static_assert(dc::positive(std::numeric_limits<std::int8_t>::max()));
static_assert(dc::positive(std::numeric_limits<std::int64_t>::max()));

[[nodiscard]] constexpr std::size_t safe_alloc_size(std::size_t n) noexcept {
    CRUCIBLE_PRE(dc::positive(n));
    return n * sizeof(int);
}

static_assert(safe_alloc_size(1) == sizeof(int));
static_assert(safe_alloc_size(64) == 64 * sizeof(int));
static_assert(safe_alloc_size(1024) == 1024 * sizeof(int));

// Tautological for unsigned T. The witnesses are here so that any
// change which stopped it being tautological would be caught.
static_assert(dc::non_negative(std::uint8_t{0}));
static_assert(dc::non_negative(std::uint16_t{0}));
static_assert(dc::non_negative(std::uint32_t{0}));
static_assert(dc::non_negative(std::uint64_t{0}));
static_assert(dc::non_negative(std::uint8_t{1}));
static_assert(dc::non_negative(std::uint32_t{42}));
static_assert(dc::non_negative(std::uint64_t{0xCAFEBABEDEADBEEFULL}));
static_assert(dc::non_negative(std::numeric_limits<std::uint8_t>::max()));
static_assert(dc::non_negative(std::numeric_limits<std::uint64_t>::max()));

// Zero separates this predicate from the strictly-positive test, and
// the negatives separate it from the non-zero test.
static_assert(dc::non_negative(std::int8_t{0}));
static_assert(dc::non_negative(std::int32_t{0}));
static_assert(dc::non_negative(std::int64_t{0}));
static_assert(!dc::non_negative(std::int8_t{-1}));
static_assert(!dc::non_negative(std::int32_t{-1}));
static_assert(!dc::non_negative(std::int64_t{-1}));
static_assert(!dc::non_negative(std::numeric_limits<std::int8_t>::min()));
static_assert(!dc::non_negative(std::numeric_limits<std::int32_t>::min()));
static_assert(!dc::non_negative(std::numeric_limits<std::int64_t>::min()));
static_assert(dc::non_negative(std::int8_t{1}));
static_assert(dc::non_negative(std::int32_t{1}));
static_assert(dc::non_negative(std::int64_t{1}));
static_assert(dc::non_negative(std::numeric_limits<std::int8_t>::max()));
static_assert(dc::non_negative(std::numeric_limits<std::int32_t>::max()));
static_assert(dc::non_negative(std::numeric_limits<std::int64_t>::max()));

[[nodiscard]] constexpr std::int32_t safe_count_to_size(std::int32_t count) noexcept {
    CRUCIBLE_PRE(dc::non_negative(count));
    return count + 7;
}

static_assert(safe_count_to_size(0) == 7);
static_assert(safe_count_to_size(1) == 8);
static_assert(safe_count_to_size(1024) == 1031);
static_assert(safe_count_to_size(std::numeric_limits<std::int32_t>::max() - 7)
              == std::numeric_limits<std::int32_t>::max());

// A constexpr global gives an address that is usable as a non-null
// witness inside consteval.
constexpr int valid_span_witness_storage = 0;
constexpr const int* valid_span_witness_real_ptr = &valid_span_witness_storage;

static_assert(dc::valid_span(std::uint32_t{0}, nullptr));
static_assert(dc::valid_span(std::uint32_t{0}, valid_span_witness_real_ptr));
static_assert(dc::valid_span(std::size_t{0}, nullptr));
static_assert(dc::valid_span(std::int32_t{0}, nullptr));
static_assert(dc::valid_span(std::uint8_t{0}, nullptr));

static_assert(!dc::valid_span(std::uint32_t{1}, nullptr));
static_assert(!dc::valid_span(std::uint32_t{5}, nullptr));
// A low byte of zero catches a count truncated to a narrower type.
static_assert(!dc::valid_span(std::uint32_t{256}, nullptr));
static_assert(!dc::valid_span(std::size_t{1024}, nullptr));
static_assert(!dc::valid_span(std::int32_t{1}, nullptr));
static_assert(dc::valid_span(std::uint32_t{1}, valid_span_witness_real_ptr));
static_assert(dc::valid_span(std::uint32_t{42}, valid_span_witness_real_ptr));
static_assert(dc::valid_span(std::size_t{4096}, valid_span_witness_real_ptr));

static_assert(dc::valid_span(std::numeric_limits<std::uint32_t>::max(), valid_span_witness_real_ptr));
static_assert(!dc::valid_span(std::numeric_limits<std::uint32_t>::max(), nullptr));

[[nodiscard]] constexpr std::uint32_t safe_drain(int* out, std::uint32_t max_count) noexcept {
    CRUCIBLE_PRE(dc::valid_span(max_count, out));
    return max_count == 0u ? 0u : max_count;
}

static_assert(safe_drain(nullptr, 0u) == 0u);
static_assert(safe_drain(const_cast<int*>(valid_span_witness_real_ptr), 0u) == 0u);
static_assert(safe_drain(const_cast<int*>(valid_span_witness_real_ptr), 5u) == 5u);
static_assert(safe_drain(const_cast<int*>(valid_span_witness_real_ptr), 1024u) == 1024u);

}  // namespace

// Every predicate above is settled at consteval. This body runs the
// same predicates on values routed through volatile sinks, so the
// optimizer cannot fold them and the separate runtime rail of the
// precondition macro is exercised. A violation aborts, and the
// non-zero exit is what the harness sees. Success prints nothing.

int main() {
    int volatile sink = 0;

    // no_overflow_sum
    uint64_t volatile sa = 1000;
    uint64_t volatile sb = 2000;
    sink += static_cast<int>(safe_add_u64(sa, sb));

    int32_t volatile si = -7;
    int32_t volatile sj = 12;
    sink += safe_add_i32(si, sj);

    if (!dc::no_overflow_sum<uint32_t>(100u, 200u)) {
        std::fprintf(stderr, "test_decide: 100u+200u flagged as overflow\n");
        return 1;
    }
    if (dc::no_overflow_sum<uint32_t>(std::numeric_limits<uint32_t>::max(), 1u)) {
        std::fprintf(stderr, "test_decide: UINT32_MAX+1 NOT flagged as overflow\n");
        return 1;
    }
    if (dc::no_overflow_sum<int32_t>(std::numeric_limits<int32_t>::min(), -1)) {
        std::fprintf(stderr, "test_decide: INT32_MIN+(-1) NOT flagged as overflow\n");
        return 1;
    }

    // is_power_of_two_le
    std::size_t volatile sa_w = 32;
    sink += static_cast<int>(safe_table_capacity(static_cast<std::size_t>(sa_w)));

    if (!dc::is_power_of_two_le<uint32_t>(16u, 64u)) {
        std::fprintf(stderr, "test_decide: 16 ≤ 64 (power of 2) WRONGLY rejected\n");
        return 1;
    }
    if (dc::is_power_of_two_le<uint32_t>(48u, 64u)) {
        std::fprintf(stderr, "test_decide: 48 (= 32+16, not pow2) WRONGLY accepted\n");
        return 1;
    }
    if (dc::is_power_of_two_le<uint32_t>(128u, 64u)) {
        std::fprintf(stderr, "test_decide: 128 > 64 WRONGLY accepted\n");
        return 1;
    }
    if (dc::is_power_of_two_le<uint32_t>(0u, 64u)) {
        std::fprintf(stderr, "test_decide: 0 WRONGLY accepted as power of 2\n");
        return 1;
    }
    if (dc::is_power_of_two_le<int32_t>(-1, 64)) {
        std::fprintf(stderr, "test_decide: -1 WRONGLY accepted as power of 2\n");
        return 1;
    }
    if (dc::is_power_of_two_le<int32_t>(std::numeric_limits<int32_t>::min(), 64)) {
        std::fprintf(stderr, "test_decide: INT_MIN WRONGLY accepted (UB-on-x-1 risk)\n");
        return 1;
    }

    // tier_replaces
    {
        std::uint8_t volatile cold_v = 0;  // CipherTierTag::Cold
        std::uint8_t volatile warm_v = 1;
        std::uint8_t volatile hot_v = 2;
        auto C = static_cast<cl::CipherTierTag>(cold_v);
        auto W = static_cast<cl::CipherTierTag>(warm_v);
        auto H = static_cast<cl::CipherTierTag>(hot_v);

        if (!dc::tier_replaces(H, C)) {
            std::fprintf(stderr, "test_decide: tier_replaces Hot→Cold WRONGLY rejected\n");
            return 1;
        }
        if (!dc::tier_replaces(H, H)) {
            std::fprintf(stderr, "test_decide: tier_replaces Hot→Hot reflexivity broken\n");
            return 1;
        }
        if (dc::tier_replaces(C, H)) {
            std::fprintf(stderr, "test_decide: tier_replaces Cold→Hot downgrade WRONGLY accepted\n");
            return 1;
        }
        if (dc::tier_replaces(W, H)) {
            std::fprintf(stderr, "test_decide: tier_replaces Warm→Hot adjacent-downgrade WRONGLY accepted\n");
            return 1;
        }
        sink += static_cast<int>(cold_v) + static_cast<int>(warm_v) + static_cast<int>(hot_v);
    }
    {
        std::uint8_t volatile pure_v = 6;  // DetSafeTier::Pure
        std::uint8_t volatile entropy_v = 2;  // DetSafeTier::EntropyRead
        auto P = static_cast<cl::DetSafeTier>(pure_v);
        auto E = static_cast<cl::DetSafeTier>(entropy_v);

        if (!dc::tier_replaces(P, E)) {
            std::fprintf(stderr, "test_decide: tier_replaces Pure→EntropyRead WRONGLY rejected\n");
            return 1;
        }
        if (dc::tier_replaces(E, P)) {
            std::fprintf(stderr, "test_decide: tier_replaces EntropyRead→Pure downgrade "
                                 "WRONGLY accepted (replay-determinism violator)\n");
            return 1;
        }
        sink += static_cast<int>(pure_v) - static_cast<int>(entropy_v);
    }

    // implies
    {
        volatile bool ant_t = true;
        volatile bool ant_f = false;
        volatile bool cons_t = true;
        volatile bool cons_f = false;

        if (!dc::implies(static_cast<bool>(ant_t), static_cast<bool>(cons_t))) {
            std::fprintf(stderr, "test_decide: implies(T, T) WRONGLY rejected\n");
            return 1;
        }
        if (dc::implies(static_cast<bool>(ant_t), static_cast<bool>(cons_f))) {
            std::fprintf(stderr, "test_decide: implies(T, F) WRONGLY accepted "
                                 "(always-true / wrong-direction violator)\n");
            return 1;
        }
        if (!dc::implies(static_cast<bool>(ant_f), static_cast<bool>(cons_t))) {
            std::fprintf(stderr, "test_decide: implies(F, T) WRONGLY rejected "
                                 "(IFF-instead-of-implies violator)\n");
            return 1;
        }
        if (!dc::implies(static_cast<bool>(ant_f), static_cast<bool>(cons_f))) {
            std::fprintf(stderr, "test_decide: implies(F, F) WRONGLY rejected "
                                 "(IFF-instead-of-implies violator)\n");
            return 1;
        }

        sink += static_cast<int>(dc::implies(static_cast<bool>(ant_t), static_cast<bool>(cons_t)))
              - static_cast<int>(dc::implies(static_cast<bool>(ant_t), static_cast<bool>(cons_f)));
    }

    // in_range
    {
        volatile std::uint32_t x_lo = 0;
        volatile std::uint32_t x_hi = 10;
        volatile std::uint32_t x_mid = 5;
        volatile std::uint32_t x_above = 11;
        volatile std::uint32_t lo = 0;
        volatile std::uint32_t hi = 10;

        if (!dc::in_range<std::uint32_t>(static_cast<std::uint32_t>(x_lo), static_cast<std::uint32_t>(lo),
                                         static_cast<std::uint32_t>(hi))) {
            std::fprintf(stderr, "test_decide: in_range(0, 0, 10) WRONGLY rejected "
                                 "(closed lower endpoint)\n");
            return 1;
        }
        if (!dc::in_range<std::uint32_t>(static_cast<std::uint32_t>(x_hi), static_cast<std::uint32_t>(lo),
                                         static_cast<std::uint32_t>(hi))) {
            std::fprintf(stderr, "test_decide: in_range(10, 0, 10) WRONGLY rejected "
                                 "(closed upper endpoint)\n");
            return 1;
        }
        if (!dc::in_range<std::uint32_t>(static_cast<std::uint32_t>(x_mid), static_cast<std::uint32_t>(lo),
                                         static_cast<std::uint32_t>(hi))) {
            std::fprintf(stderr, "test_decide: in_range(5, 0, 10) WRONGLY rejected\n");
            return 1;
        }
        if (dc::in_range<std::uint32_t>(static_cast<std::uint32_t>(x_above), static_cast<std::uint32_t>(lo),
                                        static_cast<std::uint32_t>(hi))) {
            std::fprintf(stderr, "test_decide: in_range(11, 0, 10) WRONGLY accepted "
                                 "(above-high violator)\n");
            return 1;
        }

        volatile std::uint32_t lo_one = 1;
        if (dc::in_range<std::uint32_t>(static_cast<std::uint32_t>(x_lo),  // 0
                                        static_cast<std::uint32_t>(lo_one), static_cast<std::uint32_t>(hi))) {
            std::fprintf(stderr, "test_decide: in_range(0, 1, 10) WRONGLY accepted "
                                 "(below-low violator)\n");
            return 1;
        }

        volatile std::uint32_t lo_inv = 10;
        volatile std::uint32_t hi_inv = 0;
        if (dc::in_range<std::uint32_t>(static_cast<std::uint32_t>(x_mid), static_cast<std::uint32_t>(lo_inv),
                                        static_cast<std::uint32_t>(hi_inv))) {
            std::fprintf(stderr, "test_decide: in_range(5, 10, 0) WRONGLY accepted "
                                 "(empty interval admits)\n");
            return 1;
        }

        sink += static_cast<int>(dc::in_range<std::uint32_t>(
                    static_cast<std::uint32_t>(x_mid), static_cast<std::uint32_t>(lo), static_cast<std::uint32_t>(hi)))
              - static_cast<int>(dc::in_range<std::uint32_t>(
                  static_cast<std::uint32_t>(x_above), static_cast<std::uint32_t>(lo), static_cast<std::uint32_t>(hi)));
    }

    // is_non_zero
    {
        volatile std::uint64_t z_u64 = 0;
        volatile std::uint64_t nz_u64 = 0xDEADBEEFCAFEBABEULL;
        volatile std::int32_t z_i32 = 0;
        volatile std::int32_t neg_i32 = -42;
        volatile std::int32_t pos_i32 = 7;

        if (dc::is_non_zero(static_cast<std::uint64_t>(z_u64))) {
            std::fprintf(stderr, "test_decide: is_non_zero(0u64) WRONGLY accepted\n");
            return 1;
        }
        if (!dc::is_non_zero(static_cast<std::uint64_t>(nz_u64))) {
            std::fprintf(stderr, "test_decide: is_non_zero(non-zero u64) WRONGLY rejected\n");
            return 1;
        }
        if (dc::is_non_zero(static_cast<std::int32_t>(z_i32))) {
            std::fprintf(stderr, "test_decide: is_non_zero(0i32) WRONGLY accepted\n");
            return 1;
        }
        if (!dc::is_non_zero(static_cast<std::int32_t>(neg_i32))) {
            std::fprintf(stderr, "test_decide: is_non_zero(-42i32) WRONGLY rejected "
                                 "(negative is non-zero)\n");
            return 1;
        }
        if (!dc::is_non_zero(static_cast<std::int32_t>(pos_i32))) {
            std::fprintf(stderr, "test_decide: is_non_zero(7i32) WRONGLY rejected\n");
            return 1;
        }

        AggregateZero const all_zero{static_cast<std::uint64_t>(z_u64), static_cast<std::uint64_t>(z_u64)};
        AggregateZero const lo_only{static_cast<std::uint64_t>(z_u64), static_cast<std::uint64_t>(nz_u64)};
        AggregateZero const hi_only{static_cast<std::uint64_t>(nz_u64), static_cast<std::uint64_t>(z_u64)};
        AggregateZero const both_set{static_cast<std::uint64_t>(nz_u64), static_cast<std::uint64_t>(nz_u64)};

        if (dc::is_non_zero(all_zero)) {
            std::fprintf(stderr, "test_decide: is_non_zero({0,0}) WRONGLY accepted "
                                 "(structural zero must reject)\n");
            return 1;
        }
        if (!dc::is_non_zero(lo_only)) {
            std::fprintf(stderr, "test_decide: is_non_zero({0,nz}) WRONGLY rejected "
                                 "(any non-zero field implies non-zero aggregate)\n");
            return 1;
        }
        if (!dc::is_non_zero(hi_only)) {
            std::fprintf(stderr, "test_decide: is_non_zero({nz,0}) WRONGLY rejected "
                                 "(field-myopic implementation would miss this)\n");
            return 1;
        }
        if (!dc::is_non_zero(both_set)) {
            std::fprintf(stderr, "test_decide: is_non_zero({nz,nz}) WRONGLY rejected\n");
            return 1;
        }

        sink += static_cast<int>(dc::is_non_zero(both_set)) - static_cast<int>(dc::is_non_zero(all_zero));
    }

    // positive. The signed-negative branch is the only place this
    // parts company with the non-zero test.
    {
        volatile std::uint64_t z_u64 = 0;
        volatile std::uint64_t pos_u64 = 0xDEADBEEF;
        volatile std::int32_t z_i32 = 0;
        volatile std::int32_t neg_i32 = -42;
        volatile std::int32_t pos_i32 = 7;

        if (dc::positive(static_cast<std::uint64_t>(z_u64))) {
            std::fprintf(stderr, "test_decide: positive(0u64) WRONGLY accepted\n");
            return 1;
        }
        if (!dc::positive(static_cast<std::uint64_t>(pos_u64))) {
            std::fprintf(stderr, "test_decide: positive(non-zero u64) WRONGLY rejected\n");
            return 1;
        }
        if (dc::positive(static_cast<std::int32_t>(z_i32))) {
            std::fprintf(stderr, "test_decide: positive(0i32) WRONGLY accepted\n");
            return 1;
        }
        if (dc::positive(static_cast<std::int32_t>(neg_i32))) {
            std::fprintf(stderr, "test_decide: positive(-42i32) WRONGLY accepted "
                                 "(strict-positive must reject negatives)\n");
            return 1;
        }
        if (!dc::positive(static_cast<std::int32_t>(pos_i32))) {
            std::fprintf(stderr, "test_decide: positive(7i32) WRONGLY rejected\n");
            return 1;
        }

        sink += static_cast<int>(dc::positive(static_cast<std::int32_t>(pos_i32)))
              - static_cast<int>(dc::positive(static_cast<std::int32_t>(neg_i32)));
    }

    // non_negative. A negative rules out the non-zero test, and
    // INT_MIN additionally rules out an absolute-value implementation.
    {
        volatile std::uint64_t z_u64_nn = 0;
        volatile std::uint64_t pos_u64_nn = 0xDEADBEEF;
        volatile std::int32_t z_i32_nn = 0;
        volatile std::int32_t neg_i32_nn = -42;
        volatile std::int32_t pos_i32_nn = 7;
        volatile std::int32_t int_min_i32 = std::numeric_limits<std::int32_t>::min();

        if (!dc::non_negative(static_cast<std::uint64_t>(z_u64_nn))) {
            std::fprintf(stderr, "test_decide: non_negative(0u64) WRONGLY rejected "
                                 "(unsigned tautology violation)\n");
            return 1;
        }
        if (!dc::non_negative(static_cast<std::uint64_t>(pos_u64_nn))) {
            std::fprintf(stderr, "test_decide: non_negative(non-zero u64) WRONGLY rejected\n");
            return 1;
        }

        if (!dc::non_negative(static_cast<std::int32_t>(z_i32_nn))) {
            std::fprintf(stderr, "test_decide: non_negative(0i32) WRONGLY rejected "
                                 "(zero-inclusive discipline violation)\n");
            return 1;
        }

        if (!dc::non_negative(static_cast<std::int32_t>(pos_i32_nn))) {
            std::fprintf(stderr, "test_decide: non_negative(7i32) WRONGLY rejected\n");
            return 1;
        }

        if (dc::non_negative(static_cast<std::int32_t>(neg_i32_nn))) {
            std::fprintf(stderr, "test_decide: non_negative(-42i32) WRONGLY accepted "
                                 "(must reject signed-negative)\n");
            return 1;
        }

        // std::abs(INT_MIN) is undefined and commonly returns
        // INT_MIN, so an absolute-value implementation admits this.
        if (dc::non_negative(static_cast<std::int32_t>(int_min_i32))) {
            std::fprintf(stderr, "test_decide: non_negative(INT_MIN) WRONGLY accepted "
                                 "(boundary signed-negative must be rejected)\n");
            return 1;
        }

        sink += static_cast<int>(dc::non_negative(static_cast<std::int32_t>(pos_i32_nn)))
              + static_cast<int>(dc::non_negative(static_cast<std::int32_t>(z_i32_nn)))
              - static_cast<int>(dc::non_negative(static_cast<std::int32_t>(neg_i32_nn)))
              - static_cast<int>(dc::non_negative(static_cast<std::int32_t>(int_min_i32)));

        std::int32_t volatile cnt_nn = 5;
        sink += safe_count_to_size(static_cast<std::int32_t>(cnt_nn));
    }

    // valid_span

    {
        int storage_vs = 42;
        int* volatile real_ptr_vs = &storage_vs;
        int* volatile null_ptr_vs = nullptr;

        std::uint32_t volatile zero_u32_vs = 0u;
        std::uint32_t volatile pos_u32_vs = 5u;
        std::uint32_t volatile big_u32_vs = 256u;
        std::size_t volatile pos_sz_vs = 1024u;

        if (!dc::valid_span(static_cast<std::uint32_t>(zero_u32_vs), static_cast<const void*>(real_ptr_vs))) {
            std::fprintf(stderr, "test_decide: valid_span(0u, &storage) WRONGLY rejected "
                                 "(empty span with real ptr is the canonical fast path)\n");
            return 1;
        }
        if (!dc::valid_span(static_cast<std::uint32_t>(zero_u32_vs), static_cast<const void*>(null_ptr_vs))) {
            std::fprintf(stderr, "test_decide: valid_span(0u, nullptr) WRONGLY rejected "
                                 "(empty span with sentinel nullptr is the canonical "
                                 "drain(nullptr, 0) idiom)\n");
            return 1;
        }

        if (!dc::valid_span(static_cast<std::uint32_t>(pos_u32_vs), static_cast<const void*>(real_ptr_vs))) {
            std::fprintf(stderr, "test_decide: valid_span(5u, &storage) WRONGLY rejected\n");
            return 1;
        }
        if (!dc::valid_span(static_cast<std::size_t>(pos_sz_vs), static_cast<const void*>(real_ptr_vs))) {
            std::fprintf(stderr, "test_decide: valid_span(1024u, &storage) WRONGLY rejected\n");
            return 1;
        }

        if (dc::valid_span(static_cast<std::uint32_t>(pos_u32_vs), static_cast<const void*>(null_ptr_vs))) {
            std::fprintf(stderr, "test_decide: valid_span(5u, nullptr) WRONGLY accepted "
                                 "(non-empty span with null ptr is unconditional UB)\n");
            return 1;
        }
        // A count truncated to a narrower type would pass this one.
        if (dc::valid_span(static_cast<std::uint32_t>(big_u32_vs), static_cast<const void*>(null_ptr_vs))) {
            std::fprintf(stderr, "test_decide: valid_span(256u, nullptr) WRONGLY accepted "
                                 "(low-byte-zero magnitude must reject — catches truncation "
                                 "bugs that small-magnitude witnesses cannot)\n");
            return 1;
        }

        sink += static_cast<int>(
                    dc::valid_span(static_cast<std::uint32_t>(zero_u32_vs), static_cast<const void*>(null_ptr_vs)))
              + static_cast<int>(
                    dc::valid_span(static_cast<std::uint32_t>(pos_u32_vs), static_cast<const void*>(real_ptr_vs)))
              - static_cast<int>(
                    dc::valid_span(static_cast<std::uint32_t>(pos_u32_vs), static_cast<const void*>(null_ptr_vs)))
              - static_cast<int>(
                    dc::valid_span(static_cast<std::uint32_t>(big_u32_vs), static_cast<const void*>(null_ptr_vs)));

        std::uint32_t volatile cnt_vs = 7u;
        sink += static_cast<int>(safe_drain(real_ptr_vs, static_cast<std::uint32_t>(cnt_vs)));
    }

    if (sink == 0) {
        std::fprintf(stderr, "test_decide: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}
