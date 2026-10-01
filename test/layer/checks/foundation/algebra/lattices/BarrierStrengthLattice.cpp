// The compile-time checks of foundation/algebra/lattices/BarrierStrengthLattice.h.

#include <foundation/algebra/lattices/BarrierStrengthLattice.h>

namespace foundation::algebra::lattices {

namespace detail::barrier_strength_lattice_self_test {

static_assert(::foundation::reflect::enum_count<BarrierStrength> == 7,
              "BarrierStrength diverged from {None, CompilerBarrier, AcquireLoad, ReleaseStore, AcqRel, SeqCst, "
              "FullFence}.  A new tier appends at the next free ordinal.");

static_assert(std::to_underlying(BarrierStrength::None) == 0);

static_assert(std::to_underlying(BarrierStrength::FullFence) == 6);

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>);

static_assert(Lattice<BarrierStrengthLattice>);
static_assert(BoundedLattice<BarrierStrengthLattice>);

// The lattice laws at every triple of the seven strengths, and leq, join
// and meet in agreement at every pair: reflexive, antisymmetric and
// transitive, and join and meet the least upper and greatest lower
// bounds.  The walk reads the enumerators by reflection.
static_assert(verify_enum_lattice_exhaustive<BarrierStrengthLattice>(),
              "BarrierStrengthLattice: a partial-order or bound law fails at some triple of strengths.");

// A chain with one diamond is distributive.  The law holds at every
// triple, so a later strength that breaks it stops the build here.
static_assert(verify_chain_lattice_distributive_exhaustive<BarrierStrengthLattice>(),
              "BarrierStrengthLattice: the distributive law fails at some triple of strengths.");

static_assert(verify_pinned_at<BarrierStrengthLattice>(),
              "BarrierStrengthLattice::At<K>: a pinned grade lost its emptiness, its conversion back to K, or its "
              "reflected name.");

// The incomparable pairs, derived from the order rather than listed.  The
// only pair is {AcquireLoad, ReleaseStore}, which join and meet above
// depend on.  A new strength at an existing height adds a pair, and this
// assertion names the fault before join and meet can give a wrong bound.
[[nodiscard]] consteval std::size_t incomparable_pairs() noexcept {
    std::size_t pairs = 0;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^BarrierStrength));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto first : enumerators) {
        template for (constexpr auto second : enumerators) {
            constexpr BarrierStrength a = [:first:];
            constexpr BarrierStrength b = [:second:];
            if (std::to_underlying(a) < std::to_underlying(b) && !BarrierStrengthLattice::leq(a, b)
                && !BarrierStrengthLattice::leq(b, a)) {
                ++pairs;
            }
        }
    }
#pragma GCC diagnostic pop
    return pairs;
}
static_assert(incomparable_pairs() == 1,
              "BarrierStrengthLattice: the order must have exactly one incomparable pair, AcquireLoad and "
              "ReleaseStore.  join and meet name AcqRel and CompilerBarrier as its bounds.");
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore)
                  && !BarrierStrengthLattice::leq(BarrierStrength::ReleaseStore, BarrierStrength::AcquireLoad),
              "An acquire load orders the operations after it and a release store the operations before it.  "
              "Neither satisfies a requirement for the other.");
static_assert(BarrierStrengthLattice::join(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore)
                  == BarrierStrength::AcqRel,
              "acq_rel is both an acquire and a release, so it is the least strength that satisfies both.");
static_assert(BarrierStrengthLattice::meet(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore)
              == BarrierStrength::CompilerBarrier);

static_assert(!::foundation::algebra::Semiring<BarrierStrengthLattice>);

static_assert(BarrierStrengthLattice::bottom() == BarrierStrength::None);
static_assert(BarrierStrengthLattice::top() == BarrierStrength::FullFence);

static_assert(BarrierStrengthLattice::name() == std::string_view{"BarrierStrengthLattice"});

static_assert(BarrierStrengthLattice::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst),
              "A SeqCst fence satisfies an AcqRel requirement.  leq(required, "
              "provided) is the admission direction.");
static_assert(!BarrierStrengthLattice::leq(BarrierStrength::SeqCst, BarrierStrength::AcqRel),
              "An AcqRel fence does not satisfy a SeqCst requirement.");

static_assert(BarrierStrengthLattice::join(BarrierStrength::None, BarrierStrength::FullFence)
                  == BarrierStrength::FullFence,
              "join gives the strictest-wins reading on this chain, because the "
              "top is FullFence.  join(None, FullFence) returns FullFence, the "
              "stronger of the two.");
static_assert(BarrierStrengthLattice::meet(BarrierStrength::None, BarrierStrength::FullFence) == BarrierStrength::None,
              "meet gives the weakest floor, because the bottom is None.  An "
              "admission gate that grants only what every participant provides "
              "calls meet.");

static_assert(BarrierStrengthLattice::At<BarrierStrength::SeqCst>::tier == BarrierStrength::SeqCst);
static_assert(BarrierStrengthLattice::At<BarrierStrength::SeqCst>::name() == "BarrierStrengthLattice::At<SeqCst>");
static_assert(BarrierStrengthLattice::At<static_cast<BarrierStrength>(255)>::name() == "BarrierStrengthLattice::At<?>");

}  // namespace detail::barrier_strength_lattice_self_test

}  // namespace foundation::algebra::lattices
