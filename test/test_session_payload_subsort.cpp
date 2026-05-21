// Runtime + compile-time harness for safety/SessionPayloadSubsort.h
// (task #395, SAFEINT-S6 from misc/24_04_2026_safety_integration.md §6).
//
// Coverage:
//   * Compile-time:  every is_subsort axiom (positive AND negative
//     cases) plus protocol-level Send/Recv subtype propagation.
//   * Compile-time:  the deliberate non-axioms (External / FromUser /
//     FromPytorch / trust::* / access::* / version::*) verified to
//     STAY non-axioms — these are the load-bearing FFI / trust-
//     boundary discipline.
//   * Runtime:       a worked FFI-flow scenario where a Vessel-style
//     adapter receives Tagged<T, vessel_trust::FromPytorch>, runs the
//     validator, produces Tagged<T, vessel_trust::Validated>, and the
//     internal pipeline accepts the result via subtype subsumption
//     into a position expecting bare T.
//
// Closes the documented FFI gap: provenance no longer ENDS at the
// validator — the type system carries it through subsequent protocol
// steps and rejects internal callers that try to consume unvalidated
// values.
//
// See also:
//   * misc/24_04_2026_safety_integration.md §6, §7, §23 (Vessel FFI
//     boundary discipline), Part VIII §39 (the deliberate
//     non-axioms — Secret ⩽ T asymmetry, etc.)
//   * include/crucible/sessions/SessionPayloadSubsort.h (the axioms)
//   * include/crucible/sessions/SessionSubtype.h (the relation)

#include <crucible/sessions/SessionPayloadSubsort.h>
#include <crucible/safety/RefinedAlgebra.h>  // FIXY-U-163 — bounded_below

#include <cstdio>
#include <expected>
#include <span>        // FIXY-U-163 — span<int> in is_subsort witnesses
#include <utility>

namespace {

using namespace crucible::safety;
using namespace crucible::safety::proto;

// ── Compile-time witnesses (these duplicate the in-header self-tests
//    intentionally — keeping the test-side asserts ensures regressions
//    in the TU that USES the header surface clearly, not buried inside
//    the framework's internal self-test namespace) ────────────────────

// Refined ⩽ bare propagates through Send (covariance).
static_assert( is_subtype_sync_v<Send<Refined<positive, int>, End>,
                                 Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<int, End>,
                                 Send<Refined<positive, int>, End>>);

// Tagged Sanitized ⩽ bare propagates through Send.
static_assert( is_subtype_sync_v<Send<Tagged<int, source::Sanitized>, End>,
                                 Send<int, End>>);

// Tagged External does NOT propagate — defeats validator.
static_assert(!is_subtype_sync_v<Send<Tagged<int, source::External>, End>,
                                 Send<int, End>>);

// Tagged Validated propagates; FromPytorch does not.
static_assert( is_subtype_sync_v<Send<Tagged<int, vessel_trust::Validated>, End>,
                                 Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<Tagged<int, vessel_trust::FromPytorch>, End>,
                                 Send<int, End>>);

// trust::* / access::* / version::* are deliberately non-axioms
// (epistemic / mode / version content must not silently erase).
static_assert(!is_subtype_sync_v<Send<Tagged<int, trust::Verified>,    End>,
                                 Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<Tagged<int, access::WriteOnce>,  End>,
                                 Send<int, End>>);
static_assert(!is_subtype_sync_v<Send<Tagged<int, version::V<1>>,      End>,
                                 Send<int, End>>);

// Recv contravariance: bare ⩽ Tagged Sanitized (the receiver of bare
// can stand for one expecting Sanitized — they're happy with the
// looser type).
static_assert( is_subtype_sync_v<Recv<int, End>,
                                 Recv<Tagged<int, source::Sanitized>, End>>);

// Loop body propagates the wrapper subsorts.
static_assert( is_subtype_sync_v<
    Loop<Send<Tagged<int, vessel_trust::Validated>, Continue>>,
    Loop<Send<int, Continue>>>);

// ── Cross-predicate strengthening (#227 + §22 wiring) ──────────────
//
// Refined<P, T> ⩽ Refined<Q, T> when implies_v<P, Q>.  Both directions
// of variance covered; reverse rejected.

static_assert( is_subtype_sync_v<Send<Refined<positive, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);

static_assert(!is_subtype_sync_v<Send<Refined<non_negative, int>, End>,
                                 Send<Refined<positive, int>, End>>);

static_assert( is_subtype_sync_v<Recv<Refined<non_negative, int>, End>,
                                 Recv<Refined<positive, int>, End>>);

// Parameterised: BoundedAbove tighter ⩽ looser via Send.
static_assert( is_subtype_sync_v<Send<Refined<bounded_above<256u>, unsigned>, End>,
                                 Send<Refined<bounded_above<1024u>, unsigned>, End>>);

// Parameterised: Aligned<64> ⩽ Aligned<32> through a Loop.
static_assert( is_subtype_sync_v<
    Loop<Send<Refined<aligned<64>, void*>, Continue>>,
    Loop<Send<Refined<aligned<32>, void*>, Continue>>>);

// InRange tighter ⩽ looser; disjoint ranges rejected.
static_assert( is_subtype_sync_v<Send<Refined<in_range<10, 20>, int>, End>,
                                 Send<Refined<in_range<0, 100>, int>, End>>);
static_assert(!is_subtype_sync_v<Send<Refined<in_range<0, 10>, int>, End>,
                                 Send<Refined<in_range<20, 30>, int>, End>>);

// ── FIXY-U-163 — end-to-end is_subsort witnesses ───────────────────
//
// The U-159b → U-162 thread accumulated 7+ new predicate_implies
// axioms with only file-scope `implies_v` witnesses.  Those prove the
// TRAIT fires.  The PRODUCTION consumer is SessionPayloadSubsort's
// `is_subsort<Refined<P,T>, Refined<Q,T>>` partial spec (requires
// `implies_v<P, Q> && !std::is_same_v<decltype(P), decltype(Q)>`),
// reached at runtime via `is_subtype_sync_v` through Send/Recv/Loop's
// covariant/contravariant payload positions.
//
// A future refactor of is_subsort's requires-clause (e.g., adding a
// guard that incidentally rejects struct-form predicates) would
// silently break consumer-level propagation while the implies_v
// witnesses still pass.  The end-to-end asserts below catch that
// regression class.  Each new axiom from U-159b / U-160 / U-161 /
// U-162 is exercised through Send (covariant payload) with at least
// one positive case + one soundness witness where applicable.

// U-159b: non_null ⇔ non_zero bidirectional for pointer T
static_assert( is_subtype_sync_v<Send<Refined<non_null, int*>, End>,
                                 Send<Refined<non_zero, int*>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<non_zero, int*>, End>,
                                 Send<Refined<non_null, int*>, End>>);

// U-159b: LengthGe<N> ⇒ non_empty for N ≥ 1 (parameterised bridge)
static_assert( is_subtype_sync_v<Send<Refined<length_ge<1>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<length_ge<8>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);
// FIXY-U-164: missing soundness witness from U-163 — length_ge<0> is
// vacuous (size_t unsigned, x ≥ 0 always-true), so an empty container
// passes length_ge<0> but FAILS non_empty.  The implies_v witness for
// this gate exists at file scope (Refined.h's `!implies_v<length_ge<0>,
// non_empty>`), but the consumer-level is_subsort witness was missed
// in U-163.  Closing here completes U-163's 100% premise.
static_assert(!is_subtype_sync_v<Send<Refined<length_ge<0>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);

// U-161: InRange<L, H> ⇒ non_negative when L ≥ 0
static_assert( is_subtype_sync_v<Send<Refined<in_range<0, 100>, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);
// Soundness gate: L < 0 must NOT propagate to non_negative.
static_assert(!is_subtype_sync_v<Send<Refined<in_range<-5, 100>, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);

// U-162: BoundedBelow<N> ⇒ BoundedBelow<M> when N ≥ M (transitivity)
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<10>, int>, End>,
                                 Send<Refined<bounded_below<5>, int>, End>>);
// Soundness gate: looser must NOT propagate to tighter.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<5>, int>, End>,
                                 Send<Refined<bounded_below<10>, int>, End>>);

// U-162: InRange<L, H> ⇒ BoundedBelow<L> (dual to InRange⇒BoundedAbove<H>)
static_assert( is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>,
                                 Send<Refined<bounded_below<5>, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<in_range<0, 200>, int>, End>,
                                 Send<Refined<bounded_below<0>, int>, End>>);

// U-162: BoundedBelow<N> ⇒ non_negative when N ≥ 0
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<0>, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<5>, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);
// Soundness gate: N < 0 must NOT propagate to non_negative.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<-5>, int>, End>,
                                 Send<Refined<non_negative, int>, End>>);

// U-162: BoundedBelow<N> ⇒ positive when N ≥ 1
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<1>, int>, End>,
                                 Send<Refined<positive, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<10>, int>, End>,
                                 Send<Refined<positive, int>, End>>);
// Soundness gate: N=0 admits x=0 (NOT positive); must NOT propagate.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<0>, int>, End>,
                                 Send<Refined<positive, int>, End>>);

// Recv contravariance: payload position reverses direction.  Use one
// flagship case to witness Recv routes through the same lattice fold.
static_assert( is_subtype_sync_v<Recv<Refined<non_negative, int>, End>,
                                 Recv<Refined<bounded_below<5>, int>, End>>);

// Loop composition: lattice fires through repeated payload position.
static_assert( is_subtype_sync_v<
    Loop<Send<Refined<bounded_below<10>, int>, Continue>>,
    Loop<Send<Refined<positive, int>, Continue>>>);

// ── FIXY-U-164 — transitive-closure direct bridges ─────────────────
//
// The is_subsort partial spec in SessionPayloadSubsort.h requires
// DIRECT implies_v — there is no recursive transitive composition.
// So the chain `in_range<5, 100> ⇒ bounded_below<5> ⇒ positive` is
// NOT automatically reachable through is_subsort without an explicit
// `in_range<L, H> ⇒ positive` direct bridge axiom.  Similarly the
// chain `bounded_below<N> ⇒ positive ⇒ non_zero` needs a direct
// `bounded_below<N> ⇒ non_zero` bridge.
//
// U-164 ships both bridges (Refined.h + RefinedAlgebra.h).  Witness
// each through is_subtype_sync_v at the boundary cardinality + the
// soundness gate that proves the L ≥ 1 / N ≥ 1 requires-clause is
// load-bearing.

// InRange<L, H> ⇒ positive direct bridge (L ≥ 1):
static_assert( is_subtype_sync_v<Send<Refined<in_range<1, 100>, int>, End>,
                                 Send<Refined<positive, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>,
                                 Send<Refined<positive, int>, End>>);
// Soundness: L=0 admits x=0 (NOT positive); must NOT propagate.
static_assert(!is_subtype_sync_v<Send<Refined<in_range<0, 100>, int>, End>,
                                 Send<Refined<positive, int>, End>>);

// BoundedBelow<N> ⇒ non_zero direct bridge (N ≥ 1):
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<1>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<bounded_below<10>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);
// Soundness: N=0 admits x=0 (IS zero); must NOT propagate.
static_assert(!is_subtype_sync_v<Send<Refined<bounded_below<0>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);

// ── FIXY-U-165 — InRange ⇒ non_zero disjunctive bridge ─────────────
//
// non_zero admits a disjunctive gate `(L ≥ 1) ∨ (H ≤ -1)` because the
// predicate is the union-of-two-half-lines.  Witness BOTH branches +
// the load-bearing soundness gate where the range straddles 0.

// L ≥ 1 branch (positive range):
static_assert( is_subtype_sync_v<Send<Refined<in_range<1, 100>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<in_range<5, 100>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);

// H ≤ -1 branch (negative range — the load-bearing new direction):
static_assert( is_subtype_sync_v<Send<Refined<in_range<-100, -1>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<in_range<-100, -5>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);

// Soundness: range straddles 0 (admits x=0) must NOT propagate.
static_assert(!is_subtype_sync_v<Send<Refined<in_range<0, 100>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);
static_assert(!is_subtype_sync_v<Send<Refined<in_range<-5, 5>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);
static_assert(!is_subtype_sync_v<Send<Refined<in_range<-100, 0>, int>, End>,
                                 Send<Refined<non_zero, int>, End>>);

// ── FIXY-U-166 — ExactSize propagation lattice (end-to-end) ────────
//
// Witness ExactSize<N>⇒LengthGe<M> when N ≥ M AND ExactSize<N>⇒
// non_empty when N ≥ 1 flow through Send-covariance.  Symmetric to
// the BoundedBelow⇒positive/non_zero lattice but on the size axis:
// a fixed-shape SIMD-lane producer (Sized<8, span<int>>) feeds a
// generic container consumer (MinSize<4, span<int>> /
// NonEmpty<span<int>>) without re-validation.

// ExactSize ⇒ LengthGe boundary (N=M):
static_assert( is_subtype_sync_v<Send<Refined<exact_size<8>, std::span<int>>, End>,
                                 Send<Refined<length_ge<8>, std::span<int>>, End>>);

// ExactSize ⇒ LengthGe interior (N>M):
static_assert( is_subtype_sync_v<Send<Refined<exact_size<8>, std::span<int>>, End>,
                                 Send<Refined<length_ge<4>, std::span<int>>, End>>);

// ExactSize ⇒ LengthGe vacuous lower bound (M=0):
static_assert( is_subtype_sync_v<Send<Refined<exact_size<1>, std::span<int>>, End>,
                                 Send<Refined<length_ge<0>, std::span<int>>, End>>);

// ExactSize ⇒ LengthGe soundness (N<M must NOT propagate):
static_assert(!is_subtype_sync_v<Send<Refined<exact_size<4>, std::span<int>>, End>,
                                 Send<Refined<length_ge<8>, std::span<int>>, End>>);

// ExactSize ⇒ non_empty boundary (N=1):
static_assert( is_subtype_sync_v<Send<Refined<exact_size<1>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);

// ExactSize ⇒ non_empty interior (N>1):
static_assert( is_subtype_sync_v<Send<Refined<exact_size<8>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);

// Soundness: N=0 means c is empty; must NOT propagate to non_empty.
static_assert(!is_subtype_sync_v<Send<Refined<exact_size<0>, std::span<int>>, End>,
                                 Send<Refined<non_empty, std::span<int>>, End>>);

// ── FIXY-U-167 — DivisibleBy subsumption (end-to-end) ──────────────
//
// Witness DivisibleBy<N>⇒DivisibleBy<M> when M divides N flow through
// Send-covariance.  Mirrors the Aligned<N>⇒Aligned<M> path but on
// integer-modulo: a SIMD-lane trip count gated by divisible_by<16>
// (AVX-512) structurally strengthens to a consumer expecting
// divisible_by<8> (AVX2) or divisible_by<4> (SSE) without
// re-validation.

// Reflexive (N=M=4):
static_assert( is_subtype_sync_v<Send<Refined<divisible_by<4>, int>, End>,
                                 Send<Refined<divisible_by<4>, int>, End>>);

// SIMD chain — AVX-512 trip count satisfies AVX2 and SSE:
static_assert( is_subtype_sync_v<Send<Refined<divisible_by<16>, int>, End>,
                                 Send<Refined<divisible_by<8>, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<divisible_by<16>, int>, End>,
                                 Send<Refined<divisible_by<4>, int>, End>>);
static_assert( is_subtype_sync_v<Send<Refined<divisible_by<8>, int>, End>,
                                 Send<Refined<divisible_by<4>, int>, End>>);

// Soundness: M does NOT divide N must NOT propagate.
static_assert(!is_subtype_sync_v<Send<Refined<divisible_by<6>, int>, End>,
                                 Send<Refined<divisible_by<4>, int>, End>>);

// Soundness: looser divisor does NOT imply tighter divisor (N < M).
static_assert(!is_subtype_sync_v<Send<Refined<divisible_by<4>, int>, End>,
                                 Send<Refined<divisible_by<8>, int>, End>>);

// ── Runtime scenario: Vessel-FFI flow ──────────────────────────────

// Mock dispatch request — the kind of value that arrives at the FFI
// boundary as raw bytes from a frontend.
struct DispatchRequest {
    int  schema_hash;
    long shape0;
    long shape1;
};

// Internal mock-handle returned to the frontend.
struct MockHandle {
    int  request_id;
    bool succeeded;
};

// The validator: the only function in the codebase that produces
// Tagged<DispatchRequest, vessel_trust::Validated>.  In production
// this performs schema-hash lookup, shape bounds-check, dtype/device
// enum range check, etc.  Returns std::expected so callers can route
// validation errors through error branches.

enum class DispatchValidationError : int {
    SchemaUnknown,
    ShapeMalformed,
};

[[nodiscard]] auto validate(
    Tagged<DispatchRequest, vessel_trust::FromPytorch>&& raw)
    -> std::expected<Tagged<DispatchRequest, vessel_trust::Validated>,
                     DispatchValidationError>
{
    const DispatchRequest& req = raw.value();
    if (req.schema_hash == 0)            return std::unexpected{DispatchValidationError::SchemaUnknown};
    if (req.shape0 < 0 || req.shape1 < 0) return std::unexpected{DispatchValidationError::ShapeMalformed};

    // Retag — exactly the signature the §6 discipline requires.
    return std::move(raw).template retag<vessel_trust::Validated>();
}

// Internal API that ONLY accepts validated input.  Note the parameter
// type carries the provenance — there is no overload taking bare
// DispatchRequest; the only path to this function is through
// validate().  This is the load-bearing FFI gap-closure.

[[nodiscard]] MockHandle internal_dispatch(
    Tagged<DispatchRequest, vessel_trust::Validated> req)
{
    return MockHandle{
        .request_id = req.value().schema_hash,
        .succeeded  = true,
    };
}

// SUBSUMPTION TEST: a HYPOTHETICAL legacy internal API that takes
// bare DispatchRequest still composes — the Validated wrapper flows
// downward via subsumption.  In production we'd refactor away the
// bare-T overloads and require the typed wrapper everywhere; this
// shows that during the transition, the discipline is non-disruptive.

[[nodiscard]] MockHandle legacy_internal_dispatch(DispatchRequest req)
{
    return MockHandle{
        .request_id = req.schema_hash + 1000,
        .succeeded  = true,
    };
}

// ── Runtime: happy path through the validator ──────────────────────

int run_validator_happy_path() {
    // Simulate FFI input.
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{
        DispatchRequest{42, 64, 128}};

    auto validated = validate(std::move(raw));
    if (!validated) return 1;

    auto handle = internal_dispatch(std::move(*validated));
    if (handle.request_id != 42) return 2;
    if (!handle.succeeded)        return 3;
    return 0;
}

// ── Runtime: validator catches malformed input ─────────────────────

int run_validator_rejects_unknown_schema() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{
        DispatchRequest{0, 64, 128}};   // schema_hash=0 is the sentinel
    auto validated = validate(std::move(raw));
    if (validated)                                          return 1;
    if (validated.error() != DispatchValidationError::SchemaUnknown) return 2;
    return 0;
}

int run_validator_rejects_malformed_shape() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{
        DispatchRequest{42, -1, 128}};
    auto validated = validate(std::move(raw));
    if (validated)                                            return 1;
    if (validated.error() != DispatchValidationError::ShapeMalformed) return 2;
    return 0;
}

// ── Runtime: subsumption through the integration's discipline ──────
//
// A function expecting bare DispatchRequest accepts a Validated value
// AT THE SUBTYPE LEVEL via .into() that explicitly drops the tag.
// The discipline: every such call site is grep-discoverable as a
// .into() invocation, naming the place where the provenance is
// intentionally dropped.

int run_subsumption_via_explicit_into() {
    auto raw = Tagged<DispatchRequest, vessel_trust::FromPytorch>{
        DispatchRequest{7, 32, 32}};

    auto validated = validate(std::move(raw));
    if (!validated) return 1;

    // Explicit into() at the call-site for a legacy-style consumer.
    auto handle = legacy_internal_dispatch(std::move(*validated).into());
    if (handle.request_id != 1007) return 2;
    if (!handle.succeeded)         return 3;
    return 0;
}

// ── Runtime worked example: predicate strengthening across a session
//    (#227 + §22) ──────────────────────────────────────────────────────
//
// CNTP Layer-1 frame size discipline.  The framing layer publishes
// payloads whose lengths are Refined<bounded_above<MAX_FRAME_TIGHT>,
// uint32_t> — a TIGHT bound proven at the producer.  Downstream
// consumers carry their own ceiling (MAX_FRAME_LOOSE), often higher
// because they accept payloads from multiple producers with different
// constraints.  The session-type subsumption flows the tighter
// producer bound into the looser consumer position automatically:
//
//   Producer protocol: Send<Refined<bounded_above<TIGHT>, u32>, End>
//   Consumer protocol: Send<Refined<bounded_above<LOOSE>, u32>, End>
//                                                         ^^^^^^
//   Subtype relation:  Producer ⩽ Consumer  iff  TIGHT ≤ LOOSE
//                                                (predicate_implies)
//
// Without the wiring, the producer would have to declassify down to
// the looser bound (loss of information) or the consumer would have
// to widen its declared protocol (loss of intent).  With the wiring,
// the same producer handle CAN BE TYPED as the looser-bound consumer
// position by the framework — no runtime cost, no cast.

constexpr uint32_t MAX_FRAME_TIGHT = 1024;   // producer's tight bound
constexpr uint32_t MAX_FRAME_LOOSE = 4096;   // consumer's looser ceiling

using TightProto = Send<Refined<bounded_above<MAX_FRAME_TIGHT>,  uint32_t>, End>;
using LooseProto = Send<Refined<bounded_above<MAX_FRAME_LOOSE>,  uint32_t>, End>;

// The framework's subtype relation flows tighter → looser at compile
// time.  The runtime body below exercises the actual value flow,
// confirming the subsumption is information-preserving (the value
// transmitted equals the value received) and zero-overhead (no
// re-validation at the consumer).
static_assert(is_subtype_sync_v<TightProto, LooseProto>);

int run_predicate_strengthening_through_session() {
    // Producer side: build a length-bounded value with the TIGHT bound.
    Refined<bounded_above<MAX_FRAME_TIGHT>, uint32_t> tight_len{777u};

    // Subsumption: a value typed at the tighter bound IS-A value at
    // the looser bound.  The Refined constructor's contract holds —
    // any value satisfying the tighter predicate also satisfies the
    // looser one (by predicate_implies<bounded_above<TIGHT>,
    // bounded_above<LOOSE>>).  We extract via .value() (no cost)
    // and re-wrap at the looser type for the downstream interface.
    //
    // In a real session, this re-wrap happens implicitly via Send's
    // payload covariance — the compiler accepts the tighter handle
    // where the looser is named.  The .value() round-trip here is
    // for the test's runtime observation only.
    Refined<bounded_above<MAX_FRAME_LOOSE>, uint32_t> loose_len{tight_len.value()};

    if (loose_len.value() != 777u) return 1;

    // Reverse direction is rejected at compile time: a value at the
    // LOOSER bound (which might be 2000) cannot inhabit the TIGHTER
    // type without re-checking the predicate at runtime.  Demonstrate
    // by showing the Refined ctor's contract DOES fire when the value
    // would violate the tighter predicate (no UB; contract abort path
    // is exercised by the framework's contract-violation handler in
    // debug builds — here we just stay within the bound).
    Refined<bounded_above<MAX_FRAME_LOOSE>, uint32_t> within_tight_too{500u};
    Refined<bounded_above<MAX_FRAME_TIGHT>, uint32_t> renarrowed{within_tight_too.value()};
    if (renarrowed.value() != 500u) return 2;

    return 0;
}

// ── Runtime: predicate strengthening across distinct predicates ────

int run_positive_strengthens_to_non_negative() {
    // A value carrying the Positive refinement IS-A NonNegative value
    // by predicate_implies<positive, non_negative>.  The protocol
    // payload position changes from Refined<positive, int> to
    // Refined<non_negative, int> via Send covariance with no runtime
    // cost; here we observe the value is preserved.
    Refined<positive,     int> p{42};
    Refined<non_negative, int> n{p.value()};
    if (n.value() != 42)      return 1;

    // power_of_two ⇒ non_zero — same pattern.
    Refined<power_of_two, std::size_t> pot{64u};
    Refined<non_zero,     std::size_t> nz{pot.value()};
    if (nz.value() != 64u)    return 2;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_validator_happy_path();                       rc != 0) return rc;
    if (int rc = run_validator_rejects_unknown_schema();           rc != 0) return 100 + rc;
    if (int rc = run_validator_rejects_malformed_shape();          rc != 0) return 200 + rc;
    if (int rc = run_subsumption_via_explicit_into();              rc != 0) return 300 + rc;
    if (int rc = run_predicate_strengthening_through_session();    rc != 0) return 400 + rc;
    if (int rc = run_positive_strengthens_to_non_negative();       rc != 0) return 500 + rc;

    std::puts("session_payload_subsort: validator + subsumption + "
              "predicate strengthening + non-axiom rejection OK");
    return 0;
}
