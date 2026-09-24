// has_insights_v holds when any prose field is non-empty. The substantive
// form holds when every field clears its length minimum. Each provider
// embeds its own length checks, so this file pins the same property from
// the outside: weakened prose reddens here as well as at the definition.

#include <crucible/fixy/_Insights.h>
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Theory.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/diag/_Insights.h>

#include <meta>

namespace {

using ::crucible::safety::diag::has_insights_v;
using ::crucible::safety::diag::has_substantive_insights_v;

// The reflection operator takes an entity, not a name introduced by a
// using-declaration, so the enum is named fully qualified at the splice.

consteval bool every_axis_has_substantive_insights_() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^::crucible::safety::DimensionAxis));
    bool result = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr ::crucible::safety::DimensionAxis axis = [:en:];
        using Tag = typename ::crucible::fixy::diag::tag_for_axis<axis>::type;
        result = result && has_insights_v<Tag>;
        result = result && has_substantive_insights_v<Tag>;
    }
#pragma GCC diagnostic pop
    return result;
}

static_assert(every_axis_has_substantive_insights_(),
              "every DimensionAxis enumerator must have a FixyNotEngaged_<Axis> "
              "insight_provider specialization whose four prose fields all meet "
              "the length thresholds (30/20/10/10 chars).");

// The fold above extends to a new enumerator on its own. This exact pin
// is what forces a maintainer to notice, so that an axis cannot ship
// before its tag and its insight prose do.
static_assert(::crucible::safety::DIMENSION_AXIS_COUNT == 33,
              "DimensionAxis grew beyond 33 enumerators. Ship the "
              "FixyNotEngaged_<NewAxis> tag and its insight prose, then bump this "
              "assertion.");

// Corpus entries classify by predicate shape rather than by axis, so the
// fold above does not reach them. A new corpus entry needs a new line here.

#define CRUCIBLE_CHECK_CORPUS_INSIGHT(EntryName)                                           \
    static_assert(has_insights_v<::crucible::fixy::theory::corpus::EntryName>,             \
                  "corpus::" #EntryName " must have an insight_provider specialization");  \
    static_assert(has_substantive_insights_v<::crucible::fixy::theory::corpus::EntryName>, \
                  "corpus::" #EntryName " insights must meet the length thresholds (30/20/10/10 chars)")

CRUCIBLE_CHECK_CORPUS_INSIGHT(classified_io_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(classified_bg_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(staleness_secret_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(ghost_runtime_observable);
CRUCIBLE_CHECK_CORPUS_INSIGHT(internal_io_without_declassify);
CRUCIBLE_CHECK_CORPUS_INSIGHT(internal_bg_without_declassify);

#undef CRUCIBLE_CHECK_CORPUS_INSIGHT

static_assert(::crucible::safety::diag::insight_provider<
                  ::crucible::fixy::theory::corpus::classified_io_without_declassify>::severity
                  == ::crucible::safety::diag::Severity::Fatal,
              "corpus entries ship Severity::Fatal. An information-flow leak is not "
              "downgrade-eligible.");

static_assert(::crucible::safety::diag::insight_provider<::crucible::fixy::diag::FixyNotEngaged_Type>::severity
                  == ::crucible::safety::diag::Severity::Error,
              "FixyNotEngaged_<Axis> tags ship Severity::Error — engagement gaps are "
              "build-breakers but not security-fatal.");

}  // namespace

int main() { return 0; }
