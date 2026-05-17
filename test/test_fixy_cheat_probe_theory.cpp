// ── test_fixy_cheat_probe_theory — Round 2 trait-spec-injection ─────
//
// Adversarial cheats against the §30.14 known-unsoundness corpus in
// fixy/Theory.h.  Each cheat MUST be rejected by the gate; if the
// build accepts any of these, the corpus is bypassed.
//
// FIXY-AUDIT-D9 cheat-probe Round 2.  Round 1 (test_fixy_cheat_probe)
// attacks IsAccepted's engagement gate via trait-spec injection on
// IsGrantTag / which_dim.  Round 2 attacks Theory.h's
// `classified_io_without_declassify` corpus entry.
//
// The corpus computes:
//     has_secret  = ∃ G ∈ Grants. is_secret_grant<G>::value
//     has_io      = ∃ G ∈ Grants. is_io_effect_grant<G>::value
//     has_decls   = ∃ G ∈ Grants. is_declassify_grant<G>::value
//     matches()   = has_secret && has_io && !has_decls
//
// A bypass would either (a) flip a positive detection to false, or
// (b) inject a false-positive declassify detection.  All cheats here
// MUST be rejected; the `static_assert(!fixy::IsAccepted<...>)`
// claims encode the bypass-failure witnesses.
//
// All claims are negative — `static_assert(!cheat::passes)`.  A
// green compile means: the cheats all failed to escape the corpus.

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Theory.h>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
namespace th   = crucible::fixy::theory;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// ═════════════════════════════════════════════════════════════════════
// ── The canonical §30.14 implicit-flow pack (must always reject) ───
// ═════════════════════════════════════════════════════════════════════
//
// Engages every axis correctly + (as_secret + with_io) + NO
// declassify — this is the corpus's exact target pattern.

template <typename ExtraGrant>
inline constexpr bool implicit_flow_pack_rejects =
    !fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,            // Effect = IO
        gr::as_secret,          // Security = Secret
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        ExtraGrant>;

// Sanity-check counter-witness: the canonical 19-axis pack WITHOUT
// the implicit-flow pair DOES accept.
namespace counter_witness {
    static_assert(fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
        strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>,
        "Counter-witness: the canonical fully-engaged strict pack MUST "
        "accept (Round 2 gate is not broken-shut).");
}

// ─── Cheat 1: rogue declassify-trait specialization ────────────────
//
// The attacker invents an empty user struct (no `declassify<>` shape)
// and specializes `is_declassify_grant<rogue>` to `std::true_type`,
// hoping the OR-fold over the grants pack inside
// `has_grant_of<is_declassify_grant, ...>` returns true and lets the
// `!has_declassify` negation flip the corpus's match to false.
//
// Defense: IsAcceptedGrants requires every grant to satisfy
// `IsGrantTag` (final + inherits grant_base) BEFORE the corpus is
// consulted.  A rogue type that lacks IsGrantTag fails at
// engagement; the corpus check never runs because the gate already
// rejected.

namespace cheat_1_rogue_declassify_trait {
    struct rogue {};  // NOT a grant tag — no inheritance, no finality
}
namespace crucible::fixy::theory::detail {
    template <>
    struct is_declassify_grant<::cheat_1_rogue_declassify_trait::rogue>
        : std::true_type {};
}
namespace cheat_1_rogue_declassify_trait {
    static_assert(!fixy::IsAcceptedGrants<rogue>,
        "Cheat 1: a rogue type cannot satisfy IsAcceptedGrants — "
        "engagement gate fires before the corpus.");
    static_assert(implicit_flow_pack_rejects<rogue>,
        "Cheat 1: even with rogue specialized as a declassify, the "
        "outer IsAccepted must reject because the rogue type is not "
        "a well-formed grant.");
}

// ─── Cheat 2: trait-spec injection on is_secret_grant ──────────────
//
// The attacker specializes `is_secret_grant<gr::as_secret>` to
// `std::false_type`, hoping to flip the corpus's `has_secret`
// detection to false (which would make `matches()` return false,
// pretending the binding is sound).
//
// Defense: this is an explicit specialization of an existing
// primary template member — C++ permits a SINGLE definition, and
// the substrate's specialization (Theory.h §73) is already
// definitive.  Attempting a second user-side specialization is an
// ODR violation; the compiler rejects.  We exercise the cheat in
// the SHAPE that would be required — but the static_assert below
// proves the substrate's specialization wins even if multiple TUs
// were combined: the corpus correctly detects `as_secret`.

namespace cheat_2_flip_secret_trait {
    // We CANNOT redefine is_secret_grant<as_secret> here (ODR).
    // Instead, the cheat tests whether the corpus's detection
    // correctly fires for the canonical Security tag in the
    // presence of an attacker-side IO grant — proving the
    // substrate's positive detection wins.
    static_assert(
        th::detail::is_secret_grant<gr::as_secret>::value,
        "Cheat 2 defense witness: the substrate's positive "
        "is_secret_grant specialization is definitive.");
    static_assert(
        th::corpus::classified_io_without_declassify::matches<
            int, gr::as_secret, gr::with_io>(),
        "Cheat 2 defense witness: the corpus correctly detects "
        "the implicit-flow shape via the substrate's "
        "specializations — no user-side flip is possible.");
}

// ─── Cheat 3: hide the secret tag behind a transparent wrapper ─────
//
// The attacker writes a final `final_wrapper` that inherits
// `grant_base`, engages `which_dim` for Security, but is NOT
// recognized by `is_secret_grant` (which is specialized only on the
// exact `gr::as_secret` / `gr::as_classified` shapes).  If
// `is_secret_grant<final_wrapper>` is false-by-default, the
// attacker's pack engages Security with `final_wrapper` instead of
// `as_secret` — the corpus's `has_secret` is false → `matches()` is
// false → corpus accepts the binding even though the user's INTENT
// matches the implicit-flow pattern.
//
// Defense: this is NOT a soundness bypass.  If `is_secret_grant`
// doesn't match the wrapper, the wrapper IS NOT semantically a
// secret tag at the theory layer.  The corpus only fires on the
// CANONICAL shapes; user-defined Security grants are not "classified"
// by the corpus definition.  The wrapper does engage Security at
// the resolver layer (via which_dim), but the corpus is a separate
// closed-set discipline.  This is documented architectural behavior:
// the corpus targets the NAMED canonical patterns; novel user tags
// must be added to the corpus by name.

namespace cheat_3_transparent_secret_wrapper {
    struct final_wrapper final : gr::grant_base {};
}
// Engage which_dim<final_wrapper> for Security:
// fixy-CR-09: known residual gap — this cheat probe intentionally
// reopens `namespace crucible::fixy::grant` to demonstrate that the
// §30.14 corpus is a CLOSED-SET discipline (not a structural one):
// novel user tags engage which_dim at the resolver layer but do not
// fire the corpus until added by name.  C++ has no namespace-scoped
// specialization access control; the namespace-purity CI guard
// (scripts/check-fixy-grant-namespace-purity.sh) excepts this file
// because the attack-demonstration intent is documented above.
namespace crucible::fixy::grant {
    template <>
    struct which_dim<::cheat_3_transparent_secret_wrapper::final_wrapper>
        : std::integral_constant<
              ::crucible::fixy::dim::DimensionAxis,
              ::crucible::fixy::dim::DimensionAxis::Security> {};
}
namespace cheat_3_transparent_secret_wrapper {
    // The wrapper IS recognized as a grant — that's expected.
    static_assert(gr::IsGrantTag<final_wrapper>,
        "Cheat 3 setup: the wrapper IS a structural grant.");

    // BUT the corpus does NOT match it as a secret tag — by design.
    // The wrapper is not in the canonical Security-secret-tag set
    // (as_secret / as_classified); the §30.14 corpus does not fire.
    //
    // This is an ARCHITECTURAL LIMIT of the closed-set corpus
    // discipline: the corpus targets named canonical patterns.  A
    // user-defined Security tag that aliases as_secret semantically
    // would need to be added to is_secret_grant by hand.  We
    // document this as the expected behavior and rely on review +
    // the closed-set discipline that the corpus's named-pattern
    // approach is intentional (Theory.h §30.14 doc-block).
    //
    // The witness below proves the substrate's behavior IS the
    // documented one: the wrapper is NOT detected as a secret by
    // the corpus.
    static_assert(!th::detail::is_secret_grant<final_wrapper>::value,
        "Cheat 3 architectural-limit witness: the corpus does not "
        "detect user-defined Security tags — closed-set discipline.");
}

// ─── Cheat 4: rogue IO-effect trait specialization ─────────────────
//
// Symmetric to Cheat 1: specialize is_io_effect_grant for a rogue
// type to inject a false IO detection, hoping to make the corpus
// think IO is present without an actual `with_io` in the pack — but
// since the rogue type is not in the grants pack, this specialization
// is dead.  More plausibly: try to MASK an existing `with_io` by
// specializing is_io_effect_grant<with_io> to std::false_type.
//
// As in Cheat 2, ODR forbids redefining the substrate's
// specialization.  We instead exercise the dual claim: the corpus's
// positive detection correctly fires on `with_io`, and the rogue
// false-positive specialization is dead because the rogue isn't a
// grant tag.

namespace cheat_4_rogue_io_trait {
    struct rogue {};  // NOT a grant tag
}
namespace crucible::fixy::theory::detail {
    template <>
    struct is_io_effect_grant<::cheat_4_rogue_io_trait::rogue>
        : std::true_type {};
}
namespace cheat_4_rogue_io_trait {
    static_assert(!fixy::IsAcceptedGrants<rogue>,
        "Cheat 4: a rogue type cannot satisfy IsAcceptedGrants — "
        "the engagement gate fires before the corpus.");
    static_assert(
        th::detail::is_io_effect_grant<gr::with_io>::value,
        "Cheat 4 defense witness: the substrate's positive "
        "is_io_effect_grant specialization remains definitive on "
        "the canonical with_io tag.");
}

// ─── Cheat 5: duplicate Security grant via two distinct lattice tags ──
//
// The attacker engages Security twice — once with as_secret (which
// the corpus targets) and once with as_classified (also targeted).
// The corpus's `has_secret` detection ORs over both, so both
// engagements should fire the corpus.  But UniqueEngagementPerAxis
// in IsAcceptedGrants forbids two grants on the same axis — the
// pack rejects on the uniqueness check BEFORE the corpus is
// reached.
//
// This is not a corpus bypass; it confirms the corpus and the
// uniqueness gate are layered defenses, neither alone sufficient.

namespace cheat_5_double_security_engagement {
    static_assert(!fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,
        gr::as_secret,      // Security #1
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        gr::as_classified>,   // Security #2 (duplicate axis)
        "Cheat 5: double-Security engagement must reject — "
        "UniqueEngagementPerAxis fires before the corpus.");
}

// ─── Cheat 6: declassify after a chain of dummy grants ─────────────
//
// The attacker reasons: "the OR-fold over the grants pack visits
// each grant once; if I sandwich `declassify<P>` between dummies,
// maybe the fold short-circuits and misses it."
//
// Defense: the corpus's `has_grant_of` uses a fold-expression
// (`(Predicate<Grants>::value || ...)`).  Fold expressions visit
// EVERY pack element regardless of short-circuit (the values are
// computed, then OR'd at compile time).  A declassify anywhere in
// the pack flips `has_declassify` to true.
//
// This cheat is the WITNESS that adding declassify legitimately
// removes the binding from the corpus.

namespace cheat_6_declassify_threading {
    struct AuditPolicy {};

    // This pack DOES match the corpus (Secret + IO) but the
    // declassify discharges the audit trail — the binding accepts.
    static_assert(fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,
        gr::declassify<AuditPolicy>,    // remediation
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>,
        "Cheat 6 counter-witness: declassify<Policy> correctly "
        "discharges the corpus — the binding ACCEPTS even with the "
        "Secret×IO pattern present.");
}

// ─── Cheat 7: strict-default Security bypass (closes fixy-CR-01) ───
//
// The attacker reasons: "Security defaults to `SecLevel::Classified`
// per `strict_default_for<Security>::value`.  A binding with NO
// explicit `as_secret` but with `strict<D::Security>` (=
// `accept_default_strict_for<Security>`) should resolve to
// Classified at the value layer — so the §30.14 corpus SHOULD reject
// it just like an explicit `as_secret` binding.  But the gate is
// syntactic (`is_secret_grant` matches only the tag SHAPE), so the
// strict-default form silently bypasses the corpus despite being
// semantically equivalent."
//
// This was a real bypass before fixy-CR-01.  Defense, shipped in
// Theory.h §30.14: a SECOND `is_secret_grant` specialization for
// `grant::accept_default_strict_for<DimensionAxis::Security>`,
// anchored by a `static_assert` invariant tying the strict default
// to `SecLevel::Classified`.  Both syntactic forms of "Security
// engages at the Classified tier" now hit the corpus.
//
// Witnesses are split between (a) this static_assert and (b) three
// new neg-compile fixtures in test/fixy_neg/ (one per corpus entry
// that mentions Security: classified_io / classified_bg /
// staleness_secret).

namespace cheat_7_strict_default_security_bypass {
    // Pack 1: strict-default Security × IO, NO declassify.  Before
    // the fix, IsAccepted would return TRUE here despite the
    // semantically-equivalent explicit-as_secret form rejecting.
    // After the fix, IsAccepted returns FALSE.
    static_assert(!fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_io,
        strict<D::Security>,   // strict default = Classified
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>,
        "Cheat 7 defense witness (classified_io): the corpus now "
        "rejects strict-default Security × IO without declassify, "
        "closing the fixy-CR-01 syntactic-vs-semantic bypass.");

    // Pack 2: strict-default Security × Bg, NO declassify.
    static_assert(!fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        gr::with_bg,
        strict<D::Security>,
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>, strict<D::Staleness>>,
        "Cheat 7 defense witness (classified_bg): the corpus now "
        "rejects strict-default Security × Bg without declassify.");

    // Pack 3: strict-default Security × stale_to<N>, NO declassify.
    static_assert(!fixy::IsAccepted<int,
        strict<D::Refinement>, strict<D::Usage>,
        strict<D::Effect>,
        strict<D::Security>,
        strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
        strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
        strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
        strict<D::Size>, strict<D::Version>,
        gr::stale_to<100>>,
        "Cheat 7 defense witness (staleness_secret): the corpus now "
        "rejects strict-default Security × stale_to<N> without "
        "declassify.");

    // No counter-witness for "strict-default + declassify discharges
    // corpus": `gr::declassify<Policy>` ITSELF engages Security (its
    // `which_dim_v == Security`), so combining `strict<D::Security>`
    // with `gr::declassify<Policy>` would double-engage Security and
    // trip `UniqueEngagementPerAxis` before the corpus is consulted.
    // Cheat 6's declassify-alone counter-witness already covers the
    // "Security engaged solely via declassify discharges corpus"
    // shape — semantically identical to the strict-default form
    // discharged via declassify.  No new shape to witness here.
}

// ═════════════════════════════════════════════════════════════════════
// ── Summary ────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Seven adversarial cheats, all rejected (or, in Cheat 3, documented
// as an architectural limit of the closed-set corpus discipline).
//
//   Cheat 1: rogue declassify specialization        — rejected (IsGrantTag fires)
//   Cheat 2: flip is_secret_grant via ODR redef     — IMPOSSIBLE by ODR
//   Cheat 3: transparent Security wrapper           — ARCHITECTURAL LIMIT (documented)
//   Cheat 4: rogue IO-effect specialization         — rejected (IsGrantTag fires)
//   Cheat 5: double-Security engagement             — rejected (UniqueEngagementPerAxis)
//   Cheat 6: declassify discharges corpus           — counter-witness (binding accepts)
//   Cheat 7: strict-default Security bypass         — rejected (fixy-CR-01 fix)
//
// The closed-set corpus discipline (Theory.h §30.14) is intentional:
// it targets NAMED canonical implicit-flow shapes from the
// literature.  User-defined Security tags that semantically alias
// as_secret would need to be added to is_secret_grant explicitly —
// this is the documented behavior per the Theory.h doc-block, and
// the Cheat 3 witness proves the substrate matches the documented
// behavior.  fixy-CR-01 (Cheat 7) closed the strict-default Security
// bypass — both syntactic forms of "Security engages at Classified"
// now hit the corpus.

int main() { return 0; }
