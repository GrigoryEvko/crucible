// A grants pack can match more than one entry of the unsoundness corpus.
// When it does, the OR fold and its three companion chains must all
// report the same entry: the first one that matches.  Otherwise a
// rejection would name one entry while its diagnostic described another.
// Every assertion in this file is one half of that claim.

#include <crucible/fixy/Theory.h>
#include <crucible/fixy/Grant.h>

#include <string_view>

namespace fxt = ::crucible::fixy::theory;
namespace gr = ::crucible::fixy::grant;
namespace eff = ::crucible::effects;

namespace l11_short_circuit_proof {

// These corpus entries match on the shape of the grants pack and ignore
// the type axis, so any concrete type serves as the probe.
using TypeProbe = int;

// Each entry fires on its own pattern when fed in isolation.  That is
// what makes the multi-match assertions further down readable: the inputs
// there are known to trigger exactly two entries.

// Entry 1 matches a secret grant plus an IO effect with no declassify.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_secret, gr::with_io>
                  == std::string_view{"classified_io_without_declassify"},
              "classified_io_without_declassify detects "
              "as_secret + with_io + no-declassify pattern in isolation.");

// Entry 2 matches a secret grant plus a Bg effect with no declassify.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_secret, gr::with_bg>
                  == std::string_view{"classified_bg_without_declassify"},
              "classified_bg_without_declassify detects "
              "as_secret + with_bg + no-declassify pattern in isolation.");

// A declassify grant with an IO effect and no secret carrier is admitted,
// and the assertion is on the entry name rather than on admission,
// because the two paths to admission are not equally sound.  Entry 1 must
// stand down because no classified carrier is present.  A matcher that
// instead counted the declassify as the carrier and then cancelled itself
// against its own no-declassify clause would admit too, for a reason that
// is not true.  Only the empty name distinguishes them.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::declassify<::crucible::safety::secret_policy::AuditedLogging>,
                                           gr::with_io>
                  == std::string_view{},
              "declassify-only + with_io admits cleanly — no entry matches.  "
              "No as_secret, as_classified or strict<Security> grant is present, "
              "so there is no classified carrier and Entry 1 stands down on that "
              "ground rather than cancelling itself against its own "
              "no-declassify clause.");

// Entry 5 matches an internal grant plus an IO effect with no declassify.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_internal, gr::with_io>
                  == std::string_view{"internal_io_without_declassify"},
              "internal_io_without_declassify detects "
              "as_internal + with_io + no-declassify pattern in isolation.");

// Entry 6 matches an internal grant plus a Bg effect with no declassify.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_internal, gr::with_bg>
                  == std::string_view{"internal_bg_without_declassify"},
              "internal_bg_without_declassify detects "
              "as_internal + with_bg + no-declassify pattern in isolation.");

// One grant naming both IO and Bg satisfies the IO clause of entry 1 and
// the Bg clause of entry 2 at once.  Put with a secret grant and no
// declassify, it therefore matches both entries, and the fold stops at
// the first.  The three companion chains are written in the same
// declaration order and so must land on the same entry.
using IoAndBg = gr::with<eff::Effect::IO, eff::Effect::Bg>;

static_assert(fxt::IsInUnsoundnessCorpus_v<TypeProbe, gr::as_secret, IoAndBg>,
              "multi-match Grants pack must be rejected by "
              "NotInTheoryCorpus (at least one corpus entry fires).");

static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_secret, IoAndBg>
                  == std::string_view{"classified_io_without_declassify"},
              "short-circuit on first match — corpus_entry_name_for_v "
              "must return classified_io_without_declassify (entry 1), NOT "
              "classified_bg_without_declassify (entry 2 also matches).");

static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_secret, IoAndBg>
                  != std::string_view{"classified_bg_without_declassify"},
              "short-circuit witnessed in the negative — the chain "
              "does NOT return the later match's name.");

// The two entries open their citations with different attributions,
// which is what lets the opening phrase identify which one the chain
// returned.
static_assert(fxt::corpus_cite_for_v<TypeProbe, gr::as_secret, IoAndBg>.starts_with("Sabelfeld-Myers 2003"),
              "corpus_cite_for_v aligns with first-match entry — "
              "classified_io_without_declassify's cite opens with "
              "'Sabelfeld-Myers 2003'.  If alignment broke, the chain would "
              "return entry 2's cite, which opens 'Smith-Volpano 1998'.");

static_assert(fxt::corpus_cite_for_v<TypeProbe, gr::as_secret, IoAndBg>.find("Smith-Volpano 1998")
                  == std::string_view::npos,
              "corpus_cite_for_v does NOT include entry 2's cite — "
              "short-circuit confirmed on the cite chain.");

// The text before the entry name is the same for every corpus entry, so
// the name sits at a fixed offset.  Probing with `starts_with` against
// that prefix therefore pins the position rather than merely asking
// whether the name appears somewhere.  It is also the cheaper form:
// `find` over the whole diagnostic runs the standard library's character
// search through pointer arithmetic that some builds refuse to accept as
// a constant expression.
inline constexpr std::string_view kFullDiagnosticPrefix = "fixy::fn<Type, Grants...> [tier 5: NotInTheoryCorpus]: "
                                                          "binding matches known-unsoundness corpus entry: ";

static_assert(fxt::corpus_full_diagnostic_v<TypeProbe, gr::as_secret, IoAndBg>.starts_with(std::string_view{
                  "fixy::fn<Type, Grants...> [tier 5: "
                  "NotInTheoryCorpus]: binding matches known-unsoundness "
                  "corpus entry: "
                  "classified_io_without_declassify"}),
              "corpus_full_diagnostic_v opens with the entry-1-naming prefix — "
              "full-diagnostic chain confirmed aligned with the OR fold at "
              "position 0.");

static_assert(!fxt::corpus_full_diagnostic_v<TypeProbe, gr::as_secret, IoAndBg>.starts_with(std::string_view{
                  "fixy::fn<Type, Grants...> [tier 5: "
                  "NotInTheoryCorpus]: binding matches known-unsoundness "
                  "corpus entry: "
                  "classified_bg_without_declassify"}),
              "corpus_full_diagnostic_v does NOT open with the entry-2-naming "
              "prefix — full-diagnostic chain confirmed short-circuiting on "
              "first match.");

// The internal-tier pair matches twice for the same reason and resolves
// to entry 5.  Repeating the proof further along the chain catches an
// order divergence that only appears past the first two positions.
static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_internal, IoAndBg>
                  == std::string_view{"internal_io_without_declassify"},
              "internal-tier multi-match short-circuits to entry 5 "
              "(internal_io_without_declassify) per OR-fold order.");

static_assert(fxt::corpus_entry_name_for_v<TypeProbe, gr::as_internal, IoAndBg>
                  != std::string_view{"internal_bg_without_declassify"},
              "internal-tier short-circuit skips entry 6 — chain-order "
              "alignment holds at corpus position 5 and 6, not only at 1 and 2.");

// The strongest form of the claim: for any multi-match input the name one
// surface reports must be the name embedded in the other's text.  The
// check reads the name slot directly by stepping past the invariant
// prefix, which pins the offset instead of searching the whole string.
namespace coherence_proof {
inline constexpr auto kName = fxt::corpus_entry_name_for_v<TypeProbe, gr::as_secret, IoAndBg>;
inline constexpr auto kFull = fxt::corpus_full_diagnostic_v<TypeProbe, gr::as_secret, IoAndBg>;

static_assert(kFull.starts_with(kFullDiagnosticPrefix),
              "corpus_full_diagnostic_v must open with the documented template "
              "prefix before the entry-name slot.");

static_assert(kFull.substr(kFullDiagnosticPrefix.size()).starts_with(kName),
              "corpus_full_diagnostic_v string MUST embed the entry name "
              "returned by corpus_entry_name_for_v at the template-fixed offset "
              "— diagnostic surfaces must be internally consistent at every "
              "multi-match input.");

// There is deliberately no internal-tier copy of the coherence check.
// Both tiers run through one chain, and the assertions above already pin
// the internal tier's first-match position, so the classified-tier proof
// carries over unchanged.
}  // namespace coherence_proof

}  // namespace l11_short_circuit_proof

int main() { return 0; }
