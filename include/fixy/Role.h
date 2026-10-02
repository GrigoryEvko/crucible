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
// A role is the atoms it names and the strict poles of the axes it does
// not name.  It spells no marker that accepts a default, because fn has
// no engagement tier.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Fn.h>
#include <fixy/Reject.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Anchor.h>

#include <cstdint>
#include <meta>
#include <type_traits>
#include <vector>

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
// cannot relax this role.  The Security grade is constant_time, the one
// point of that axis that states the timing claim, so every
// constant-time rule of fixy/Collision.h reads this role.  The strict
// poles carry the rest: linear usage, since duplicating a secret defeats
// the discipline, and non-reentrant, since a constant-time path must not
// interleave with itself.
template <class Type>
using CtCrypto = ::fixy::fn<Type, ::fixy::atom::with<>, ::fixy::atom::constant_time>;

// The Security axis takes a declassification rather than a plain public
// pin.  Both land the binding below the classified carrier, but only
// the declassification carries the policy that authorized the
// emission, and the grade on the Security axis recovers it from the
// type.  A plain pin would leave the emission auditable only by call
// site.  The policy must license IO: its mask in fixy/Corpus.h names IO.
// Under any other policy the value stays classified on IO, and the gate
// refuses the binding.
template <class Type, class Policy>
using PublicEmit = ::fixy::fn<Type, ::fixy::atom::with_io, ::fixy::atom::declassify<Policy>>;

}  // namespace fixy::role

namespace fixy::detail {

// The role roster, read out of fixy::role by reflection, so a role added
// there is covered the moment it is declared.  The check file of this
// header and test/fixy/test_row_hash_wrappers.cpp read the two walks.
//
// The two arities a role can have.  A role takes the payload alone, or a
// policy beside it, and can_substitute answers which without the roster
// having to declare it.  A shape neither form instantiates is counted
// unproven rather than skipped, so a third arity makes the count of
// roles_off_the_zero_slot smaller than roles_declared.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The template parameter defers the walk to the first call.  A
// translation unit that includes this header and does not call the walk
// does not evaluate it.  The same holds for the walk below.
template <class Site = void>
[[nodiscard]] consteval std::size_t roles_declared() noexcept {
    return std::meta::members_of(^^::fixy::role, std::meta::access_context::current()).size();
}

template <class Site = void>
[[nodiscard]] consteval std::size_t roles_off_the_zero_slot() noexcept {
    std::size_t proven = 0;
    static constexpr auto role_members =
        std::define_static_array(static_cast<::foundation::reflect::anchored_t<^^Site, std::vector<std::meta::info>>>(
            std::meta::members_of(^^::fixy::role, std::meta::access_context::current())));
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

}  // namespace fixy::detail
