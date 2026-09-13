// Five attempts to get past the acceptance gate, each of which the gate
// must refuse.  Every claim here is negative, so a green build is the
// statement that all five attempts failed.  The final witness is positive
// and exists to show the gate is not simply refusing everything.

#include <crucible/fixy/Reject.h>

namespace fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// A type shaped like a grant, empty and final, but not descended from the
// grant base.  The gate asks for the base, not for the shape.

namespace cheat_1_user_empty_type {
struct rogue final {};
static_assert(!fixy::grant::IsGrantTag<rogue>, "Cheat 1: a final empty user type without grant_base must NOT "
                                               "satisfy IsGrantTag.");

// In a pack it engages no axis at all, so acceptance fails a second
// time over.
static_assert(!fixy::IsAccepted<int, rogue>, "Cheat 1: a rogue type cannot satisfy the engagement check.");
}  // namespace cheat_1_user_empty_type

// Deriving from a real grant would inherit the base for free and open the
// type to downstream trait specialization.  Every grant tag is final, so
// that derivation does not compile at all and cannot be written here.
// What can be written is a type that takes the base directly and is not
// itself final, which the gate refuses on the same check.

namespace cheat_2_subclass_injection {
struct rogue_nonfinal : fixy::grant::grant_base {};
static_assert(!fixy::grant::IsGrantTag<rogue_nonfinal>, "Cheat 2: a non-final type inheriting grant_base must NOT "
                                                        "satisfy IsGrantTag — the final-class check defends against "
                                                        "subclass-injection of behavior.");
}  // namespace cheat_2_subclass_injection

// A dimension mapping declared for a foreign type, in the hope that
// having the mapping is enough to be accepted.  The well-formedness gate
// runs before any mapping is read.

namespace cheat_3_foreign_which_dim {
struct foreign {};
}  // namespace cheat_3_foreign_which_dim
// fixy-CR-09: known residual gap — the namespace is reopened here on
// purpose.  C++ has no access control over where a specialization may be
// written, so the gate, and not the namespace, is what defends.  This
// reopen is the demonstration of that.
namespace crucible::fixy::grant {
template <>
struct which_dim<::cheat_3_foreign_which_dim::foreign>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Usage> {};
}  // namespace crucible::fixy::grant
namespace cheat_3_foreign_which_dim {
static_assert(!fixy::IsAccepted<int, foreign>, "Cheat 3: specializing which_dim<> for a non-grant type must "
                                               "NOT bypass IsAcceptedGrants — the IsGrantTag gate fires "
                                               "before which_dim is consulted.");
}  // namespace cheat_3_foreign_which_dim

// A sensible type and no grants at all, in the hope that the type marker
// injected on its behalf carries the rest of the pack.  It engages the
// type axis and nothing else.

namespace cheat_4_empty_pack_with_type {
static_assert(!fixy::IsAccepted<int>, "Cheat 4: the auto-injected Type marker alone does not "
                                      "satisfy IsAccepted — the other 18 axes are still unengaged.");
}  // namespace cheat_4_empty_pack_with_type

// Roughly half the axes engaged, in the hope that acceptance is decided
// by a majority or short-circuits partway through the list.  It is a
// conjunction over every axis, so one unengaged axis is enough to refuse.

namespace cheat_5_half_engagement {
static_assert(!fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                                strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                                strict<D::Representation>>,
              "Cheat 5: 10-of-20 engagement (Type via injection + 9 "
              "explicit) must reject — the other 10 dims are still "
              "unengaged.");
}  // namespace cheat_5_half_engagement

// A gate that refuses everything would satisfy all five cheats above.
// This pack engages every axis except the type, which is injected, and
// must be accepted.

namespace counter_witness_accepts {
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                     strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                     strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                     strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "Counter-witness: a fully-engaged strict pack MUST accept "
    "(IsAccepted is not broken-shut).");
}  // namespace counter_witness_accepts

int main() { return 0; }
