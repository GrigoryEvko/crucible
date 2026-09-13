// Sentinel TU: compiles the alias header under the project warning flags so its
// in-namespace sentinels run.

#include <crucible/fixy/Rules.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fr = ::crucible::fixy::rule;
namespace sfn = ::crucible::safety::fn;
namespace scoll = ::crucible::safety::fn::collision;

using TestProbe = sfn::Fn<int>;

static_assert(std::is_same_v<fr::RuleCode, scoll::RuleCode>, "fixy::rule::RuleCode must alias substrate enum");

static_assert(fr::RuleCode::I002 == scoll::RuleCode::I002);
static_assert(fr::RuleCode::F002 == scoll::RuleCode::F002);
static_assert(fr::RuleCode::None == scoll::RuleCode::None);

static_assert(fr::rule_code_of_v<scoll::I002_ClassifiedFailPayload> == fr::RuleCode::I002);
static_assert(fr::rule_code_of_v<scoll::F002_FederationPeerTerminatingBudget> == fr::RuleCode::F002);

static_assert(std::is_same_v<fr::rule_tag_t<fr::RuleCode::I002>, scoll::I002_ClassifiedFailPayload>,
              "fixy::rule::rule_tag_t must resolve to substrate tag");

static_assert(fr::rule_bijection_v<fr::RuleCode::I002>);
static_assert(fr::rule_bijection_v<fr::RuleCode::H001>);
static_assert(fr::rule_bijection_v<fr::RuleCode::F002>);

static_assert(fr::ValidComposition<TestProbe>, "fr::ValidComposition must accept Fn<int> probe.");

static_assert(fr::ValidComposition<TestProbe> == sfn::ValidComposition<TestProbe>);

static_assert(std::is_same_v<fr::CollisionRules<TestProbe>, sfn::CollisionRules<TestProbe>>,
              "fr::CollisionRules<F> must alias substrate trait");

static_assert(fr::CollisionRules<TestProbe>::valid);

static_assert(fr::I002_OK<TestProbe>);
static_assert(fr::L002_OK<TestProbe>);
static_assert(fr::E044_OK<TestProbe>);
static_assert(fr::I003_OK<TestProbe>);
static_assert(fr::M012_OK<TestProbe>);
static_assert(fr::P002_OK<TestProbe>);
static_assert(fr::I004_OK<TestProbe>);
static_assert(fr::N002_OK<TestProbe>);
static_assert(fr::L003_OK<TestProbe>);
static_assert(fr::M011_OK<TestProbe>);
static_assert(fr::S010_OK<TestProbe>);
static_assert(fr::S011_OK<TestProbe>);
static_assert(fr::L004_OK<TestProbe>);
static_assert(fr::B001_OK<TestProbe>);
static_assert(fr::H001_OK<TestProbe>);
static_assert(fr::H002_OK<TestProbe>);
static_assert(fr::L005_OK<TestProbe>);
static_assert(fr::F001_OK<TestProbe>);
static_assert(fr::H003_OK<TestProbe>);
static_assert(fr::F002_OK<TestProbe>);

static_assert(fr::AllRulesOK<TestProbe>);

// This is a floor, not an equality.  The exact pin sits beside the
// source-of-truth constant in the header.  A floor here catches only the inverse
// direction, an entry removed from the surface.  Growth past the floor is silent
// and the header equality tracks it.

static_assert(fr::u062_self_test::u062_surface_cardinality >= 28,
              "floor: the fixy::rule surface cardinality is below 28 — an entry was "
              "removed without updating the colocated ceiling pin and this floor "
              "witness.");

int main() { return 0; }
