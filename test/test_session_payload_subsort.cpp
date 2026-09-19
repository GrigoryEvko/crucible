// Provenance does not stop at the validator. The type system carries it
// through the protocol steps that follow, so an internal caller cannot
// consume a value that was never validated.
//
// Two kinds of claim are made below. Some wrappers erase into the bare
// payload and propagate through a protocol position; others deliberately do
// not, and those non-axioms are what hold the boundary discipline up.
//
// The runtime half walks one flow: an adapter takes a value tagged with its
// foreign origin, runs the validator, and hands the internal pipeline a value
// tagged as validated.

#include <crucible/sessions/SessionPayloadSubsort.h>
#include <crucible/safety/_RefinedAlgebra.h>

#include <cstdio>
#include <expected>
#include <span>
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

// The header runs its own self-test. These assertions repeat it on purpose,
// from a translation unit that consumes the header, so a regression surfaces
// where a consumer would meet it rather than inside the framework.

// A refinement erases into the bare payload through a send position.
static_assert(is_subtype_sync_v<Send<Refined<positive, int>, End>, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<int, End>, Send<Refined<positive, int>, End>>);

// A sanitized tag erases the same way.
static_assert(is_subtype_sync_v<Send<Tagged<int, source::Sanitized>, End>, Send<int, End>>);

// An external tag does not, or the validator could be walked around.
static_assert(!is_subtype_sync_v<Send<Tagged<int, source::External>, End>, Send<int, End>>);

// Validated erases; the foreign origin it came from does not.
static_assert(is_subtype_sync_v<Send<Tagged<int, vessel_trust::Validated>, End>, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<Tagged<int, vessel_trust::FromPytorch>, End>, Send<int, End>>);

// Trust, access mode and version carry content of their own, and none of it
// may erase silently, so none of the three is an axiom.
static_assert(!is_subtype_sync_v<Send<Tagged<int, trust::Verified>, End>, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<Tagged<int, access::WriteOnce>, End>, Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<Tagged<int, version::V<1>>, End>, Send<int, End>>);

// A receive position reverses the direction. A receiver of the bare payload
// stands in for one expecting the sanitized tag, since it is content with the
// looser type.
static_assert(is_subtype_sync_v<Recv<int, End>, Recv<Tagged<int, source::Sanitized>, End>>);

// The relation reaches a loop body as well.
static_assert(is_subtype_sync_v<Loop<Send<Tagged<int, vessel_trust::Validated>, Continue>>, Loop<Send<int, Continue>>>);

// One refinement subsumes another when its predicate implies the other's.
// Both variances are covered here, and the reverse direction is refused.

static_assert(is_subtype_sync_v<Send<Refined<positive, int>, End>, Send<Refined<non_negative, int>, End>>);

static_assert(!is_subtype_sync_v<Send<Refined<non_negative, int>, End>, Send<Refined<positive, int>, End>>);

static_assert(is_subtype_sync_v<Recv<Refined<non_negative, int>, End>, Recv<Refined<positive, int>, End>>);

// A parameterised predicate behaves the same: the tighter ceiling subsumes
// the looser one.
static_assert(is_subtype_sync_v<Send<Refined<bounded_above<256u>, unsigned>, End>,
                                Send<Refined<bounded_above<1024u>, unsigned>, End>>);

// And it reaches through a loop.
static_assert(is_subtype_sync_v<Loop<Send<Refined<aligned<64>, void*>, Continue>>,
                                Loop<Send<Refined<aligned<32>, void*>, Continue>>>);

// A range inside another subsumes it. Disjoint ranges do not.
static_assert(is_subtype_sync_v<Send<Refined<in_range<10, 20>, int>, End>, Send<Refined<in_range<0, 100>, int>, End>>);
static_assert(!is_subtype_sync_v<Send<Refined<in_range<0, 10>, int>, End>, Send<Refined<in_range<20, 30>, int>, End>>);

// An assertion on the implication trait alone proves the trait fires. What
// consumes it is the subsort specialisation, which additionally demands the
// two predicates be distinct types, and which callers reach only through a
// payload position. A guard added to that specialisation could reject some
// shape of predicate and break propagation while every trait-level assertion
// still passed, so each axiom below is exercised end to end instead, with a
// soundness case wherever the axiom has a side condition.

// A null check and a zero check mean the same thing for a pointer, so the
// implication runs both ways.
static_assert(is_subtype_sync_v<Send<Refined<non_null, int*>, End>, Send<Refined<non_zero, int*>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<non_zero, int*>, End>, Send<Refined<non_null, int*>, End>>);

// A minimum length of one or more implies non-emptiness.
static_assert(
    is_subtype_sync_v<Send<Refined<length_ge<1>, std::span<int>>, End>, Send<Refined<non_empty, std::span<int>>, End>>);
static_assert(
    is_subtype_sync_v<Send<Refined<length_ge<8>, std::span<int>>, End>, Send<Refined<non_empty, std::span<int>>, End>>);
// A minimum of zero is vacuous, since the size type is unsigned. An empty
// container satisfies it and fails non-emptiness, so it must not propagate.
static_assert(!is_subtype_sync_v<Send<Refined<length_ge<0>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);

// A range whose floor is at zero or above implies non-negativity.
static_assert(is_subtype_sync_v<Send<Refined<in_range<0, 100>, int>, End>, Send<Refined<non_negative, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>, Send<Refined<non_negative, int>, End>>);
// A floor below zero must not.
static_assert(!is_subtype_sync_v<Send<Refined<in_range<-5, 100>, int>, End>, Send<Refined<non_negative, int>, End>>);

// A higher floor implies a lower one.
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<10>, int>, End>, Send<Refined<bounded_below<5>, int>, End>>);
// The reverse does not hold.
static_assert(
    !is_subtype_sync_v<Send<Refined<bounded_below<5>, int>, End>, Send<Refined<bounded_below<10>, int>, End>>);

// A range's floor is a lower bound, dual to its ceiling being an upper one.
static_assert(is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>, Send<Refined<bounded_below<5>, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<in_range<0, 200>, int>, End>, Send<Refined<bounded_below<0>, int>, End>>);

// A floor at zero or above implies non-negativity.
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<0>, int>, End>, Send<Refined<non_negative, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<5>, int>, End>, Send<Refined<non_negative, int>, End>>);
// A negative floor does not.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<-5>, int>, End>, Send<Refined<non_negative, int>, End>>);

// A floor at one or above implies positivity.
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<1>, int>, End>, Send<Refined<positive, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<10>, int>, End>, Send<Refined<positive, int>, End>>);
// A floor at zero admits zero itself, which is not positive.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<0>, int>, End>, Send<Refined<positive, int>, End>>);

// One case suffices to witness that a receive position reaches the same fold
// with the direction reversed.
static_assert(is_subtype_sync_v<Recv<Refined<non_negative, int>, End>, Recv<Refined<bounded_below<5>, int>, End>>);

// And that a loop's repeated payload position reaches it too.
static_assert(is_subtype_sync_v<Loop<Send<Refined<bounded_below<10>, int>, Continue>>,
                                Loop<Send<Refined<positive, int>, Continue>>>);

// The subsort specialisation demands a direct implication and composes
// nothing transitively. A chain from a range through a lower bound to
// positivity is therefore unreachable unless the range-to-positive step
// exists as an axiom of its own. The same holds for the chain from a lower
// bound through positivity to non-zero.
//
// Both bridges exist, and each is witnessed here at the boundary value of
// its side condition, along with the case just past it that must fail.

// A range whose floor is one or above is positive.
static_assert(is_subtype_sync_v<Send<Refined<in_range<1, 100>, int>, End>, Send<Refined<positive, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>, Send<Refined<positive, int>, End>>);
// A floor at zero admits zero.
static_assert(!is_subtype_sync_v<Send<Refined<in_range<0, 100>, int>, End>, Send<Refined<positive, int>, End>>);

// A lower bound of one or above is non-zero.
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<1>, int>, End>, Send<Refined<non_zero, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<bounded_below<10>, int>, End>, Send<Refined<non_zero, int>, End>>);
// A bound of zero admits zero.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<0>, int>, End>, Send<Refined<non_zero, int>, End>>);

// Non-zero is the union of two half-lines, so a range implies it from either
// side: a floor at one or above, or a ceiling at minus one or below. Both
// branches are witnessed, along with the ranges that straddle zero.

// The positive branch.
static_assert(is_subtype_sync_v<Send<Refined<in_range<1, 100>, int>, End>, Send<Refined<non_zero, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>, Send<Refined<non_zero, int>, End>>);

// The negative branch.
static_assert(is_subtype_sync_v<Send<Refined<in_range<-100, -1>, int>, End>, Send<Refined<non_zero, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<in_range<-100, -5>, int>, End>, Send<Refined<non_zero, int>, End>>);

// A range that contains zero admits it.
static_assert(!is_subtype_sync_v<Send<Refined<in_range<0, 100>, int>, End>, Send<Refined<non_zero, int>, End>>);
static_assert(!is_subtype_sync_v<Send<Refined<in_range<-5, 5>, int>, End>, Send<Refined<non_zero, int>, End>>);
static_assert(!is_subtype_sync_v<Send<Refined<in_range<-100, 0>, int>, End>, Send<Refined<non_zero, int>, End>>);

// The same shape on the size axis. A producer of a fixed-width vector feeds a
// consumer that asks only for a minimum length, or for a non-empty container,
// and neither side re-validates.

// The bound is met exactly.
static_assert(is_subtype_sync_v<Send<Refined<exact_size<8>, std::span<int>>, End>,
                                Send<Refined<length_ge<8>, std::span<int>>, End>>);

// The bound is exceeded.
static_assert(is_subtype_sync_v<Send<Refined<exact_size<8>, std::span<int>>, End>,
                                Send<Refined<length_ge<4>, std::span<int>>, End>>);

// The bound is vacuous.
static_assert(is_subtype_sync_v<Send<Refined<exact_size<1>, std::span<int>>, End>,
                                Send<Refined<length_ge<0>, std::span<int>>, End>>);

// A fixed size below the bound cannot meet it.
static_assert(!is_subtype_sync_v<Send<Refined<exact_size<4>, std::span<int>>, End>,
                                 Send<Refined<length_ge<8>, std::span<int>>, End>>);

// A size of one is the boundary for non-emptiness.
static_assert(is_subtype_sync_v<Send<Refined<exact_size<1>, std::span<int>>, End>,
                                Send<Refined<non_empty, std::span<int>>, End>>);

static_assert(is_subtype_sync_v<Send<Refined<exact_size<8>, std::span<int>>, End>,
                                Send<Refined<non_empty, std::span<int>>, End>>);

// A size of zero is an empty container.
static_assert(!is_subtype_sync_v<Send<Refined<exact_size<0>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);

// Divisibility follows the alignment shape on integer modulo: a trip count
// divisible by the widest vector width also satisfies every narrower one, so
// a producer gated at the widest feeds any consumer below it unchecked.

// Reflexive.
static_assert(is_subtype_sync_v<Send<Refined<divisible_by<4>, int>, End>, Send<Refined<divisible_by<4>, int>, End>>);

// Sixteen lanes down to eight and four.
static_assert(is_subtype_sync_v<Send<Refined<divisible_by<16>, int>, End>, Send<Refined<divisible_by<8>, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<divisible_by<16>, int>, End>, Send<Refined<divisible_by<4>, int>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<divisible_by<8>, int>, End>, Send<Refined<divisible_by<4>, int>, End>>);

// Six divides neither into nor out of four.
static_assert(!is_subtype_sync_v<Send<Refined<divisible_by<6>, int>, End>, Send<Refined<divisible_by<4>, int>, End>>);

// And the coarser divisor does not imply the finer one.
static_assert(!is_subtype_sync_v<Send<Refined<divisible_by<4>, int>, End>, Send<Refined<divisible_by<8>, int>, End>>);

// Alignment carries the same side condition as divisibility, so it earns the
// same witness density. The hardware chain runs from a cache line down
// through the vector widths to a machine word, and a pointer pinned at the
// strongest alignment satisfies every requirement below it.

// Reflexive.
static_assert(is_subtype_sync_v<Send<Refined<aligned<64>, void*>, End>, Send<Refined<aligned<64>, void*>, End>>);

static_assert(is_subtype_sync_v<Send<Refined<aligned<64>, void*>, End>, Send<Refined<aligned<16>, void*>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<aligned<32>, void*>, End>, Send<Refined<aligned<8>, void*>, End>>);
static_assert(is_subtype_sync_v<Send<Refined<aligned<16>, void*>, End>, Send<Refined<aligned<4>, void*>, End>>);

// The looser alignment does not imply the tighter one.
static_assert(!is_subtype_sync_v<Send<Refined<aligned<8>, void*>, End>, Send<Refined<aligned<64>, void*>, End>>);

// Nor does the larger value help when it is not a multiple of the smaller.
static_assert(!is_subtype_sync_v<Send<Refined<aligned<8>, void*>, End>, Send<Refined<aligned<3>, void*>, End>>);

// On the receiving side the direction reverses. A receiver asking for less
// alignment than the peer supplies is sound; one asking for more is not.
static_assert(is_subtype_sync_v<Recv<Refined<aligned<16>, void*>, End>, Recv<Refined<aligned<64>, void*>, End>>);
static_assert(!is_subtype_sync_v<Recv<Refined<aligned<64>, void*>, End>, Recv<Refined<aligned<16>, void*>, End>>);

// The receiving side of the modular and size axes, on the same pattern as
// alignment above. A receiver that accepts the looser predicate stands in for
// one at the tighter position, because every value the tighter position can
// deliver is one the looser receiver already handles. The reverse fails: the
// tighter receiver would turn away values the looser position admits.
//
// These cases are what would red if the receive position were ever given the
// send position's variance. Send and receive behaving alike is a classic
// unsoundness in a session-type implementation.

static_assert(is_subtype_sync_v<Recv<Refined<divisible_by<8>, int>, End>, Recv<Refined<divisible_by<16>, int>, End>>);
static_assert(!is_subtype_sync_v<Recv<Refined<divisible_by<16>, int>, End>, Recv<Refined<divisible_by<8>, int>, End>>);

// A receiver asking only for a minimum length handles every fixed-size value.
static_assert(is_subtype_sync_v<Recv<Refined<length_ge<4>, std::span<int>>, End>,
                                Recv<Refined<exact_size<8>, std::span<int>>, End>>);
// A receiver demanding one exact size would turn away the other sizes the
// minimum-length position admits.
static_assert(!is_subtype_sync_v<Recv<Refined<exact_size<8>, std::span<int>>, End>,
                                 Recv<Refined<length_ge<4>, std::span<int>>, End>>);

static_assert(is_subtype_sync_v<Recv<Refined<non_empty, std::span<int>>, End>,
                                Recv<Refined<exact_size<8>, std::span<int>>, End>>);
static_assert(!is_subtype_sync_v<Recv<Refined<exact_size<8>, std::span<int>>, End>,
                                 Recv<Refined<non_empty, std::span<int>>, End>>);

// The value-range axis is the most populous parametric family, and it has
// three subsumption shapes: one range inside another, a range implying its
// own ceiling as an upper bound, and a range with a floor of one implying
// positivity. Each needs its own receive-side witness.

static_assert(is_subtype_sync_v<Recv<Refined<in_range<0, 100>, int>, End>, Recv<Refined<in_range<10, 20>, int>, End>>);
static_assert(!is_subtype_sync_v<Recv<Refined<in_range<10, 20>, int>, End>, Recv<Refined<in_range<0, 100>, int>, End>>);

// A receiver of any positive value handles what a range with a floor of one
// delivers.
static_assert(is_subtype_sync_v<Recv<Refined<positive, int>, End>, Recv<Refined<in_range<1, 100>, int>, End>>);
// The reverse would turn away every positive value past the range's ceiling.
static_assert(!is_subtype_sync_v<Recv<Refined<in_range<1, 100>, int>, End>, Recv<Refined<positive, int>, End>>);

static_assert(is_subtype_sync_v<Recv<Refined<bounded_below<5>, int>, End>, Recv<Refined<bounded_below<10>, int>, End>>);
// The tighter receiver would turn away the values between the two floors.
static_assert(
    !is_subtype_sync_v<Recv<Refined<bounded_below<10>, int>, End>, Recv<Refined<bounded_below<5>, int>, End>>);

// The shape of a value that arrives at the boundary as raw bytes from a
// frontend.
struct DispatchRequest {
    int schema_hash;
    long shape0;
    long shape1;
};

struct MockHandle {
    int request_id;
    bool succeeded;
};

// The validator is the only place that produces a value tagged as validated,
// which is what makes the tag mean anything. A production one would look the
// schema hash up, bounds-check the shape and range-check the enums. The
// result goes through the error channel so a caller can branch on a failure.

enum class DispatchValidationError : int {
    SchemaUnknown,
    ShapeMalformed,
};

[[nodiscard]] auto validate(Tagged<DispatchRequest, vessel_trust::FromPytorch>&& raw)
    -> std::expected<Tagged<DispatchRequest, vessel_trust::Validated>, DispatchValidationError> {
    const DispatchRequest& req = raw.value();
    if (req.schema_hash == 0) return std::unexpected{DispatchValidationError::SchemaUnknown};
    if (req.shape0 < 0 || req.shape1 < 0) return std::unexpected{DispatchValidationError::ShapeMalformed};

    return std::move(raw).template retag<vessel_trust::Validated>();
}

// The parameter type carries the provenance, and there is no overload taking
// the bare request, so the validator above is the only way in.
[[nodiscard]] MockHandle internal_dispatch(Tagged<DispatchRequest, vessel_trust::Validated> req) {
    return MockHandle{
        .request_id = req.value().schema_hash,
        .succeeded = true,
    };
}

// An older interface taking the bare request still composes, which is what
// makes the discipline adoptable one call site at a time rather than all at
// once.
[[nodiscard]] MockHandle legacy_internal_dispatch(DispatchRequest req) {
    return MockHandle{
        .request_id = req.schema_hash + 1000,
        .succeeded = true,
    };
}

int run_validator_happy_path() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{DispatchRequest{42, 64, 128}};

    auto validated = validate(std::move(raw));
    if (!validated) return 1;

    auto handle = internal_dispatch(std::move(*validated));
    if (handle.request_id != 42) return 2;
    if (!handle.succeeded) return 3;
    return 0;
}

int run_validator_rejects_unknown_schema() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{
        DispatchRequest{0, 64, 128}};  // a schema hash of zero is the sentinel
    auto validated = validate(std::move(raw));
    if (validated) return 1;
    if (validated.error() != DispatchValidationError::SchemaUnknown) return 2;
    return 0;
}

int run_validator_rejects_malformed_shape() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{DispatchRequest{42, -1, 128}};
    auto validated = validate(std::move(raw));
    if (validated) return 1;
    if (validated.error() != DispatchValidationError::ShapeMalformed) return 2;
    return 0;
}

// Dropping the tag to reach a bare-request consumer is spelled out with
// into(), so every place the provenance is deliberately discarded can be
// found by searching for that call.
int run_subsumption_via_explicit_into() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{DispatchRequest{7, 32, 32}};

    auto validated = validate(std::move(raw));
    if (!validated) return 1;

    auto handle = legacy_internal_dispatch(std::move(*validated).into());
    if (handle.request_id != 1007) return 2;
    if (!handle.succeeded) return 3;
    return 0;
}

// A framing layer publishes payloads whose length carries a tight ceiling,
// proven where the frame is built. A consumer downstream declares a higher
// ceiling of its own, because it takes frames from several producers with
// different limits. The producer's protocol is a subtype of the consumer's
// exactly when its ceiling is the lower of the two.
//
// Without that relation the producer would have to widen its own bound and
// lose what it proved, or the consumer would have to widen its declared
// protocol and lose what it meant. With it, the same producer handle types as
// the consumer position directly, with no cast and nothing to run.

constexpr uint32_t MAX_FRAME_TIGHT = 1024;  // the producer's ceiling
constexpr uint32_t MAX_FRAME_LOOSE = 4096;  // the consumer's

using TightProto = Send<Refined<bounded_above<MAX_FRAME_TIGHT>, uint32_t>, End>;
using LooseProto = Send<Refined<bounded_above<MAX_FRAME_LOOSE>, uint32_t>, End>;

// The relation holds at compile time. The body below follows the value
// through, so the subsumption is seen to preserve it and to cost the consumer
// no re-validation.
static_assert(is_subtype_sync_v<TightProto, LooseProto>);

int run_predicate_strengthening_through_session() {
    Refined<bounded_above<MAX_FRAME_TIGHT>, uint32_t> tight_len{777u};

    // A value satisfying the tighter ceiling satisfies the looser one, so the
    // wrapper's construction contract holds at the looser type without a
    // check. In a session this re-wrap is what the send position's covariance
    // does implicitly; unwrapping and rewrapping by hand here is only so the
    // test can look at the value.
    Refined<bounded_above<MAX_FRAME_LOOSE>, uint32_t> loose_len{tight_len.value()};

    if (loose_len.value() != 777u) return 1;

    // Going the other way needs the predicate checked again, since a value
    // under the looser ceiling may sit above the tighter one. The value below
    // stays under both, so the construction contract holds rather than firing
    // and aborting the process.
    Refined<bounded_above<MAX_FRAME_LOOSE>, uint32_t> within_tight_too{500u};
    Refined<bounded_above<MAX_FRAME_TIGHT>, uint32_t> renarrowed{within_tight_too.value()};
    if (renarrowed.value() != 500u) return 2;

    return 0;
}

// The same movement between two distinct predicates rather than two
// parameters of one predicate.
int run_positive_strengthens_to_non_negative() {
    // A positive value is a non-negative one, so the payload position can
    // change between the two at no cost. The check is that the value survives.
    Refined<positive, int> p{42};
    Refined<non_negative, int> n{p.value()};
    if (n.value() != 42) return 1;

    Refined<power_of_two, std::size_t> pot{64u};
    Refined<non_zero, std::size_t> nz{pot.value()};
    if (nz.value() != 64u) return 2;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_validator_happy_path(); rc != 0) return rc;
    if (int rc = run_validator_rejects_unknown_schema(); rc != 0) return 100 + rc;
    if (int rc = run_validator_rejects_malformed_shape(); rc != 0) return 200 + rc;
    if (int rc = run_subsumption_via_explicit_into(); rc != 0) return 300 + rc;
    if (int rc = run_predicate_strengthening_through_session(); rc != 0) return 400 + rc;
    if (int rc = run_positive_strengthens_to_non_negative(); rc != 0) return 500 + rc;

    std::puts("session_payload_subsort: validator + subsumption + "
              "predicate strengthening + non-axiom rejection OK");
    return 0;
}
