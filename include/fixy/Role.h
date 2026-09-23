#pragma once

// The roles: one alias template per canonical atom pack, so a call site
// names a shape rather than spelling a pack.
//
// A role is an alias and nothing more.  fn's role gate, IsRoleFor, asks
// that the alias lands on an fn the gate itself admits, so a role
// cannot smuggle a pack past the tiers.  mint_fn_for<Role>(value) is
// the door for a unary role.  A binary role, one that takes a policy
// beside the payload, mints through mint_fn with its pack spelled,
// which is the same type; the role is then the spelling a reader
// recognises, not a second door.
//
// The old tree spelled each stance with thirty-two accept-the-default
// markers beside the one or two atoms that said anything.  Those
// markers went with the engagement tier, so a role here is the
// atoms it names and the strict poles it does not.
//
// Six old stances are not carried: NamedSession, SyncBlocking,
// RealtimeHot, InternalApi, UnclassifiedScratch and AsyncEndpoint.  No
// production site spelled any of them.  The search
// `rg 'stance::(NamedSession|SyncBlocking|RealtimeHot|InternalApi|
// UnclassifiedScratch|AsyncEndpoint)'` over include/crucible/*.h, src
// and vessel finds nothing; widened to every tree, the hits are the old
// sentinel test/test_fixy_fn.cpp, fifteen fixtures under
// test/fixy_neg/, tools/dump_row_hashes.cpp and
// test/test_row_hash_distinctness.cpp, each of which includes the old
// Fn.h and goes with it.  Every one of the six was one or two atoms
// over a pack a caller can write; InternalApi and UnclassifiedScratch
// existed only so that every security level was reachable through
// some stance, and as_internal and as_unclassified are atoms.
//
// Old spelling: include/crucible/fixy/Fn.h, namespace stance.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Fn.h>
#include <fixy/Reject.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <meta>
#include <type_traits>

namespace fixy::role {

// The shortest honest binding: strictest on every axis.
template <class Type>
using PureLinear = ::fixy::fn<Type>;

template <class Type>
using PureCopy = ::fixy::fn<Type, ::fixy::atom::copy>;

// Security is pinned here rather than left strict.  The strict Security
// pole is classified, and an IO channel is observable, so a classified
// payload leaks through it and the corpus refuses the pack.  A payload
// that is classified but authorized for emission uses PublicEmit,
// which discharges the same entry through a policy.
template <class Type>
using IoFunction = ::fixy::fn<Type, ::fixy::atom::with_io, ::fixy::atom::as_public>;

// Security is pinned here rather than left strict, for the same reason
// as an IO binding: a spawn is scheduler-observable, so a spawn that
// depends on a classified value leaks it through the interleaving.  A
// worker over a classified payload declassifies explicitly instead of
// using this role.
template <class Type>
using BgWorker =
    ::fixy::fn<Type, ::fixy::atom::with<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>,
               ::fixy::atom::as_public>;

// The consumer of a secret, which leaves classification along the one
// edge the policy names.
template <class Type, class Policy>
using SecretConsumer = ::fixy::fn<Type, ::fixy::atom::declassify<Policy>>;

// A constant-time path does no IO at all, because any IO trip is
// timing-observable and reopens the side channel the discipline
// closes.  The strict Effect pole is already the empty row, but the
// empty row is spelled out here so that a later widening of that pole
// cannot relax this role.  The strict poles carry the rest: linear
// usage, since duplicating a secret defeats the discipline, and
// non-reentrant, since a constant-time path must not interleave with
// itself.
template <class Type>
using CtCrypto = ::fixy::fn<Type, ::fixy::atom::with<>, ::fixy::atom::as_secret>;

// The Security axis takes a declassification rather than a plain public
// pin.  Both land the binding below the classified carrier, but only
// the declassification carries the policy that authorized the
// emission, and the grade on the Security axis recovers it from the
// type.  A plain pin would leave the emission auditable only by call
// site.
template <class Type, class Policy>
using PublicEmit = ::fixy::fn<Type, ::fixy::atom::with_io, ::fixy::atom::declassify<Policy>>;

}  // namespace fixy::role

// ---------------------------------------------------------------------
// The header proves its own claims here.

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
static_assert(
    ::fixy::IsAccepted<int, ::fixy::atom::with_io, ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);

// Why IoFunction and BgWorker pin as_public: the same pack without it
// is a classified value on an observable channel, and the corpus
// refuses it.
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::with_io>,
              "classified_io_without_declassify refuses an IO row on the strict Security pole");
static_assert(!::fixy::IsAccepted<int, ::fixy::atom::with<::foundation::effects::Effect::Bg,
                                                            ::foundation::effects::Effect::Alloc>>,
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
static_assert(std::is_same_v<::fixy::role::BgWorker<int>::grade_on<Axis::Effect>,
                             ::fixy::atom::with<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Effect>, ::fixy::atom::with<>>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Security>, ::fixy::atom::as_secret>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Usage>,
                             typename ::fixy::axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<::fixy::role::CtCrypto<int>::grade_on<Axis::Reentrancy>,
                             typename ::fixy::axis_traits<Axis::Reentrancy>::strict>);

// The policy is recoverable from the type.
static_assert(std::is_same_v<::fixy::role::PublicEmit<int, ::fixy::tags::secret_policy::WireSerialize>::grade_on<
                                 Axis::Security>,
                             ::fixy::atom::declassify<::fixy::tags::secret_policy::WireSerialize>>);
static_assert(std::is_same_v<::fixy::role::SecretConsumer<int, ::fixy::tags::secret_policy::AuditedLogging>::grade_on<
                                 Axis::Security>,
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
              "CtCrypto<int> and PureLinear<int> differ on the Security axis alone: one names the "
              "secret level through an atom, the other takes the same level as its strict pole.  "
              "The fold reads the grade as spelled, so the two take separate slots.");

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
// that would have caught the row-hash fold going missing in the first
// place: with no fold, every role answers the primary template's zero and
// every row below reddens at once.
//
// Zero is a sound floor and not an arbitrary one.  It is the answer the
// primary template gives a type carrying no row, so a binding that folds
// to zero is not merely sharing a slot with another binding, it is
// sharing one with every bare payload in the tree.
//
// The roster is read out of the namespace rather than listed here, so a
// role added above is covered the moment it is declared.  A hand list is
// how this check would rot: a role added without a row appended is a role
// the floor does not cover, and nothing would say so.

// The two arities a role can have.  A role takes the payload alone, or a
// policy beside it, and can_substitute answers which without the roster
// having to declare it.  A shape neither form instantiates is counted
// unproven rather than skipped, so a third arity reddens the coverage
// assertion below instead of slipping past it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval std::size_t roles_declared() noexcept {
    return std::meta::members_of(^^::fixy::role, std::meta::access_context::current()).size();
}

[[nodiscard]] consteval std::size_t roles_off_the_zero_slot() noexcept {
    std::size_t proven = 0;
    static constexpr auto role_members =
        std::define_static_array(std::meta::members_of(^^::fixy::role, std::meta::access_context::current()));
    template for (constexpr auto role_member : role_members) {
        constexpr auto payload = ^^int;
        constexpr auto policy = ^^::fixy::tags::secret_policy::WireSerialize;
        if constexpr (std::meta::can_substitute(role_member, {payload})) {
            using Binding = [:std::meta::substitute(role_member, {payload}):];
            if (::foundation::diag::row_hash_contribution_v<Binding> != 0) ++proven;
        } else if constexpr (std::meta::can_substitute(role_member, {payload, policy})) {
            using Binding = [:std::meta::substitute(role_member, {payload, policy}):];
            if (::foundation::diag::row_hash_contribution_v<Binding> != 0) ++proven;
        }
    }
    return proven;
}

#pragma GCC diagnostic pop

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
