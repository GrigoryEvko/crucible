// The compile-time checks of fixy/Role.h.

#include <fixy/Role.h>

namespace fixy::detail::role_self_test {

// Every unary role passes the gate it is written against, so
// mint_fn_for<Role> is a door and not a refusal.
static_assert(::fixy::IsRoleFor<::fixy::role::PureLinear, int>);
static_assert(::fixy::IsRoleFor<::fixy::role::PureCopy, int>);
static_assert(::fixy::IsRoleFor<::fixy::role::IoFunction, int>);
static_assert(::fixy::IsRoleFor<::fixy::role::BgWorker, int>);
static_assert(::fixy::IsRoleFor<::fixy::role::CtCrypto, int>);

// The binary roles pass the same gate with their pack spelled.
static_assert(::fixy::IsAccepted<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>>);
static_assert(::fixy::IsAccepted<int, ::fixy::atom::with_io,
                                 ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);

// Why IoFunction and BgWorker pin as_public: the same pack without it
// is a classified value on an observable channel, and the corpus
// refuses it.
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::with_io>,
              "classified_io_without_declassify refuses an IO row on the strict Security pole");
static_assert(!::fixy::IsAccepted<
                  int, ::fixy::atom::with<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>>,
              "classified_bg_without_declassify refuses a Bg row on the strict Security pole");

// A role is the binding it spells, not a lookalike.
static_assert(std::is_same_v<::fixy::role::PureLinear<int>, ::fixy::fn<int>>);
static_assert(std::is_same_v<::fixy::role::PureCopy<int>, ::fixy::fn<int, ::fixy::atom::copy>>);

// Every atom is empty, so a role collapses to its payload.
static_assert(sizeof(::fixy::role::PureLinear<int>) == sizeof(int));
static_assert(sizeof(::fixy::role::PureLinear<char>) == sizeof(char));
static_assert(sizeof(::fixy::role::PureLinear<double>) == sizeof(double));
static_assert(sizeof(::fixy::role::IoFunction<int>) == sizeof(int));
static_assert(sizeof(::fixy::role::BgWorker<int>) == sizeof(int));
static_assert(sizeof(::fixy::role::CtCrypto<int>) == sizeof(int));
static_assert(sizeof(::fixy::role::PublicEmit<int, ::fixy::tags::secret_policy::WireSerialize>) == sizeof(int));

// The grades each role pins, and the ones it leaves strict.
static_assert(std::is_same_v<::fixy::role::PureCopy<int>::grade_on<Axis::Usage>, ::fixy::atom::copy>);
static_assert(std::is_same_v<::fixy::role::PureCopy<int>::grade_on<Axis::Refinement>,
                             typename ::fixy::axis_traits<Axis::Refinement>::strict>);
static_assert(std::is_same_v<::fixy::role::IoFunction<int>::grade_on<Axis::Effect>, ::fixy::atom::with_io>);
static_assert(std::is_same_v<::fixy::role::IoFunction<int>::grade_on<Axis::Security>, ::fixy::atom::as_public>);
static_assert(
    std::is_same_v<::fixy::role::BgWorker<int>::grade_on<Axis::Effect>,
                   ::fixy::atom::with<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Effect>, ::fixy::atom::with<>>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Security>, ::fixy::atom::constant_time>);
static_assert(::fixy::atom::IsConstantTime<::fixy::role::CtCrypto<int>::grade_on<Axis::Security>>,
              "CtCrypto must state the timing claim, or no constant-time rule reads the role");
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Usage>,
                             typename ::fixy::axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Reentrancy>,
                             typename ::fixy::axis_traits<Axis::Reentrancy>::strict>);

// The policy is recoverable from the type.
static_assert(
    std::is_same_v<::fixy::role::PublicEmit<int, ::fixy::tags::secret_policy::WireSerialize>::grade_on<Axis::Security>,
                   ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);
static_assert(std::is_same_v<
              ::fixy::role::SecretConsumer<int, ::fixy::tags::secret_policy::AuditedLogging>::grade_on<Axis::Security>,
              ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>>);

// ---------------------------------------------------------------------
// Two roles must never share a federation cache slot.
//
// A cache key pairs a content hash with a row hash.  The content hash
// answers "which computation", the row hash answers "under which
// discipline".  Two roles over one payload compute the same thing under
// different disciplines, so the row hash is the only half that can tell
// them apart, and a kernel compiled for one is not safe to serve for the
// other.  A pure copy may be shared between installations; a binding
// that performs IO and emits publicly may not.
//
// These pin one pair per axis-family rather than every pair, because a
// pair is what a regression produces: a fold that drops an axis collapses
// exactly the roles that differ only on it.  The last one separates a
// binding from the payload it carries, which is the collapse a fold that
// returns its input would produce.

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::PureCopy<int>> != 0,
              "PureCopy<int> must contribute a non-zero federation cache row hash.  Zero is what "
              "the primary template answers for a type carrying no row, so a binding that folds "
              "to zero is indistinguishable from its own bare payload.");

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::IoFunction<int>> != 0,
              "IoFunction<int> must contribute a non-zero federation cache row hash.");

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::PureCopy<int>>
                  != ::foundation::diag::row_hash_contribution_v<::fixy::role::IoFunction<int>>,
              "PureCopy<int> and IoFunction<int> must take disjoint federation cache slots.  They "
              "carry the same payload and differ on the Effect and Security axes: one is a pure "
              "copy, the other performs IO and emits publicly.  One slot for both serves a kernel "
              "compiled under the pure discipline to a caller that does IO.");

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::PureLinear<int>>
                  != ::foundation::diag::row_hash_contribution_v<::fixy::role::PureCopy<int>>,
              "PureLinear<int> and PureCopy<int> differ on the Usage axis alone — strict linear "
              "against copy — so the fold must read that axis.");

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::BgWorker<int>>
                  != ::foundation::diag::row_hash_contribution_v<::fixy::role::IoFunction<int>>,
              "BgWorker<int> declares {Bg, Alloc} and IoFunction<int> declares {IO}, so the fold "
              "must read the effect row and not merely whether one was declared.");

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::CtCrypto<int>>
                  != ::foundation::diag::row_hash_contribution_v<::fixy::role::PureLinear<int>>,
              "CtCrypto<int> and PureLinear<int> differ on the Security axis alone: one states "
              "constant_time, the other takes the classified strict pole.  A kernel compiled for "
              "one discipline must not serve the other, so the two take separate slots.");

static_assert(::foundation::diag::row_hash_contribution_v<::fixy::role::PureLinear<int>>
                  != ::foundation::diag::row_hash_contribution_v<int>,
              "A binding must not share a slot with the payload it carries.  The payload answers "
              "the primary template's zero, so this fails exactly when the fold over the axes is "
              "missing and the binding falls through to that primary too.");

// ---------------------------------------------------------------------
// No role reaches the zero slot, over every role there is.
//
// The pairs above pin one axis-family each, which is what a regression
// produces.  This is the floor under all of them, and it is the check
// that catches a missing row-hash fold: with no fold, every role answers
// the primary template's zero and every row below reddens at once.
//
// Zero is a sound floor and not an arbitrary one.  It is the answer the
// primary template gives a type carrying no row, so a binding that folds
// to zero is not merely sharing a slot with another binding, it is
// sharing one with every bare payload in the tree.
//
// The roster is read out of the namespace rather than listed here, so a
// role added above is covered the moment it is declared.  A hand list is
// how this check would rot: a role added without a row appended is a role
// the floor does not cover, and nothing would say so.  fixy/Role.h holds
// the two walks, roles_declared and roles_off_the_zero_slot.

static_assert(roles_declared() > 0, "the roster reflected out of fixy::role is empty, so the floor below proves "
                                    "nothing.  Either every role moved out of the namespace, or the reflection "
                                    "query stopped seeing alias templates.");

static_assert(roles_off_the_zero_slot() == roles_declared(),
              "Every role must contribute a non-zero federation cache row hash.  A role at zero shares "
              "a slot with every bare payload in the tree, which is what happens when the fold over the "
              "axes in fixy/Fn.h is missing.  This also reddens for a role whose arity is neither the "
              "payload alone nor a payload and a policy, because such a role is not instantiated here "
              "and so is not proven.");

}  // namespace fixy::detail::role_self_test
