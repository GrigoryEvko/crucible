// Sentinel TU for the strong-counter lattices and the vector clock.  The
// headers carry their own static assertions; this file makes them compile
// and runs each operation on operands the optimizer cannot see, which is
// what catches a body that only ever instantiates in a constant expression.
//
// It also pins the row-hash identity of each axis: four axes that are the
// same lattice under four tags must take four slots, or a value graded on
// the epoch would share a cache entry with one graded on the generation.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/diag/RowHash.h>

#include <array>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fd = ::foundation::diag;

template <typename L>
using OnAxis = fa::Graded<fa::ModalityKind::Absolute, L, int>;

constexpr std::array<std::uint64_t, 4> kAxisIdentities = {
    fd::lattice_canonical_id_v<fl::EpochLattice>,
    fd::lattice_canonical_id_v<fl::GenerationLattice>,
    fd::lattice_canonical_id_v<fl::PeakBytesLattice>,
    fd::lattice_canonical_id_v<fl::BitsBudgetLattice>,
};

constexpr std::array<std::uint64_t, 4> kCarrierHashes = {
    fd::row_hash_contribution_v<OnAxis<fl::EpochLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::GenerationLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::PeakBytesLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::BitsBudgetLattice>>,
};

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

// Two clocks of one width and different tags are two protocols, so they
// must take two slots too.
struct ReplayTag {};
struct KernelTag {};
static_assert(fd::row_hash_contribution_v<OnAxis<fl::HappensBeforeLattice<4, ReplayTag>>>
              != fd::row_hash_contribution_v<OnAxis<fl::HappensBeforeLattice<4, KernelTag>>>);
static_assert(fd::row_hash_contribution_v<OnAxis<fl::HappensBeforeLattice<4, ReplayTag>>> != 0);

// The value is read at run time, so the compiler cannot fold the calls.
volatile std::uint64_t g_runtime_seed = 5;

[[noreturn]] void fail(char const* what) {
    std::fprintf(stderr, "test_lattices_counters: %s\n", what);
    std::abort();
}

template <typename L>
void exercise_counter(char const* name) {
    using E = typename L::element_type;
    E const low{g_runtime_seed};
    E const high{g_runtime_seed + 10};
    if (!L::leq(low, high) || L::leq(high, low)) fail(name);
    if (!(L::join(low, high) == high) || !(L::meet(low, high) == low)) fail(name);
    if (!(L::join(low, L::bottom()) == low) || !(L::meet(high, L::top()) == high)) fail(name);
    if (!L::leq(L::bottom(), low) || !L::leq(high, L::top())) fail(name);
    if ((low <=> high) != std::strong_ordering::less) fail(name);
    if (L::top().raw() != std::numeric_limits<std::uint64_t>::max()) fail(name);
    if (!(L::successor(low) == E{g_runtime_seed + 1}) || !L::leq(low, L::successor(low))) fail(name);

    OnAxis<L> const carried{7, high};
    if (!(carried.grade() == high) || carried.peek() != 7) fail(name);
}

void exercise_happens_before() {
    using HB = fl::HappensBeforeLattice<4, ReplayTag>;
    std::uint64_t const s = g_runtime_seed;
    HB::element_type const a = fl::make_clock<HB>(s, 0u, 0u, 0u);
    HB::element_type const b = HB::successor_at(a, 1);
    HB::element_type const x = fl::make_clock<HB>(s + 1, 0u, 1u, 0u);
    HB::element_type const y = fl::make_clock<HB>(0u, s + 1, 0u, 1u);

    if (!HB::happens_before(a, b) || HB::happens_before(b, a)) fail("happens_before on a chain");
    if (!HB::is_concurrent(x, y) || HB::comparable(x, y)) fail("is_concurrent on an antichain");
    if ((x <=> y) != std::partial_ordering::unordered) fail("operator<=> on an antichain");
    if (b[1] != 1 || b[0] != s) fail("slot reader");

    HB::element_type const merged = HB::causal_merge(a, y, 0);
    if (!HB::happens_before(a, merged) || !HB::happens_before(y, merged)) fail("causal_merge");
    if (merged[0] != s + 1) fail("causal_merge counts the receive");

    using HB1 = fl::HappensBeforeLattice<1>;
    HB1::element_type const one{{s}};
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
    std::printf("test_lattices_counters: ok\n");
    return 0;
}
