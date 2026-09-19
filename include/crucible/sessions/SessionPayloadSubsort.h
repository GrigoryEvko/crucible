#pragma once

// Subsort axioms relating the per-value safety wrappers to the
// session-type subsort order.  Without them a wrapped payload is simply
// a different type from the payload it wraps, and every protocol that
// wants payload refinement states the relation by hand at each site.
//
// Flow is one-way and provenance-aware.  These hold:
//
//   Refined<P, T>            ⩽  T
//   Refined<P, T>            ⩽  Refined<Q, T>      when P implies Q
//   Tagged<T, V>             ⩽  T                  for a V recording
//                                                  that the value was
//                                                  checked, computed
//                                                  here, or read from a
//                                                  trusted store
//   NumericalTier<Tight, T>  ⩽  NumericalTier<Loose, T>
//
// The absences are the discipline.  Each one has an alternative that
// looks harmless on inspection and is not:
//
//   Tagged<T, External>, Tagged<T, FromPytorch> and Tagged<T, FromUser>
//     ⩽ T would carry untrusted input into a position that assumes
//     validation, defeating the check the boundary installs.  Such a
//     tag is replaced after validating, never dropped.
//
//   Linear<T> ⩽ T would let the value flow on without the explicit
//     consume that makes linearity visible.
//
//   Secret<T> ⩽ T would move classified data into an unclassified
//     position with nothing at the call site to show for it.
//
//   A trust or access tag ⩽ T would discard what a verifier
//     established, or what separates a read-only handle from a writable
//     one.  Both carry more than bare T, and surrendering that is a
//     decision the call site should show.
//
//   A version tag ⩽ T would stand in for a migration policy that has to
//     be written per version pair.
//
//   T ⩽ Tagged<T, V> and T ⩽ Refined<P, T> would let a value acquire a
//     guarantee nobody established.
//
// A caller that means to surrender one of these goes through the
// wrapper's own accessor, which leaves the decision visible where it
// was made.

#include <crucible/safety/_Refined.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/safety/_Tagged.h>

#include <type_traits>

namespace crucible::safety::proto {

template <auto Pred, typename T>
struct is_subsort<Refined<Pred, T>, T> : std::true_type {};

// A value carrying the stronger predicate stands where the weaker one
// is expected.  Send lifts a payload relation covariantly and Recv
// contravariantly, so the same axiom reads in opposite directions
// depending on which side of the channel the payload sits:
//
//   Send<Refined<P, T>, K>  ⩽  Send<Refined<Q, T>, K>
//   Recv<Refined<Q, T>, K>  ⩽  Recv<Refined<P, T>, K>
//
// The guard on the predicate types keeps this specialisation from
// claiming the case where both predicates are the same, which the
// reflexive path already answers.

template <auto P, auto Q, typename T>
    requires(implies_v<P, Q> && !std::is_same_v<decltype(P), decltype(Q)>)
struct is_subsort<Refined<P, T>, Refined<Q, T>> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::Sanitized>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::FromInternal>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::FromConfig>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::FromDb>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::Durable>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, source::Computed>, T> : std::true_type {};

template <typename T>
struct is_subsort<Tagged<T, vessel_trust::Validated>, T> : std::true_type {};

// The relation is not transitively closed.  A payload that is both
// refined and tagged reaches the bare payload only when the caller
// chains the two steps.  The opposite nesting needs no rule of its own,
// because the tag axiom applies at the inner position.

template <auto Pred, typename T, typename V>
struct is_subsort<Refined<Pred, Tagged<T, V>>, Tagged<T, V>> : std::true_type {};

// The tolerance order runs loose below tight, so a tighter producer
// guarantee satisfies a looser consumer requirement.  That inverts the
// operand order in the comparison below.
//
// The axiom stays payload-shaped.  A rule written directly over Send or
// Recv would work here and is the wrong layer: those are combinators,
// their variance is stated once where they are defined, and duplicating
// it lets a protocol marker start flowing as if it were a value.

template <Tolerance ProducerTier, Tolerance ConsumerTier, typename P>
struct is_subsort<NumericalTier<ProducerTier, P>, NumericalTier<ConsumerTier, P>>
    : std::bool_constant<ToleranceLattice::leq(ConsumerTier, ProducerTier)> {};

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::payload_subsort_self_test {

struct DispatchRequest {
    int op_id;
};
struct MemoryPlanByte {
    unsigned char value;
};
struct ConfigEntry {
    int field;
};
struct TensorTile {
    float value[4];
};

static_assert(is_subsort_v<Refined<positive, int>, int>);
static_assert(is_subsort_v<Refined<non_negative, int>, int>);
static_assert(is_subsort_v<Refined<non_zero, int>, int>);
static_assert(is_subsort_v<Refined<bounded_above<1024>, int>, int>);

static_assert(!is_subsort_v<int, Refined<positive, int>>);
static_assert(!is_subsort_v<int, Refined<non_negative, int>>);

static_assert(is_subsort_v<Tagged<DispatchRequest, source::Sanitized>, DispatchRequest>);
static_assert(is_subsort_v<Tagged<DispatchRequest, source::FromInternal>, DispatchRequest>);
static_assert(is_subsort_v<Tagged<ConfigEntry, source::FromConfig>, ConfigEntry>);
static_assert(is_subsort_v<Tagged<ConfigEntry, source::FromDb>, ConfigEntry>);
static_assert(is_subsort_v<Tagged<MemoryPlanByte, source::Durable>, MemoryPlanByte>);
static_assert(is_subsort_v<Tagged<MemoryPlanByte, source::Computed>, MemoryPlanByte>);
static_assert(is_subsort_v<Tagged<DispatchRequest, vessel_trust::Validated>, DispatchRequest>);

static_assert(!is_subsort_v<Tagged<DispatchRequest, source::External>, DispatchRequest>);
static_assert(!is_subsort_v<Tagged<DispatchRequest, source::FromUser>, DispatchRequest>);
static_assert(!is_subsort_v<Tagged<DispatchRequest, vessel_trust::FromPytorch>, DispatchRequest>);

static_assert(!is_subsort_v<DispatchRequest, Tagged<DispatchRequest, source::Sanitized>>);
static_assert(!is_subsort_v<DispatchRequest, Tagged<DispatchRequest, source::External>>);
static_assert(!is_subsort_v<DispatchRequest, Tagged<DispatchRequest, vessel_trust::Validated>>);

static_assert(!is_subsort_v<Tagged<int, trust::Verified>, int>);
static_assert(!is_subsort_v<Tagged<int, trust::Tested>, int>);
static_assert(!is_subsort_v<Tagged<int, trust::Unverified>, int>);
static_assert(!is_subsort_v<Tagged<int, trust::Assumed>, int>);
static_assert(!is_subsort_v<Tagged<int, access::RO>, int>);
static_assert(!is_subsort_v<Tagged<int, access::WriteOnce>, int>);
static_assert(!is_subsort_v<Tagged<int, access::AppendOnly>, int>);

static_assert(!is_subsort_v<Tagged<int, version::V<1>>, int>);
static_assert(!is_subsort_v<Tagged<int, version::V<2>>, int>);

static_assert(is_subsort_v<Refined<positive, Tagged<int, source::Sanitized>>, Tagged<int, source::Sanitized>>);

static_assert(is_subsort_v<Refined<bounded_above<1024>, Tagged<int, vessel_trust::Validated>>,
                           Tagged<int, vessel_trust::Validated>>);

static_assert(!is_subsort_v<Tagged<int, source::Sanitized>, Refined<positive, Tagged<int, source::Sanitized>>>);

static_assert(is_subsort_v<Tagged<int, source::Sanitized>, Tagged<int, source::Sanitized>>);
static_assert(is_subsort_v<Refined<positive, int>, Refined<positive, int>>);
static_assert(is_subsort_v<int, int>);

using BitexactTile = NumericalTier<Tolerance::BITEXACT, TensorTile>;
using Fp32Tile = NumericalTier<Tolerance::ULP_FP32, TensorTile>;
using Fp16Tile = NumericalTier<Tolerance::ULP_FP16, TensorTile>;
using RelaxedTile = NumericalTier<Tolerance::RELAXED, TensorTile>;

static_assert(is_subsort_v<BitexactTile, RelaxedTile>);
static_assert(is_subsort_v<BitexactTile, Fp16Tile>);
static_assert(is_subsort_v<Fp32Tile, Fp16Tile>);
static_assert(!is_subsort_v<RelaxedTile, BitexactTile>);
static_assert(!is_subsort_v<Fp16Tile, Fp32Tile>);

static_assert(is_subtype_sync_v<Send<BitexactTile, End>, Send<RelaxedTile, End>>);

static_assert(!is_subtype_sync_v<Send<RelaxedTile, End>, Send<BitexactTile, End>>);

static_assert(is_subtype_sync_v<Recv<RelaxedTile, End>, Recv<BitexactTile, End>>);

static_assert(!is_subtype_sync_v<Recv<BitexactTile, End>, Recv<RelaxedTile, End>>);

static_assert(CompatibleClient<Send<BitexactTile, End>, Recv<RelaxedTile, End>>);

static_assert(!CompatibleClient<Send<RelaxedTile, End>, Recv<BitexactTile, End>>);

static_assert(CompatibleServer<Recv<RelaxedTile, End>, Send<BitexactTile, End>>);

static_assert(!CompatibleServer<Recv<BitexactTile, End>, Send<RelaxedTile, End>>);

static_assert(CompatibleClient<Loop<Send<BitexactTile, Continue>>, Loop<Recv<RelaxedTile, Continue>>>);

static_assert(!CompatibleClient<Loop<Send<RelaxedTile, Continue>>, Loop<Recv<BitexactTile, Continue>>>);

static_assert(!is_subsort_v<Tagged<int, source::Sanitized>, Tagged<int, source::FromInternal>>);
static_assert(!is_subsort_v<Tagged<int, source::FromConfig>, Tagged<int, source::FromDb>>);

static_assert(is_subsort_v<Refined<positive, int>, Refined<non_negative, int>>);
static_assert(!is_subsort_v<Refined<non_negative, int>, Refined<positive, int>>);

static_assert(is_subsort_v<Refined<positive, int>, Refined<non_zero, int>>);

static_assert(is_subsort_v<Refined<power_of_two, std::size_t>, Refined<non_zero, std::size_t>>);

// Being non-zero says nothing about sign.  A value of -3 satisfies the
// first predicate and not the second, so the implication fails.
static_assert(!is_subsort_v<Refined<non_zero, int>, Refined<non_negative, int>>);

static_assert(is_subsort_v<Refined<bounded_above<8u>, unsigned>, Refined<bounded_above<16u>, unsigned>>);
static_assert(!is_subsort_v<Refined<bounded_above<16u>, unsigned>, Refined<bounded_above<8u>, unsigned>>);

static_assert(is_subsort_v<Refined<in_range<10, 20>, int>, Refined<in_range<0, 100>, int>>);
static_assert(!is_subsort_v<Refined<in_range<0, 100>, int>, Refined<in_range<10, 20>, int>>);
static_assert(!is_subsort_v<Refined<in_range<0, 10>, int>, Refined<in_range<20, 30>, int>>);

static_assert(is_subsort_v<Refined<in_range<0, 100>, int>, Refined<bounded_above<100>, int>>);

static_assert(is_subsort_v<Refined<aligned<64>, void*>, Refined<aligned<32>, void*>>);
static_assert(is_subsort_v<Refined<aligned<32>, void*>, Refined<aligned<8>, void*>>);
static_assert(!is_subsort_v<Refined<aligned<8>, void*>, Refined<aligned<32>, void*>>);

static_assert(is_subsort_v<Refined<positive, int>, Refined<positive, int>>);

static_assert(is_subtype_sync_v<Send<Refined<positive, int>, End>, Send<int, End>>);

// A sender promising only a bare value cannot stand where the refined
// send is expected, because the receiver at that position is entitled
// to the predicate and the sender has nothing to prove it with.
static_assert(!is_subtype_sync_v<Send<int, End>, Send<Refined<positive, int>, End>>);

// The direction inverts on a receive.  A receiver of bare values stands
// where a receiver of refined values is expected, because it accepts
// everything the other would have accepted.  The refined receiver
// cannot stand the other way round, since it would reject values the
// position permits.

static_assert(is_subtype_sync_v<Recv<int, End>, Recv<Refined<positive, int>, End>>);

static_assert(!is_subtype_sync_v<Recv<Refined<positive, int>, End>, Recv<int, End>>);

static_assert(is_subtype_sync_v<Send<Tagged<int, source::Sanitized>, End>, Send<int, End>>);

static_assert(!is_subtype_sync_v<Send<Tagged<int, source::External>, End>, Send<int, End>>);

static_assert(is_subtype_sync_v<Send<Tagged<int, vessel_trust::Validated>, End>, Send<int, End>>);

static_assert(!is_subtype_sync_v<Send<Tagged<int, vessel_trust::FromPytorch>, End>, Send<int, End>>);

static_assert(is_subtype_sync_v<Recv<int, End>, Recv<Tagged<int, source::Sanitized>, End>>);

static_assert(is_subtype_sync_v<Loop<Send<Refined<positive, int>, Continue>>, Loop<Send<int, Continue>>>);

static_assert(is_subtype_sync_v<Send<Refined<positive, int>, End>, Send<Refined<non_negative, int>, End>>);

static_assert(!is_subtype_sync_v<Send<Refined<non_negative, int>, End>, Send<Refined<positive, int>, End>>);

static_assert(is_subtype_sync_v<Recv<Refined<non_negative, int>, End>, Recv<Refined<positive, int>, End>>);

static_assert(!is_subtype_sync_v<Recv<Refined<positive, int>, End>, Recv<Refined<non_negative, int>, End>>);

static_assert(is_subtype_sync_v<Send<Refined<bounded_above<1024u>, unsigned>, End>,
                                Send<Refined<bounded_above<4096u>, unsigned>, End>>);

static_assert(
    is_subtype_sync_v<Loop<Send<Refined<positive, int>, Continue>>, Loop<Send<Refined<non_negative, int>, Continue>>>);

static_assert(
    is_subtype_sync_v<Select<Send<Refined<positive, int>, End>, Send<Tagged<MemoryPlanByte, source::Sanitized>, End>>,
                      Select<Send<int, End>, Send<MemoryPlanByte, End>>>);

// Branch width and payload direction are separate rules under an
// Offer.  Here the branch shape is fixed on both sides, so only the
// payload moves, and it moves the way a receive payload does.
static_assert(is_subtype_sync_v<Offer<Recv<int, End>>, Offer<Recv<Refined<positive, int>, End>>>);

}  // namespace detail::payload_subsort_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
