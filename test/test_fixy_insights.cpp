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

#include <crucible/fixy/Insights.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/diag/Insights.h>

namespace {

using ::crucible::safety::diag::has_insights_v;
using ::crucible::safety::diag::has_substantive_insights_v;

// ── 20 FixyNotEngaged_<Axis> tags ─────────────────────────────────

#define CRUCIBLE_CHECK_FIXY_INSIGHT(AxisName)                                  \
    static_assert(has_insights_v<                                              \
        ::crucible::fixy::diag::FixyNotEngaged_##AxisName>,                    \
        "fixy-H-14: FixyNotEngaged_" #AxisName                                 \
        " must have an insight_provider specialization");                      \
    static_assert(has_substantive_insights_v<                                  \
        ::crucible::fixy::diag::FixyNotEngaged_##AxisName>,                    \
        "fixy-H-14: FixyNotEngaged_" #AxisName                                 \
        " insights must meet QV thresholds (30/20/10/10 chars)")

CRUCIBLE_CHECK_FIXY_INSIGHT(Type);
CRUCIBLE_CHECK_FIXY_INSIGHT(Refinement);
CRUCIBLE_CHECK_FIXY_INSIGHT(Usage);
CRUCIBLE_CHECK_FIXY_INSIGHT(Effect);
CRUCIBLE_CHECK_FIXY_INSIGHT(Security);
CRUCIBLE_CHECK_FIXY_INSIGHT(Protocol);
CRUCIBLE_CHECK_FIXY_INSIGHT(Lifetime);
CRUCIBLE_CHECK_FIXY_INSIGHT(Provenance);
CRUCIBLE_CHECK_FIXY_INSIGHT(Trust);
CRUCIBLE_CHECK_FIXY_INSIGHT(Representation);
CRUCIBLE_CHECK_FIXY_INSIGHT(Observability);
CRUCIBLE_CHECK_FIXY_INSIGHT(Complexity);
CRUCIBLE_CHECK_FIXY_INSIGHT(Precision);
CRUCIBLE_CHECK_FIXY_INSIGHT(Space);
CRUCIBLE_CHECK_FIXY_INSIGHT(Overflow);
CRUCIBLE_CHECK_FIXY_INSIGHT(Mutation);
CRUCIBLE_CHECK_FIXY_INSIGHT(Reentrancy);
CRUCIBLE_CHECK_FIXY_INSIGHT(Size);
CRUCIBLE_CHECK_FIXY_INSIGHT(Version);
CRUCIBLE_CHECK_FIXY_INSIGHT(Staleness);

#undef CRUCIBLE_CHECK_FIXY_INSIGHT

// ── 4 §30.14 theory corpus entries ────────────────────────────────

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
