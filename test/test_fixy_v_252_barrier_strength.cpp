// This axis is separate from the memory-order axis on purpose.  That one
// models the tag on an atomic operation and runs in the inverted
// convention, with sequential consistency at the bottom.  This one is a
// hardware-fence capability ladder in the ordinary convention, bracketed
// below by values that are not memory orders at all and above by a
// standalone fence instruction.  Distinct axes, distinct gates.
//
// Acquire and release are formally incomparable.  Linearizing them into
// one strength ladder is a deliberate simplification for admission
// gating.  The formal correctness of a given fence is carried by the
// explicit barrier grant at the site, not derived from this order.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/BarrierStrengthLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;

namespace {

using cal::BarrierStrength;
using L = cal::BarrierStrengthLattice;

static_assert(crucible::algebra::Lattice<L>);
static_assert(crucible::algebra::BoundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>, "A chain order carries no independent add and multiply, so the "
                                               "lattice must not satisfy the Semiring concept.");

static_assert(cal::detail::barrier_strength_lattice_self_test::barrier_strength_count == 7,
              "BarrierStrength must have exactly 7 enumerators.");

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>);

static_assert(std::to_underlying(BarrierStrength::None) == 0, "bottom — no barrier");
static_assert(std::to_underlying(BarrierStrength::CompilerBarrier) == 1);
static_assert(std::to_underlying(BarrierStrength::AcquireLoad) == 2);
static_assert(std::to_underlying(BarrierStrength::ReleaseStore) == 3);
static_assert(std::to_underlying(BarrierStrength::AcqRel) == 4);
static_assert(std::to_underlying(BarrierStrength::SeqCst) == 5);
static_assert(std::to_underlying(BarrierStrength::FullFence) == 6, "top — standalone fence");

static_assert(L::bottom() == BarrierStrength::None);
static_assert(L::top() == BarrierStrength::FullFence);

// Every tier admits everything below it.
static_assert(L::leq(BarrierStrength::None, BarrierStrength::CompilerBarrier));
static_assert(L::leq(BarrierStrength::CompilerBarrier, BarrierStrength::AcquireLoad));
static_assert(L::leq(BarrierStrength::AcquireLoad, BarrierStrength::ReleaseStore));
static_assert(L::leq(BarrierStrength::ReleaseStore, BarrierStrength::AcqRel));
static_assert(L::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst));
static_assert(L::leq(BarrierStrength::SeqCst, BarrierStrength::FullFence));
static_assert(L::leq(BarrierStrength::None, BarrierStrength::FullFence), "transitive endpoints");

// These four are the admission decisions the chain exists to make.
static_assert(L::leq(BarrierStrength::AcqRel, BarrierStrength::SeqCst),
              "A sequentially consistent fence satisfies an acquire-release "
              "requirement, because the stronger satisfies the weaker.");
static_assert(!L::leq(BarrierStrength::SeqCst, BarrierStrength::AcqRel),
              "An acquire-release fence does not satisfy a sequentially "
              "consistent requirement.");
static_assert(L::leq(BarrierStrength::CompilerBarrier, BarrierStrength::FullFence),
              "A full fence satisfies a compiler-barrier requirement.");
static_assert(!L::leq(BarrierStrength::FullFence, BarrierStrength::None),
              "A full-fence claim is never reduced to a no-barrier requirement.");

// Join takes the stronger fence, meet the weaker.
static_assert(L::join(BarrierStrength::CompilerBarrier, BarrierStrength::SeqCst) == BarrierStrength::SeqCst,
              "Composing a compiler-barrier site with a sequentially consistent "
              "one yields the stronger of the two, since a region's fence "
              "strength is the least upper bound of its parts.");
static_assert(L::join(BarrierStrength::None, BarrierStrength::AcquireLoad) == BarrierStrength::AcquireLoad,
              "None is the join identity");
static_assert(L::join(BarrierStrength::FullFence, BarrierStrength::AcqRel) == BarrierStrength::FullFence,
              "FullFence absorbs in join");

static_assert(L::meet(BarrierStrength::FullFence, BarrierStrength::AcquireLoad) == BarrierStrength::AcquireLoad,
              "Meeting a strong binding with a weak policy yields the floor.");
static_assert(L::meet(BarrierStrength::None, BarrierStrength::SeqCst) == BarrierStrength::None, "None absorbs in meet");

static_assert(crucible::algebra::Lattice<L::At<BarrierStrength::AcqRel>>);
static_assert(crucible::algebra::BoundedLattice<L::At<BarrierStrength::FullFence>>);
static_assert(std::is_empty_v<L::At<BarrierStrength::None>::element_type>,
              "At<None>::element_type must be empty so that Graded<Absolute, "
              "At<None>, P> collapses to sizeof(P).");
static_assert(std::is_empty_v<L::At<BarrierStrength::SeqCst>::element_type>);
static_assert(std::is_empty_v<L::At<BarrierStrength::FullFence>::element_type>);
static_assert(L::At<BarrierStrength::SeqCst>::tier == BarrierStrength::SeqCst,
              "At<K>::tier must equal K at the type level, so a wrapper can read "
              "the pinned tier with no runtime data.");

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                               L::At<BarrierStrength::SeqCst>, EightByteValue>)
                  == sizeof(EightByteValue),
              "Pinning a SeqCst tier must add zero bytes to an 8-byte payload.");
static_assert(
    sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, L::At<BarrierStrength::FullFence>, int>)
    == sizeof(int));

static_assert(L::name() == std::string_view{"BarrierStrengthLattice"});
static_assert(L::At<BarrierStrength::AcqRel>::name() == std::string_view{"BarrierStrengthLattice::At<AcqRel>"});
static_assert(L::At<BarrierStrength::CompilerBarrier>::name()
              == std::string_view{"BarrierStrengthLattice::At<CompilerBarrier>"});
static_assert(cal::barrier_strength_name(BarrierStrength::FullFence) == std::string_view{"FullFence"});

}  // namespace

int main() {
    cal::detail::barrier_strength_lattice_self_test::barrier_strength_lattice_runtime_smoke_test();
    return 0;
}
