// ── test_fixy_engaged — positive-compile sentinel for Phase A ──────
//
// Eight positive-compile witnesses exercising IsAccepted with
// different Tier × stance combinations.  No runtime body — every
// claim is a static_assert.  TU compile = green test.
//
// Per misc/16_05_2026_fixy.md §4 Phase A acceptance gate.  Witnesses:
//
//   1. All-strict (PureLinear S-Tier baseline)
//   2. PureCopy substitution                — Usage relaxation
//   3. IoFunction stance                    — Effect relaxation
//   4. CtCrypto stance                      — Effect+Usage combination
//   5. SecretConsumer stance                — implicit (default Security
//                                              is Classified — no relaxation)
//   6. PureLinear over Foundational dim T   — Refinement relaxation
//   7. PureLinear over Lifetime axis        — in_region relaxation
//   8. PureLinear over Versioned axis       — version relaxation

#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Default.h>

#include <tuple>
#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

// ─── Shared 20-strict scaffold ─────────────────────────────────────
//
// Helper variadic: AcceptAllExcept<RelaxAxis, Replacements...> = the
// AllStrict pack minus AcceptStrict<RelaxAxis>, plus Replacements.
//
// Phase A's IsAccepted needs every dim engaged, so each stance below
// is built by starting from "every dim accept-strict" and replacing
// one or more accept-strict markers with relaxations.

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Witness 1: PureLinear baseline — every axis accept-strict.
//
// fixy-H-05 follow-up: the wrapper-discipline `IsAccepted` auto-injects
// the Type-axis acceptance marker (per Grant.h's Dim 1 discipline:
// callers do not write it).  Each witness pack therefore engages only
// the 22 non-Type axes; the IsAccepted concept supplies Type itself.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 1: PureLinear (all-strict) baseline must accept.");

// Witness 2: PureCopy — Usage relaxation to `copy`.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>,
    gr::copy,  // <-- Usage
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 2: PureCopy stance — `copy` engages Usage.");

// Witness 3: IoFunction — Effect relaxation to `with<IO>`.
//
// fixy-CR-01 audit follow-up: Theory.h §30.14
// `classified_io_without_declassify` corpus entry rejects bindings
// that engage strict-default Security (= Classified) with an IO
// effect and no declassify.  IoFunction's canonical Security shape
// is therefore `as_public` (matches fixy::IoFunction stance in
// fixy/Fn.h).  The witness pins that the substrate-level
// `as_public + with<IO>` composition is the well-formed
// fixy/Fn alias of the IoFunction stance.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>, strict<D::Usage>,
    gr::with<crucible::effects::Effect::IO>,  // <-- Effect
    gr::as_public,                            // <-- Security
    strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 3: IoFunction stance — `with<IO>` engages Effect; "
    "Security is `as_public` to satisfy fixy-CR-01 corpus.");

// Witness 4: CtCrypto — Effect relaxation + Usage relaxation.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>,
    gr::copy,                                          // Usage
    gr::with<crucible::effects::Effect::Block>,         // Effect
    strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 4: CtCrypto stance — Usage + Effect both engaged.");

// Witness 5: SecretConsumer — same shape as PureLinear (default
// Security is Classified, so no relaxation is needed); pinning the
// fact that the accept-strict marker on Security IS the correct
// engagement when the binding consumes Classified data.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 5: SecretConsumer stance — explicit accept-strict on "
    "Security engages the Classified default.");

// Witness 6: Refinement (Tier F) relaxation via `refined_with`.
static_assert(fixy::IsAccepted<int,
    gr::refined_with<crucible::safety::fn::pred::True>,  // <-- Refinement
    strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 6: Tier-F Refinement relaxation via refined_with<Pred> "
    "engages the Refinement axis.");

// Witness 7: Lifetime (Tier S) relaxation via `in_region`.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    gr::in_region<42>,  // <-- Lifetime
    strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 7: Tier-S Lifetime relaxation via in_region<Tag>.");

// Witness 8: Version (Tier V) relaxation via `version<N>`.
static_assert(fixy::IsAccepted<int,
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>,
    gr::version<3>,    // <-- Version
    strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Witness 8: Tier-V Version relaxation via version<N>.");

// ─── Negative shape-witness — empty pack rejects ───────────────────
//
// Under wrapper-discipline IsAccepted, the empty Grants pack still
// engages Type (auto-injected) but leaves the other 18 axes
// unengaged — rejected by AllDimsEngaged.
static_assert(!fixy::IsAccepted<int>,
    "Empty Grants pack must reject — only Type engaged via injection.");

// ─── Negative shape-witness — Type=void rejects ────────────────────
static_assert(!fixy::IsAccepted<void,
    strict<D::Refinement>, strict<D::Usage>,
    strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
    strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
    strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>>,
    "Type=void must reject — Fn requires complete object type.");

int main() { return 0; }
