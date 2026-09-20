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
// markers went with the engagement tier (A11.1), so a role here is the
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
#include <foundation/effects/Effect.h>

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

}  // namespace fixy::detail::role_self_test
