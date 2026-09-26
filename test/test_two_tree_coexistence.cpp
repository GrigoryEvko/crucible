// Both substrate trees in one translation unit.
//
// While the frozen tree and the new tree both exist, a consumer that
// moves to the new tree one header at a time can leave a translation
// unit that sees a header of each.  The build generates
// every_substrate_header.h from every header of both trees, so a macro or
// a declaration of one tree that collides with the other stops this
// build, and no list here has to name the headers.
//
// The four macro pairs below once shared a name and differed in body.
// Each pair registers into its own relation, so the body writes through
// the frozen spelling and through the new spelling in this one
// translation unit, and reads each registration back from its own tree.

#include <every_substrate_header.h>

#include <string_view>
#include <type_traits>

namespace two_tree {

struct frozen_tag : ::crucible::safety::diag::tag_base {
    static constexpr std::string_view name = "TwoTreeFrozenTag";
    static constexpr std::string_view description = "fixture registered through the frozen insights macro";
    static constexpr std::string_view remediation = "n/a — fixture";
};

struct new_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "TwoTreeNewTag";
    static constexpr std::string_view description = "fixture registered through the new insights macro";
    static constexpr std::string_view remediation = "n/a — fixture";
};

struct frozen_from {};
struct frozen_to {};
struct new_from {};
struct new_to {};

inline void sample_fn() noexcept {}

}  // namespace two_tree

CRUCIBLE_DEFINE_INSIGHTS(::two_tree::frozen_tag, ::crucible::safety::diag::Severity::Warning, "WHY-FROZEN",
                         "SYMPTOM-FROZEN", "CORRECT-FROZEN", "VIOLATING-FROZEN");

CRUCIBLE_DIAG_INSIGHTS(::two_tree::new_tag, ::foundation::diag::Severity::Fatal, "WHY-NEW", "SYMPTOM-NEW",
                       "CORRECT-NEW", "VIOLATING-NEW");

CRUCIBLE_ALLOW_MACHINE_TRANSITION(::two_tree::frozen_from, ::two_tree::frozen_to)
CRUCIBLE_ADMIT_MACHINE_TRANSITION(::two_tree::new_from, ::two_tree::new_to)

namespace two_tree {

static_assert(::crucible::safety::diag::insight_provider<frozen_tag>::severity
              == ::crucible::safety::diag::Severity::Warning);
static_assert(::crucible::safety::diag::insight_provider<frozen_tag>::why_this_matters == "WHY-FROZEN");
static_assert(::foundation::diag::insight_provider<new_tag>::severity == ::foundation::diag::Severity::Fatal);
static_assert(::foundation::diag::insight_provider<new_tag>::why_this_matters == "WHY-NEW");

static_assert(::crucible::safety::machine_transition_v<frozen_from, frozen_to>);
static_assert(!::crucible::safety::machine_transition_v<frozen_to, frozen_from>);
static_assert(::fixy::machine_transition_v<new_from, new_to>);
static_assert(!::fixy::machine_transition_v<new_to, new_from>);

// Each tree's edge lands in its own relation and nowhere else.
static_assert(!::fixy::machine_transition_v<frozen_from, frozen_to>);
static_assert(!::crucible::safety::machine_transition_v<new_from, new_to>);

inline void row_mismatch_asserts_expand() noexcept {
    CRUCIBLE_ROW_MISMATCH_ASSERT(true, EffectRowMismatch, &::two_tree::sample_fn, int, float, double);
    CRUCIBLE_DIAG_ROW_MISMATCH_ASSERT(true, EffectRowMismatch, &::two_tree::sample_fn, int, float, double);
}

}  // namespace two_tree

int main() {
    ::two_tree::row_mismatch_asserts_expand();
    return 0;
}
