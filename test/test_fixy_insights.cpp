// fixy-H-14 sentinel TU: every FixyNotEngaged_<Axis> + every §30.14
// corpus entry ships a substantive insight_provider specialization.
//
// `has_insights_v<Tag>`             — any prose field non-empty.
// `has_substantive_insights_v<Tag>` — every prose field meets QV
//                                      minimums (30/20/10/10 chars).
//
// fixy/Insights.h uses CRUCIBLE_DEFINE_INSIGHTS_QV which embeds the
// length-check static_asserts in each specialization; this sentinel
// TU witnesses that has_substantive_insights_v is true for every Tag
// from the outside (so a future maintainer who weakens the prose
// triggers BOTH the embedded asserts AND this sentinel).
//
// FIXY-FOUND-135 Batch 3 — Pattern B reflection fold replaces the
// previously hand-listed 20 axes (Type..Staleness) with a single
// `template for` over `enumerators_of(^^DimensionAxis)`, picking up
// the 13 axes added after the original sentinel landed
// (Synchronization, Regime, FpMode, SyscallSurface, ControlFlow,
// CallShape, StackUse, GlobalState, Stdio, HwInstruction,
// BarrierStrength, SimdIsa, MemoryScope).  The DIMENSION_AXIS_COUNT
// cardinality pin below is the real forward-compat trap — appending
// a new enumerator reddens loudly so future axes can't ship without
// shipping insight prose first.

#include <crucible/fixy/Insights.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/Insights.h>

#include <meta>

namespace {

using ::crucible::safety::diag::has_insights_v;
using ::crucible::safety::diag::has_substantive_insights_v;

// ── 33 FixyNotEngaged_<Axis> tags via Pattern B reflection fold ──
//
// One `static_assert` per `(axis, predicate)` pair; the fold
// instantiates 2*N static_asserts at consteval, replacing the
// hand-maintained CRUCIBLE_CHECK_FIXY_INSIGHT(...) lines that
// shipped with the original 20-axis sentinel.
//
// Reflection-of-using-declaration is rejected by GCC 16 (the ^^
// operator wants an entity, not a name brought-in via using-decl),
// so we name DimensionAxis fully-qualified at the splice site.

consteval bool every_axis_has_substantive_insights_() noexcept {
    static constexpr auto enumerators = std::define_static_array(
        std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
    bool result = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr ::crucible::safety::DimensionAxis axis = [:en:];
        using Tag =
            typename ::crucible::fixy::diag::tag_for_axis<axis>::type;
        result = result && has_insights_v<Tag>;
        result = result && has_substantive_insights_v<Tag>;
    }
#pragma GCC diagnostic pop
    return result;
}

static_assert(every_axis_has_substantive_insights_(),
    "fixy-H-14 (FIXY-FOUND-135): every DimensionAxis enumerator must "
    "have a FixyNotEngaged_<Axis> insight_provider specialization with "
    "all four prose fields meeting QV thresholds (30/20/10/10 chars).  "
    "The reflection fold above instantiates has_insights_v + "
    "has_substantive_insights_v for every axis via tag_for_axis<axis>.");

// Cardinality pin — the real forward-compat trap.  Appending a new
// enumerator to DimensionAxis reddens this assertion; the fix is to
// (a) ship a tag_for_axis<NewAxis> specialization in fixy/Reject.h,
// (b) ship a CRUCIBLE_DEFINE_INSIGHTS_QV block for
// FixyNotEngaged_NewAxis in fixy/Insights.h, and (c) bump the count
// here.  Auto-extends through the reflection fold without code change
// in this TU.
static_assert(::crucible::safety::DIMENSION_AXIS_COUNT == 33,
    "FIXY-FOUND-135 cardinality pin: DimensionAxis grew beyond 33 "
    "enumerators.  Ship the FixyNotEngaged_<NewAxis> tag + insight "
    "prose, then bump this assertion.  The reflection fold above "
    "already covers the new axis via tag_for_axis<axis>.");

// ── 6 §30.14 theory corpus entries ────────────────────────────────
//
// Corpus entries are NOT enum-driven (they classify by predicate
// shape, not by DimensionAxis), so they stay hand-listed.  The fold
// above does NOT cover them; adding a new corpus entry requires a
// new line below.

#define CRUCIBLE_CHECK_CORPUS_INSIGHT(EntryName)                               \
    static_assert(has_insights_v<                                              \
        ::crucible::fixy::theory::corpus::EntryName>,                          \
        "fixy-H-14: corpus::" #EntryName                                       \
        " must have an insight_provider specialization");                      \
    static_assert(has_substantive_insights_v<                                  \
        ::crucible::fixy::theory::corpus::EntryName>,                          \
        "fixy-H-14: corpus::" #EntryName                                       \
        " insights must meet QV thresholds (30/20/10/10 chars)")

CRUCIBLE_CHECK_CORPUS_INSIGHT(classified_io_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(classified_bg_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(staleness_secret_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(ghost_runtime_observable);
CRUCIBLE_CHECK_CORPUS_INSIGHT(internal_io_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(internal_bg_without_declassify);

#undef CRUCIBLE_CHECK_CORPUS_INSIGHT

// ── Severity sanity — corpus entries are Fatal, axis tags are Error.
// (Severity::Fatal is reserved for security-critical flows; §30.14
// IFC corpus matches that bar.)

static_assert(
    ::crucible::safety::diag::insight_provider<
        ::crucible::fixy::theory::corpus::classified_io_without_declassify>
        ::severity == ::crucible::safety::diag::Severity::Fatal,
    "§30.14 corpus entries ship Severity::Fatal — IFC leaks are not "
    "downgrade-eligible.");

static_assert(
    ::crucible::safety::diag::insight_provider<
        ::crucible::fixy::diag::FixyNotEngaged_Type>
        ::severity == ::crucible::safety::diag::Severity::Error,
    "FixyNotEngaged_<Axis> tags ship Severity::Error — engagement gaps are "
    "build-breakers but not security-fatal.");

}  // namespace

int main() { return 0; }
