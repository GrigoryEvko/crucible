// Sentinel TU for the strong-counter lattices and the vector clock.  The
// headers carry their own static assertions; this file makes them compile
// and runs each operation on operands the optimizer cannot see, which is
// what catches a body that only ever instantiates in a constant expression.
//
// It also pins the row-hash identity of each axis: four axes that are the
// same lattice under four tags must take four slots, or a value graded on
// the epoch would share a cache entry with one graded on the generation.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include "../test_assert.h"

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fd = ::foundation::diag;

template <typename L>
using OnAxis = fa::Graded<fa::ModalityKind::Absolute, L, int>;

// The authority for the carriers built below.  It hands its key out,
// which an authority in production code never does.
struct test_authority {
    [[nodiscard]] static constexpr fa::grade_key<test_authority> key() noexcept {
        return fa::grade_key<test_authority>{};
    }
};

// The lattice Graded accepts for an axis: the axis itself when its up is
// the weaker claim, and its order dual when its up is the stronger one.
template <typename L>
using GradedWay = std::conditional_t<fa::GradableLattice<L>, L, fl::DualLattice<L>>;

template <typename L>
using OnAxisGradedWay = OnAxis<GradedWay<L>>;

constexpr std::array<std::uint64_t, 4> kAxisIdentities = {
    fd::lattice_canonical_id_v<fl::EpochLattice>,
    fd::lattice_canonical_id_v<fl::GenerationLattice>,
    fd::lattice_canonical_id_v<fl::PeakBytesLattice>,
    fd::lattice_canonical_id_v<fl::BitsBudgetLattice>,
};

constexpr std::array<std::uint64_t, 4> kCarrierHashes = {
    fd::row_hash_contribution_v<OnAxisGradedWay<fl::EpochLattice>>,
    fd::row_hash_contribution_v<OnAxisGradedWay<fl::GenerationLattice>>,
    fd::row_hash_contribution_v<OnAxisGradedWay<fl::PeakBytesLattice>>,
    fd::row_hash_contribution_v<OnAxisGradedWay<fl::BitsBudgetLattice>>,
};

// The two version axes enter through their duals, and the two use axes
// enter as they are.
static_assert(std::is_same_v<GradedWay<fl::EpochLattice>, fl::DualLattice<fl::EpochLattice>>);
static_assert(std::is_same_v<GradedWay<fl::PeakBytesLattice>, fl::PeakBytesLattice>);

template <std::size_t N>
[[nodiscard]] consteval bool pairwise_distinct(std::array<std::uint64_t, N> const& values) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == 0) return false;
        for (std::size_t j = i + 1; j < N; ++j) {
            if (values[i] == values[j]) return false;
        }
    }
    return true;
}

static_assert(pairwise_distinct(kAxisIdentities), "two counter axes share one lattice identity");
static_assert(pairwise_distinct(kCarrierHashes), "two counter axes share one row-hash slot");

}  // namespace

// Two clocks of one width and different tags are two protocols, so they
// must take two slots too.  The tags have a name outside an unnamed
// namespace, because a stable id refuses a type with internal linkage.
namespace test_lattices_counters_types {
struct ReplayTag {};
struct KernelTag {};
}  // namespace test_lattices_counters_types

namespace {

using namespace test_lattices_counters_types;

static_assert(fd::row_hash_contribution_v<OnAxisGradedWay<fl::HappensBeforeLattice<4, ReplayTag>>>
              != fd::row_hash_contribution_v<OnAxisGradedWay<fl::HappensBeforeLattice<4, KernelTag>>>);
static_assert(fd::row_hash_contribution_v<OnAxisGradedWay<fl::HappensBeforeLattice<4, ReplayTag>>> != 0);

// The value is read at run time, so the compiler cannot fold the calls.
volatile std::uint64_t g_runtime_seed = 5;

[[noreturn]] void fail(char const* what) {
    std::fprintf(stderr, "test_lattices_counters: %s\n", what);
    std::abort();
}

namespace fe = ::foundation::effects;

// A count at a number read at run time.  No integer builds a count, so the
// test reads one through the one door that states a count from bytes, with
// a context that owns IO.
template <typename L>
typename L::element_type count_at(std::uint64_t count) {
    fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>> const ctx{fe::testing::test()};
    typename L::image_type image{};
    for (std::size_t i = 0; i < 8; ++i) {
        image[i] = static_cast<std::byte>((L::image_axis() >> (8 * i)) & 0xFFu);
        image[8 + i] = static_cast<std::byte>((count >> (8 * i)) & 0xFFu);
    }
    auto const read = L::mint_from_image(ctx, image);
    if (!read) std::abort();
    return *read;
}

// A clock reached from the empty history by local events.
template <typename HB>
typename HB::element_type clock_after(std::array<std::uint64_t, HB::process_count> const& steps) {
    typename HB::element_type clock = HB::bottom();
    for (std::size_t p = 0; p < HB::process_count; ++p) {
        for (std::uint64_t i = 0; i < steps[p]; ++i)
            clock = HB::successor_at(clock, p);
    }
    return clock;
}

template <typename L>
void exercise_counter(char const* name) {
    using E = typename L::element_type;
    E const low = count_at<L>(g_runtime_seed);
    E const high = count_at<L>(g_runtime_seed + 10);
    if (!L::leq(low, high) || L::leq(high, low)) fail(name);
    if (!(L::join(low, high) == high) || !(L::meet(low, high) == low)) fail(name);
    if (!(L::join(low, L::bottom()) == low) || !(L::meet(high, L::top()) == high)) fail(name);
    if (!L::leq(L::bottom(), low) || !L::leq(high, L::top())) fail(name);
    if ((low <=> high) != std::strong_ordering::less) fail(name);
    if (L::top().raw() != std::numeric_limits<std::uint64_t>::max()) fail(name);
    if (!(L::successor(low) == count_at<L>(g_runtime_seed + 1)) || !L::leq(low, L::successor(low))) fail(name);
    if (!L::is_at_least(high, typename L::bound_type{g_runtime_seed + 10})
        || L::is_at_least(low, typename L::bound_type{high})) {
        fail(name);
    }

    OnAxisGradedWay<L> const carried{test_authority::key(), 7, high};
    if (!(carried.grade() == high) || carried.peek() != 7) fail(name);
}

void exercise_happens_before() {
    using HB = fl::HappensBeforeLattice<4, ReplayTag>;
    std::uint64_t const s = g_runtime_seed;
    HB::element_type const a = clock_after<HB>({s, 0, 0, 0});
    HB::element_type const b = HB::successor_at(a, 1);
    HB::element_type const x = clock_after<HB>({s + 1, 0, 1, 0});
    HB::element_type const y = clock_after<HB>({0, s + 1, 0, 1});

    if (!HB::happens_before(a, b) || HB::happens_before(b, a)) fail("happens_before on a chain");
    if (!HB::is_concurrent(x, y) || HB::comparable(x, y)) fail("is_concurrent on an antichain");
    if ((x <=> y) != std::partial_ordering::unordered) fail("operator<=> on an antichain");
    if (b[1] != 1 || b[0] != s) fail("slot reader");

    HB::element_type const merged = HB::causal_merge(a, y, 0);
    if (!HB::happens_before(a, merged) || !HB::happens_before(y, merged)) fail("causal_merge");
    if (merged[0] != s + 1) fail("causal_merge counts the receive");

    using HB1 = fl::HappensBeforeLattice<1>;
    HB1::element_type const one = clock_after<HB1>({s});
    if (HB1::is_concurrent(one, HB1::successor_at(one, 0))) fail("a scalar clock is total");
}

}  // namespace

int main() {
    exercise_counter<fl::EpochLattice>("EpochLattice");
    exercise_counter<fl::GenerationLattice>("GenerationLattice");
    exercise_counter<fl::PeakBytesLattice>("PeakBytesLattice");
    exercise_counter<fl::BitsBudgetLattice>("BitsBudgetLattice");
    exercise_happens_before();

    // The identities are compile-time constants.  Reading them here proves
    // they reach a running program with the values the assertions saw.
    for (std::size_t i = 0; i < kCarrierHashes.size(); ++i) {
        if (kCarrierHashes[i] == 0) fail("a counter carrier folded to the zero slot");
    }
    crucible::test::pass("test_lattices_counters: ok\n");
    return 0;
}
