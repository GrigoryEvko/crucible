// The effect row records memory effects and cannot say which class of
// hardware instruction a kernel issues.  A backend needs that before it
// can pick a legal instruction set and privilege level, so the axis is
// its own chain.  The order of the chain is a ladder of stances: a pure
// region allows no hardware instruction at all, production code allows
// vectorization, a benchmark additionally allows a non-deterministic
// timestamp read, and only initialization allows a ring-zero register.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;

namespace {

using cal::HwInstruction;
using L = cal::HwInstructionLattice;

static_assert(crucible::algebra::Lattice<L>, "HwInstructionLattice must satisfy the Lattice concept.");
static_assert(crucible::algebra::BoundedLattice<L>, "HwInstructionLattice has both bottom() (NoneAllowed) and "
                                                    "top() (PrivilegedMsr).");
static_assert(!crucible::algebra::Semiring<L>, "A chain order carries no independent add and multiply, so the "
                                               "lattice must not satisfy the Semiring concept.");

static_assert(cal::detail::hw_instruction_lattice_self_test::hw_instruction_count == 5,
              "HwInstruction must have exactly 5 enumerators.  A new tier takes "
              "the next ordinal and must be added to the name switches.");

static_assert(std::is_same_v<std::underlying_type_t<HwInstruction>, std::uint8_t>,
              "HwInstruction must use uint8_t as its underlying type, because the "
              "chain derives leq from the underlying value.");

static_assert(std::to_underlying(HwInstruction::NoneAllowed) == 0, "bottom — the default for a pure region");
static_assert(std::to_underlying(HwInstruction::Scalar) == 1);
static_assert(std::to_underlying(HwInstruction::Vectorizable) == 2);
static_assert(std::to_underlying(HwInstruction::NonDeterministicTsc) == 3);
static_assert(std::to_underlying(HwInstruction::PrivilegedMsr) == 4, "top — ring-0 MSR/port I/O");

static_assert(L::bottom() == HwInstruction::NoneAllowed);
static_assert(L::top() == HwInstruction::PrivilegedMsr);

// Every tier admits everything below it.
static_assert(L::leq(HwInstruction::NoneAllowed, HwInstruction::Scalar));
static_assert(L::leq(HwInstruction::Scalar, HwInstruction::Vectorizable));
static_assert(L::leq(HwInstruction::Vectorizable, HwInstruction::NonDeterministicTsc));
static_assert(L::leq(HwInstruction::NonDeterministicTsc, HwInstruction::PrivilegedMsr));
static_assert(L::leq(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr), "transitive endpoints");

// These three are the admission decisions the chain exists to make.
static_assert(L::leq(HwInstruction::Scalar, HwInstruction::NonDeterministicTsc),
              "A scalar kernel is admitted by a context that also allows a "
              "timestamp read, because capability is cumulative.");
static_assert(!L::leq(HwInstruction::NonDeterministicTsc, HwInstruction::Vectorizable),
              "A kernel that reads the timestamp counter is not admitted where "
              "only vectorization is allowed.");
static_assert(!L::leq(HwInstruction::PrivilegedMsr, HwInstruction::NoneAllowed),
              "A kernel issuing a privileged register access is never admitted on "
              "a context that allows no hardware instruction.");

// Join takes the wider instruction class, meet the narrower.
static_assert(L::join(HwInstruction::Vectorizable, HwInstruction::NonDeterministicTsc)
                  == HwInstruction::NonDeterministicTsc,
              "Composing a vectorized site with one that reads the timestamp "
              "counter yields the timestamp tier, because the region as a whole "
              "reads the counter.");
static_assert(L::join(HwInstruction::NoneAllowed, HwInstruction::Scalar) == HwInstruction::Scalar,
              "NoneAllowed is the join identity");
static_assert(L::join(HwInstruction::PrivilegedMsr, HwInstruction::Scalar) == HwInstruction::PrivilegedMsr,
              "PrivilegedMsr absorbs in join");

static_assert(L::meet(HwInstruction::PrivilegedMsr, HwInstruction::Scalar) == HwInstruction::Scalar,
              "Meeting a permissive binding with a tight admission policy yields "
              "the tight floor.");
static_assert(L::meet(HwInstruction::NoneAllowed, HwInstruction::PrivilegedMsr) == HwInstruction::NoneAllowed,
              "NoneAllowed absorbs in meet");

static_assert(crucible::algebra::Lattice<L::At<HwInstruction::Scalar>>);
static_assert(crucible::algebra::BoundedLattice<L::At<HwInstruction::PrivilegedMsr>>);
static_assert(std::is_empty_v<L::At<HwInstruction::NoneAllowed>::element_type>,
              "At<NoneAllowed>::element_type must be empty so that "
              "Graded<Absolute, At<NoneAllowed>, P> collapses to sizeof(P).");
static_assert(std::is_empty_v<L::At<HwInstruction::Vectorizable>::element_type>);
static_assert(std::is_empty_v<L::At<HwInstruction::PrivilegedMsr>::element_type>);
static_assert(L::At<HwInstruction::Vectorizable>::tier == HwInstruction::Vectorizable,
              "At<I>::tier must equal I at the type level, so a wrapper can read "
              "the pinned tier with no runtime data.");

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                               L::At<HwInstruction::Vectorizable>, EightByteValue>)
                  == sizeof(EightByteValue),
              "Pinning a Vectorizable tier must add zero bytes to an 8-byte "
              "payload.");
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute,
                                               L::At<HwInstruction::PrivilegedMsr>, int>)
              == sizeof(int));

static_assert(L::name() == std::string_view{"HwInstructionLattice"});
static_assert(L::At<HwInstruction::Scalar>::name() == std::string_view{"HwInstructionLattice::At<Scalar>"});
static_assert(L::At<HwInstruction::NonDeterministicTsc>::name()
              == std::string_view{"HwInstructionLattice::At<NonDeterministicTsc>"});
static_assert(cal::hw_instruction_name(HwInstruction::PrivilegedMsr) == std::string_view{"PrivilegedMsr"});

}  // namespace

int main() {
    cal::detail::hw_instruction_lattice_self_test::hw_instruction_lattice_runtime_smoke_test();
    return 0;
}
