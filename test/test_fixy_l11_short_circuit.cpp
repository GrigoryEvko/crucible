// test_fixy_l11_short_circuit — positive verification of Theory.h's
// OR-fold + companion if-chain short-circuit semantics.
//
// fixy-L-11: Theory.h's `is_in_unsoundness_corpus` OR fold (and the
// three companion if-chains `corpus_cite_for_v`,
// `corpus_entry_name_for_v`, `corpus_full_diagnostic_v`) all CLAIM
// "short-circuit on first match".  The doc-comment at line 580 of
// Theory.h literally says:
//
//   A binding with `as_secret + with<IO, Bg>` hits BOTH entries;
//   the short-circuiting OR fold reports the first match.
//
// Pre-L-11 the claim had no positive test substantiating it — only
// implicit confidence from C++ `||` semantics.  This TU asserts at
// compile time:
//
//   PART 1: Each of the 4 declassify-axis corpus entries detects
//           ITS OWN pattern when fed in isolation (per-entry
//           individual positive proof — grounds Part 2).
//
//   PART 2: When a Grants pack hits MULTIPLE entries (the
//           documented `with<IO, Bg>` example), all four
//           diagnostic surfaces (IsInUnsoundnessCorpus_v,
//           corpus_cite_for_v, corpus_entry_name_for_v,
//           corpus_full_diagnostic_v) return the FIRST matching
//           entry's artifact — proving (a) short-circuit
//           semantics on the OR fold AND (b) chain-order
//           alignment across all four surfaces.
//
//   PART 3: Symmetric proof on the Internal-tier dual entries.
//
// Closes the SECOND clause of L-11's filing ("no per-entry
// diagnostic surfaces") as a side effect — by exercising
// `corpus_entry_name_for_v` and `corpus_full_diagnostic_v` this TU
// witnesses that per-entry diagnostic surfaces exist and are
// internally consistent.

#include <crucible/fixy/Theory.h>
#include <crucible/fixy/Grant.h>

#include <string_view>

namespace fxt = ::crucible::fixy::theory;
namespace gr  = ::crucible::fixy::grant;
namespace eff = ::crucible::effects;

namespace l11_short_circuit_proof {

// Type axis: any concrete type works — the 4 declassify-axis
// corpus entries match on the Grants pack shape, not on the Type
// axis.  `int` is the canonical neutral choice.
using TypeProbe = int;

// ═════════════════════════════════════════════════════════════════════
// PART 1 — per-entry individual positive proofs
// ═════════════════════════════════════════════════════════════════════
//
// Each entry's `matches<Type, Grants...>()` must fire on its own
// signature pattern when fed in isolation.  These tests ground the
// multi-match Part 2 — we know exactly which entries the inputs
// trigger.

// Entry 1: classified_io_without_declassify
//   Signature: is_secret_grant + is_io_effect_grant + !declassify
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_secret, gr::with_io>
              == std::string_view{"classified_io_without_declassify"},
    "L-11.E1: classified_io_without_declassify detects "
    "as_secret + with_io + no-declassify pattern in isolation.");

// Entry 2: classified_bg_without_declassify
//   Signature: is_secret_grant + is_bg_effect_grant + !declassify
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_secret, gr::with_bg>
              == std::string_view{"classified_bg_without_declassify"},
    "L-11.E2: classified_bg_without_declassify detects "
    "as_secret + with_bg + no-declassify pattern in isolation.");

// ── FIXY-FOUND-005 witness: declassify-only + IO admits cleanly ────
//
// A binding with `declassify<Policy> + with_io` (no as_secret /
// as_classified / strict<Security>) MUST NOT match Entry 1.  Pre-
// FOUND-005 the predicate self-cancelled: the same declassify
// satisfied has_secret (via fixy-A4-015 making declassify count as
// Security engagement) AND fired has_declassify, conjunction-collapsing
// to FALSE (admit) — same OUTCOME as the fix, but for the wrong
// STRUCTURAL reason (the predicate lied that classified data was
// present then blocked the matcher with !declassify).  After FOUND-005
// the matches() arm reads `is_secret_carrier` (which is FALSE for
// declassify-only), so has_secret_carrier=FALSE → predicate FALSE →
// admit, for the right STRUCTURAL reason (no classified data carrier).
// This witness locks the post-fix behavior: a future regression that
// (mis)wires has_secret back to is_secret_grant would still have the
// same OUTCOME (admit), but `corpus_entry_name_for_v` would be empty
// (no entry matched) — which we assert here.  If anyone reverts the
// FOUND-005 carrier discipline AND simultaneously breaks Entry 1's
// matcher in some other way that ends up MATCHING declassify-only,
// this static_assert reddens.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::declassify<::crucible::safety::secret_policy::AuditedLogging>,
                  gr::with_io>
              == std::string_view{},
    "FIXY-FOUND-005: declassify-only + with_io admits cleanly — no "
    "entry matches.  has_secret_carrier is FALSE (no as_secret/"
    "as_classified/strict<Security> grant), so Entry 1 does not "
    "self-cancel: it admits via the carrier-not-present path, NOT "
    "the declassify-cancels-itself path.");

// Entry 5: internal_io_without_declassify
//   Signature: is_internal_grant + is_io_effect_grant + !declassify
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_internal, gr::with_io>
              == std::string_view{"internal_io_without_declassify"},
    "L-11.E5: internal_io_without_declassify detects "
    "as_internal + with_io + no-declassify pattern in isolation.");

// Entry 6: internal_bg_without_declassify
//   Signature: is_internal_grant + is_bg_effect_grant + !declassify
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_internal, gr::with_bg>
              == std::string_view{"internal_bg_without_declassify"},
    "L-11.E6: internal_bg_without_declassify detects "
    "as_internal + with_bg + no-declassify pattern in isolation.");

// ═════════════════════════════════════════════════════════════════════
// PART 2 — multi-match short-circuit proofs (the canonical L-11 claim)
// ═════════════════════════════════════════════════════════════════════
//
// A SINGLE grant `with<IO, Bg>` triggers BOTH:
//   - is_io_effect_grant<with<IO, Bg>>::value = true  (IO is in pack)
//   - is_bg_effect_grant<with<IO, Bg>>::value = true  (Bg is in pack)
//
// Combined with `as_secret` and no-declassify, this hits BOTH:
//   - Entry 1 (classified_io_without_declassify)
//   - Entry 2 (classified_bg_without_declassify)
//
// The OR fold uses C++ `||` which short-circuits on the first true
// operand.  All three companion if-chains use the same declaration
// order, so they must return entry 1's artifacts.  If any chain
// returned entry 2's artifact, the chain order would be misaligned
// with the OR fold — the rejection diagnostic would name a corpus
// entry inconsistent with the entry that actually decided
// rejection.

using IoAndBg = gr::with<eff::Effect::IO, eff::Effect::Bg>;

// (M-A) is_in_unsoundness_corpus returns true — SOME entry matched.
static_assert(fxt::IsInUnsoundnessCorpus_v<TypeProbe,
                  gr::as_secret, IoAndBg>,
    "L-11.MA: multi-match Grants pack must be rejected by "
    "NotInTheoryCorpus (at least one corpus entry fires).");

// (M-B) corpus_entry_name_for_v returns the FIRST matching entry.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_secret, IoAndBg>
              == std::string_view{"classified_io_without_declassify"},
    "L-11.MB: short-circuit on first match — corpus_entry_name_for_v "
    "must return classified_io_without_declassify (entry 1), NOT "
    "classified_bg_without_declassify (entry 2 also matches).");

// (M-C) Negative form: corpus_entry_name_for_v does NOT return the
//       later-matching entry's name.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_secret, IoAndBg>
              != std::string_view{"classified_bg_without_declassify"},
    "L-11.MC: short-circuit witnessed in the negative — the chain "
    "does NOT return the later match's name.");

// (M-D) corpus_cite_for_v aligns with corpus_entry_name_for_v.  Probe
//       with a stable opening phrase from entry 1's cite() (which
//       starts with the Sabelfeld-Myers attribution post fixy-M-16).
static_assert(fxt::corpus_cite_for_v<TypeProbe,
                  gr::as_secret, IoAndBg>
              .starts_with("Sabelfeld-Myers 2003"),
    "L-11.MD: corpus_cite_for_v aligns with first-match entry — "
    "classified_io_without_declassify's cite opens with "
    "'Sabelfeld-Myers 2003' (post-fixy-M-16 attribution).  If "
    "alignment broke, the chain would return entry 2's cite (which "
    "opens 'Smith-Volpano 1998').");

// (M-E) corpus_cite_for_v does NOT contain entry 2's primary cite.
static_assert(fxt::corpus_cite_for_v<TypeProbe,
                  gr::as_secret, IoAndBg>
              .find("Smith-Volpano 1998") == std::string_view::npos,
    "L-11.ME: corpus_cite_for_v does NOT include entry 2's cite — "
    "short-circuit confirmed on the cite chain.");

// The full_diagnostic for any classified-tier corpus entry follows
// Theory.h's `full_diagnostic()` template:
//
//   "fixy::fn<Type, Grants...> [tier 5: NotInTheoryCorpus]: binding
//    matches §30.14 unsoundness corpus entry: <ENTRY-NAME>.  <CITE>"
//
// where <ENTRY-NAME> sits at a KNOWN, FIXED offset (the prefix
// before it is template-invariant).  This lets `starts_with`
// substitute for `find` on the structurally-most-informative
// probe — position 0, not "somewhere".  Using `starts_with` is
// also a libstdc++ consteval-budget workaround: `string_view::find`
// on the 411-char full_diagnostic haystack invokes
// `char_traits::find` through a non-constant pointer arithmetic
// chain that some libstdc++ 16 builds reject as not-a-constant-
// expression (the cite chain in M-D / M-E uses starts_with / a
// shorter haystack for the same reason).  Switching to
// `starts_with` against the known prefix makes the assertion
// CHEAPER at consteval AND STRONGER structurally (binds position,
// not just existence).
inline constexpr std::string_view kFullDiagnosticPrefix =
    "fixy::fn<Type, Grants...> [tier 5: NotInTheoryCorpus]: "
    "binding matches §30.14 unsoundness corpus entry: ";

// (M-F) corpus_full_diagnostic_v opens with the entry-1-naming
//       prefix (`<prefix>classified_io_without_declassify`).
static_assert(fxt::corpus_full_diagnostic_v<TypeProbe,
                  gr::as_secret, IoAndBg>
              .starts_with(std::string_view{
                  "fixy::fn<Type, Grants...> [tier 5: "
                  "NotInTheoryCorpus]: binding matches §30.14 "
                  "unsoundness corpus entry: "
                  "classified_io_without_declassify"}),
    "L-11.MF: corpus_full_diagnostic_v opens with the entry-1-"
    "naming prefix — full-diagnostic chain confirmed aligned with "
    "OR fold at position 0.");

// (M-G) corpus_full_diagnostic_v does NOT open with the entry-2-
//       naming prefix.  Combined with M-F's positive at position
//       0, this proves the chain returns entry 1's diagnostic and
//       NOT entry 2's at the documented prefix offset — the
//       diagnostic chain short-circuits at the same position as
//       the OR fold.
static_assert(!fxt::corpus_full_diagnostic_v<TypeProbe,
                  gr::as_secret, IoAndBg>
              .starts_with(std::string_view{
                  "fixy::fn<Type, Grants...> [tier 5: "
                  "NotInTheoryCorpus]: binding matches §30.14 "
                  "unsoundness corpus entry: "
                  "classified_bg_without_declassify"}),
    "L-11.MG: corpus_full_diagnostic_v does NOT open with the "
    "entry-2-naming prefix — full-diagnostic chain confirmed "
    "short-circuiting on first match.");

// ═════════════════════════════════════════════════════════════════════
// PART 3 — Internal-tier symmetric proof
// ═════════════════════════════════════════════════════════════════════
//
// `as_internal + with<IO, Bg>` hits BOTH entry 5 AND entry 6 by the
// same logic.  Per OR-fold declaration order, entry 5 fires first.
// Asserting on the internal-tier dual catches an unlikely class of
// regressions where the OR fold's order accidentally diverges from
// the companion chains' order at a position later than entries 1-2.

// (IT-A) Multi-match on internal-tier reports entry 5 (first).
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_internal, IoAndBg>
              == std::string_view{"internal_io_without_declassify"},
    "L-11.IT-A: internal-tier multi-match short-circuits to entry 5 "
    "(internal_io_without_declassify) per OR-fold order.");

// (IT-B) NOT entry 6.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe,
                  gr::as_internal, IoAndBg>
              != std::string_view{"internal_bg_without_declassify"},
    "L-11.IT-B: internal-tier short-circuit skips entry 6 — "
    "chain-order alignment holds at corpus position 5/6 (not just "
    "position 1/2).");

// ═════════════════════════════════════════════════════════════════════
// PART 4 — chain-order coherence invariant
// ═════════════════════════════════════════════════════════════════════
//
// Stronger: for every multi-match input, the name returned by
// corpus_entry_name_for_v MUST appear in the full_diagnostic_v
// string.  This is a coherence invariant — if it ever broke, the
// tier-5 rejection message would name one entry while the
// full-diagnostic text described a different entry.

namespace coherence_proof {
// `as_secret + with<IO, Bg>` — multi-match probe.  Witness: the
// name returned by `corpus_entry_name_for_v` MUST appear in the
// string returned by `corpus_full_diagnostic_v` at the position
// the documented prefix template fixes for the entry-name slot.
// Combined with the per-tier short-circuit proofs above, this
// closes the meta-claim that all four diagnostic surfaces stay
// aligned across the OR fold's first-match position.
//
// Implementation: kFull.substr(kPrefix.size()) skips past the
// template-invariant prefix and the resulting `string_view`'s
// `starts_with(kName)` checks the entry-name slot matches the
// short-form name returned by `corpus_entry_name_for_v`.  This is
// the substring-equivalent of `kFull.find(kName) != npos` BUT
// strictly stronger (pins the offset) AND consteval-cheaper —
// `find()` on the 411-char full_diagnostic haystack exhausts
// libstdc++ 16's `char_traits::find` constant-expression budget
// on some configurations (see M-F / M-G commentary above).
inline constexpr auto kName = fxt::corpus_entry_name_for_v<TypeProbe,
    gr::as_secret, IoAndBg>;
inline constexpr auto kFull = fxt::corpus_full_diagnostic_v<TypeProbe,
    gr::as_secret, IoAndBg>;

static_assert(kFull.starts_with(kFullDiagnosticPrefix),
    "L-11.COHERENCE-PREFIX: corpus_full_diagnostic_v must open "
    "with the documented Theory.h template prefix before the "
    "entry-name slot.");

static_assert(kFull.substr(kFullDiagnosticPrefix.size())
                   .starts_with(kName),
    "L-11.COHERENCE: corpus_full_diagnostic_v string MUST embed "
    "the entry name returned by corpus_entry_name_for_v at the "
    "template-fixed offset — diagnostic surfaces must be "
    "internally consistent at every multi-match input.");

// Note on the internal-tier dual: Part 3 (IT-A / IT-B) already
// asserts entry-5 first-match on the internal-tier multi-match.
// The full coherence invariant (entry-name slot in
// `corpus_full_diagnostic_v` matches the name in
// `corpus_entry_name_for_v`) is structurally identical between
// the classified-tier (proven here) and the internal-tier (would
// be a syntactic transliteration: `gr::as_internal` instead of
// `gr::as_secret`, expects "internal_io_without_declassify"
// instead of "classified_io_without_declassify").  The OR fold
// processes both tiers through the SAME chain, so the classified-
// tier coherence proof suffices for the internal tier under
// chain-position invariance (already proven by IT-A / IT-B).
}  // namespace coherence_proof

}  // namespace l11_short_circuit_proof

int main() { return 0; }
