#pragma once

// Tagged<T, Tag> carries PROVENANCE, not VALIDATION.  `Tagged<T, Src>`
// asserts that somebody wrote the tag Src on this value.  It does not
// assert that the value passed any check, because nothing here runs
// one.  When a value must carry a CHECKED property rather than a
// provenance mark, the type for that is Refined<Pred, T>, whose
// constructor evaluates Pred.  Compose the two when both are wanted:
// Tagged<Refined<Pred, T>, Src>.
//
// The one door is mint_tagged<Tag>(value).  The old spelling kept the
// constructor public and let any caller write `Tagged<T, Src>{raw}`
// around a hand-built aggregate and get a value indistinguishable from
// one a validating factory produced.  That mattered most for the
// `Declared*` alias family, whose name reads like a guarantee: of 51
// such aliases, 27 had a factory that genuinely validates, 21 had only
// a pass-through factory that wraps without checking, and 3
// (DeclaredPeerSet, DeclaredPingmeshMeasurement,
// DeclaredGossipMulticastPlan) had no factory at all.  Closing the
// constructor does not turn a provenance mark into a check, but it
// makes every construction a named, searchable call, and it removes
// the mutable accessor the old wrapper exposed, through which a
// Sanitized tag could wrap a value nobody sanitized.
//
// The default constructor is the second door, and it exists only to
// let an array of slots start empty before any of them is filled.  A
// default-constructed Tagged holds T{} under its tag; the tag is then
// a claim about nothing.  One production site needs it: the slot
// array `OpsPtr ops_[CAP]{}` of RegionCache.  Two more spell it
// without exercising it, the NSDMIs `topic{}` and `spec_{}` of
// GossipMulticast, which every construction overrides.  The remaining
// members declared without an initializer (CallSiteTable::Entry,
// KtlsOffloadRequest, the connection classes of cntp) are always
// initialized by their aggregate or constructor.  The door stays for
// the slot array.
//
// mint_tagged is the named factory, but it is NOT true that one
// `mint_` search finds every authorization point in the tree.  The
// factories that produce a Declared* value are also named declare_*,
// admit_*, validate_*, plan_*, query_*, and several one-offs
// (recommend_cc, eligibility_check, fallback_dispatch, open_stream,
// ptp_status_from_daemon_report).  A `mint_` grep finds roughly half
// of them.  To enumerate the authorization surface, search the alias
// names (`Declared`), not the factory prefix.
//
// Old spelling: include/crucible/safety/Tagged.h, with the path and
// architecture edges of include/crucible/safety/source/Path.h and
// Arch.h folded into the one catalog below, and the detection surface
// of include/crucible/safety/IsTagged.h.  The tag namespaces live in
// fixy/Tags.h.

#include <fixy/GradedFacade.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/TrustLattice.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

// Every tag transition is rejected unless an edge below admits it.
// Fail-closed is the point: an open-by-default policy would silently
// permit laundering untrusted input into a Sanitized tag, which is
// exactly the bug the phantom axis exists to catch.  Closing by default
// makes this namespace the single source of truth.
//
// Each edge admits exactly one transition, and each carries the name
// of the validator that discharges it.  That validator is the
// safety-bearing component.  The retag is only the type-level record
// that it ran.
//
// Adding an edge here is a security review.  The discipline has three
// parts, and the assertions after the class derive each one from this
// namespace rather than restating it per edge.  Group by tag family
// and never admit a cross-family edge, because laundering across
// orthogonal axes confounds what the phantom means.  List shorter
// transitions first.  Leave every inverse direction to the fail-closed
// default: the relation is a one-way ratchet.
//
// Trust is a one-way ratchet.  Assumed is a sibling precondition that
// discharges into Verified once the assumption is checked.
namespace fixy::tags::admitted_retags {

// Discharge: test suite ran and passed against this value.
inline constexpr ::foundation::fail_closed::edge<trust::Unverified, trust::Tested> unverified_to_tested{};
// Discharge: formal proof / cryptographic verification completed.
inline constexpr ::foundation::fail_closed::edge<trust::Unverified, trust::Verified> unverified_to_verified{};
// Discharge: proof obligation discharged on top of test coverage.
inline constexpr ::foundation::fail_closed::edge<trust::Tested, trust::Verified> tested_to_verified{};
// Discharge: the precondition it rested on is now checked.
inline constexpr ::foundation::fail_closed::edge<trust::Assumed, trust::Verified> assumed_to_verified{};
// Discharge: an axiom statement was authored about this value.  The
// retag site names the holder of that responsibility.
inline constexpr ::foundation::fail_closed::edge<trust::Unverified, trust::Assumed> unverified_to_assumed{};

// Discharge: input sanitizer at the trust boundary accepted bytes.
inline constexpr ::foundation::fail_closed::edge<source::External, source::Sanitized> external_to_sanitized{};
// Discharge: end-to-end integrity check (xxHash64 trailer, etc.)
// recomputed at receiver and matched the wire value.
inline constexpr ::foundation::fail_closed::edge<source::External, source::IntegrityVerified>
    external_to_integrity_verified{};
// Discharge: sanitized value additionally passes integrity check.  The
// two predicates compose (sanitized AND integrity-verified).
inline constexpr ::foundation::fail_closed::edge<source::Sanitized, source::IntegrityVerified>
    sanitized_to_integrity_verified{};
// Discharge: user-supplied value passed the input sanitizer.
inline constexpr ::foundation::fail_closed::edge<source::FromUser, source::Sanitized> from_user_to_sanitized{};
// Discharge: the recording pipeline closed the trace, which admits the
// value into validated persistent state.
inline constexpr ::foundation::fail_closed::edge<source::Recorded, source::Loaded> recorded_to_loaded{};
// Discharge: the row passed schema validation, which enforces
// well-formedness of every field before the row is admitted.
inline constexpr ::foundation::fail_closed::edge<source::FromDb, source::Sanitized> from_db_to_sanitized{};
// Discharge: the config parser ran its schema and range check at load
// time.
inline constexpr ::foundation::fail_closed::edge<source::FromConfig, source::Sanitized> from_config_to_sanitized{};
// Discharge: the adapter marshalling the call ran the sanitizer on the
// opaque value before admitting it.
inline constexpr ::foundation::fail_closed::edge<source::ABIBoundary, source::Sanitized> abi_boundary_to_sanitized{};
// Discharge: the persisted value additionally passed the
// integrity-check predicate.  The two claims compose.
inline constexpr ::foundation::fail_closed::edge<source::Loaded, source::IntegrityVerified>
    loaded_to_integrity_verified{};
// Discharge: replay produced a deterministic value matching the
// recorded checkpoint.  This is the read side of the same discharge
// that Recorded to Loaded gates on the write side.
inline constexpr ::foundation::fail_closed::edge<source::Replayed, source::Loaded> replayed_to_loaded{};
// Discharge: the recorded value additionally passed the integrity check
// taken at trace close.
inline constexpr ::foundation::fail_closed::edge<source::Recorded, source::IntegrityVerified>
    recorded_to_integrity_verified{};

// These admittances permit the type transition only.  They do not
// witness that a sanitize pass ran.  Discharging that obligation is the
// caller's.  CipherPath has no edge: its bytes never crossed an
// untrusted boundary, the directory-anchored open helpers trust it
// directly, and keeping it out of the three external lanes is what
// stops operator-supplied bytes from reaching those helpers.
inline constexpr ::foundation::fail_closed::edge<source::FromUserPath, source::Sanitized> from_user_path_to_sanitized{};
inline constexpr ::foundation::fail_closed::edge<source::FromEnvPath, source::Sanitized> from_env_path_to_sanitized{};
inline constexpr ::foundation::fail_closed::edge<source::FromConfigPath, source::Sanitized>
    from_config_path_to_sanitized{};

// Portable into x86 and Portable into ARM are sound weakenings.  x86
// into Portable is a false widening, because x86 code faults on ARM,
// and relabelling x86 as ARM or ARM as x86 stays rejected: both stay
// with the fail-closed default.
inline constexpr ::foundation::fail_closed::edge<source::PortablePinned, source::X86Pinned>
    portable_pinned_to_x86_pinned{};
inline constexpr ::foundation::fail_closed::edge<source::PortablePinned, source::ArmPinned>
    portable_pinned_to_arm_pinned{};

// Discharge: the adapter's well-formedness checks ran on the input.
// This is the edge that carries TraceRing's FromPytorchEntryPtr into
// its ValidatedEntryPtr; the vouch() helper that builds the second tag
// directly for internally built entries dies at Stage B4 because this
// edge exists.
inline constexpr ::foundation::fail_closed::edge<vessel_trust::FromPytorch, vessel_trust::Validated>
    from_pytorch_to_validated{};

}  // namespace fixy::tags::admitted_retags

namespace fixy {

// Identity is a no-op transition, admitted unconditionally so generic
// code that re-asserts the tag it already holds does not trip the gate.
// Every other pair is answered by the catalog and nothing else: there
// is no primary template to specialize, so a foreign translation unit
// cannot admit an edge by reopening a trait.
template <typename From, typename To>
concept RetagAllowed =
    std::is_same_v<From, To> || ::foundation::fail_closed::Admitted<^^tags::admitted_retags, From, To>;

// The trait spelling of the same answer, for a reader that asks the
// question as a value.  It is a view of RetagAllowed and carries no
// authority of its own: retag() consults the concept, so a
// specialization of this struct written elsewhere admits nothing.
template <typename From, typename To>
struct retag_policy {
    static constexpr bool allowed = RetagAllowed<From, To>;
};

template <typename Tag>
concept ValidTaggedTag = std::is_class_v<Tag>;

template <typename T, typename Tag>
class Tagged;

// The constructor is private, so this factory and retag are the door.
template <typename Tag, typename T>
    requires ValidTaggedTag<Tag>
[[nodiscard]] constexpr Tagged<T, Tag> mint_tagged(T value) noexcept(std::is_nothrow_move_constructible_v<T>);

// Moves the value under a new tag along an admitted edge.  The member
// form Tagged::retag<To>() forwards here.
template <typename To, typename T, typename From>
    requires RetagAllowed<From, To>
[[nodiscard]] constexpr Tagged<T, To> retag(Tagged<T, From>&& tagged) noexcept(std::is_nothrow_move_constructible_v<T>);

template <typename T, typename Tag>
class [[nodiscard]] Tagged : public graded_facade<::foundation::algebra::ModalityKind::RelativeMonad,
                                                  ::foundation::algebra::lattices::TrustLattice<Tag>, T> {
public:
    using tag_type = Tag;

    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the two names this
    // class body uses unqualified are re-declared here rather than
    // found by lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::RelativeMonad,
                                  ::foundation::algebra::lattices::TrustLattice<Tag>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

private:
    graded_type impl_;

    constexpr explicit Tagged(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    template <typename S, typename U>
        requires ValidTaggedTag<S>
    friend constexpr Tagged<U, S> mint_tagged(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <typename To, typename U, typename From>
        requires RetagAllowed<From, To>
    friend constexpr Tagged<U, To> retag(Tagged<U, From>&& tagged) noexcept(std::is_nothrow_move_constructible_v<U>);

public:
    // The second door: an empty slot before any of an array's slots is
    // filled.  The explicit constructor stays the only way to
    // materialise a non-default value, which keeps every
    // provenance-bearing construction site a deliberate call.
    constexpr Tagged() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::default_initializable<T>
    = default;

    [[nodiscard]] constexpr const T& value() const noexcept { return impl_.peek(); }

    // There is no value_mut().  The substrate admits mutation for an
    // empty grade, and this grade is empty, but a mutable reference
    // would let a Sanitized tag wrap a value nobody sanitized.  A change
    // is into(), the change, and a fresh mint under the tag the changed
    // value has earned.

    template <typename NewTag>
        requires RetagAllowed<Tag, NewTag>
    [[nodiscard]] constexpr Tagged<T, NewTag> retag() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return ::fixy::retag<NewTag>(std::move(*this));
    }

    [[nodiscard]] constexpr T into() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }
};

template <typename Tag, typename T>
    requires ValidTaggedTag<Tag>
[[nodiscard]] constexpr Tagged<T, Tag> mint_tagged(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Tagged<T, Tag>{std::move(value)};
}

template <typename To, typename T, typename From>
    requires RetagAllowed<From, To>
[[nodiscard]] constexpr Tagged<T, To>
retag(Tagged<T, From>&& tagged) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Tagged<T, To>{std::move(tagged).into()};
}

static_assert(sizeof(Tagged<int, tags::source::FromUser>) == sizeof(int));
static_assert(sizeof(Tagged<void*, tags::trust::Verified>) == sizeof(void*));
static_assert(sizeof(Tagged<long, tags::access::AppendOnly>) == sizeof(long));

// The detection surface of the old IsTagged.h.  One reflection query
// answers it, and the associated types are read off the wrapper's own
// typedefs, so there is no primary-plus-specialization ladder to keep
// in step with the class.

template <typename T>
inline constexpr bool is_tagged_v = ::foundation::reflect::is_instance_of_v<T, ^^Tagged>;

template <typename T>
concept IsTagged = is_tagged_v<T>;

template <typename T>
    requires is_tagged_v<T>
using tagged_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires is_tagged_v<T>
using tagged_tag_t = typename std::remove_cvref_t<T>::tag_type;

namespace detail::tagged_self_test {

struct LookalikeTagged {
    using value_type = int;
    using tag_type = tags::source::FromUser;
};

using T_int_user = Tagged<int, tags::source::FromUser>;
using T_int_db = Tagged<int, tags::source::FromDb>;
using T_double_user = Tagged<double, tags::source::FromUser>;

static_assert(is_tagged_v<T_int_user>);
static_assert(is_tagged_v<T_int_db>);
static_assert(is_tagged_v<T_double_user>);
static_assert(is_tagged_v<T_int_user&>);
static_assert(is_tagged_v<T_int_user&&>);
static_assert(is_tagged_v<T_int_user const>);
static_assert(is_tagged_v<T_int_user const&>);
static_assert(is_tagged_v<T_int_user volatile>);
static_assert(!is_tagged_v<int>);
static_assert(!is_tagged_v<int*>);
static_assert(!is_tagged_v<T_int_user*>);
static_assert(!is_tagged_v<void>);
static_assert(!is_tagged_v<LookalikeTagged>);
static_assert(IsTagged<T_int_user>);
static_assert(IsTagged<T_int_db const&>);
static_assert(!IsTagged<int>);
static_assert(std::is_same_v<tagged_value_t<T_int_user>, int>);
static_assert(std::is_same_v<tagged_value_t<T_double_user>, double>);
static_assert(std::is_same_v<tagged_tag_t<T_int_user>, tags::source::FromUser>);
static_assert(std::is_same_v<tagged_tag_t<T_int_db const&>, tags::source::FromDb>);
static_assert(!std::is_same_v<tagged_tag_t<T_int_user>, tagged_tag_t<T_int_db>>);

}  // namespace detail::tagged_self_test

// These two tags must never gain an edge.  They exist only to witness
// the fail-closed default, and they sit outside every production tag
// namespace so application code cannot reach them.
namespace detail::retag_policy_test {
struct NeverFrom {};
struct NeverTo {};
}  // namespace detail::retag_policy_test

// The sentinel pair, rather than a production pair, is what makes the
// assertions below witness a structural property.  A production pair
// would only witness that the catalog has not yet grown that edge, and
// would red the day it does.
static_assert(RetagAllowed<tags::source::FromUser, tags::source::FromUser>,
              "RetagAllowed must admit identity (X -> X)");
static_assert(!RetagAllowed<detail::retag_policy_test::NeverFrom, detail::retag_policy_test::NeverTo>,
              "RetagAllowed MUST be fail-closed for any (From -> To) pair without an edge in "
              "fixy::tags::admitted_retags");
static_assert(retag_policy<tags::source::FromUser, tags::source::FromUser>::allowed,
              "retag_policy must agree with RetagAllowed on identity");
static_assert(!retag_policy<detail::retag_policy_test::NeverFrom, detail::retag_policy_test::NeverTo>::allowed,
              "retag_policy must agree with RetagAllowed on the sentinel pair");

// The catalog's discipline, derived from the namespace.  The count is
// the one place a new edge must be acknowledged by hand: an edge is
// admitted the moment it is declared, and this pin is what makes the
// declaration a reviewed two-place edit.
inline constexpr std::size_t admitted_retag_count = ::foundation::fail_closed::edge_count<^^tags::admitted_retags>();

static_assert(admitted_retag_count == 22, "fixy::tags::admitted_retags holds a different number of edges than "
                                          "this pin.  An edge was added or removed: review it as the security "
                                          "change it is, then move the pin.");
static_assert(::foundation::fail_closed::every_edge_is_admitted<^^tags::admitted_retags>(),
              "every edge declared in fixy::tags::admitted_retags must be admitted by the "
              "fail-closed check that reads the same namespace");
static_assert(::foundation::fail_closed::is_antisymmetric<^^tags::admitted_retags>(),
              "fixy::tags::admitted_retags is a one-way ratchet: no edge may have its inverse "
              "admitted.  An inverse edge would let a proof, a sanitize pass or an integrity check "
              "be erased by relabelling.");
static_assert(::foundation::fail_closed::is_intra_namespace<^^tags::admitted_retags>(),
              "fixy::tags::admitted_retags must not cross tag families: laundering across "
              "orthogonal axes (source::* to trust::*, source::* to access::*) is never safe");

}  // namespace fixy
