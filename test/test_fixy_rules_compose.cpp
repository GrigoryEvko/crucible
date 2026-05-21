// ── test_fixy_rules_compose — sentinel TU for FIXY-U-062 surface ──
//
// Pulls fixy/Rules.h into a TU compiled under project warning flags
// so the header's in-namespace sentinels execute under enforcement.
// Witnesses (orthogonal to test_fixy_rule_helpers.cpp which covers
// the pack:: helpers):
//
//   1. RuleCode enum reaches through fixy::rule:: + bijection helpers.
//   2. ValidComposition concept resolves through the alias.
//   3. CollisionRules<F> trait alias preserves identity.
//   4. All 20 per-rule concepts (I002_OK..F002_OK) + AllRulesOK
//      aggregate accept the canonical Fn<int> positive probe.
//   5. Cardinality witness — must equal the in-header sentinel.
//
// FIXY-U-062.

#include <crucible/fixy/Rules.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fr    = ::crucible::fixy::rule;
namespace sfn   = ::crucible::safety::fn;
namespace scoll = ::crucible::safety::fn::collision;

using TestProbe = sfn::Fn<int>;

// ─── 1. RuleCode enum + bijection helpers identity ────────────────

static_assert(std::is_same_v<fr::RuleCode, scoll::RuleCode>,
    "fixy::rule::RuleCode must alias substrate enum");

static_assert(fr::RuleCode::I002 == scoll::RuleCode::I002);
static_assert(fr::RuleCode::F002 == scoll::RuleCode::F002);
static_assert(fr::RuleCode::None == scoll::RuleCode::None);

static_assert(fr::rule_code_of_v<scoll::I002_ClassifiedFailPayload>
              == fr::RuleCode::I002);
static_assert(fr::rule_code_of_v<scoll::F002_FederationPeerTerminatingBudget>
              == fr::RuleCode::F002);

static_assert(std::is_same_v<
    fr::rule_tag_t<fr::RuleCode::I002>,
    scoll::I002_ClassifiedFailPayload>,
    "fixy::rule::rule_tag_t must resolve to substrate tag");

static_assert(fr::rule_bijection_v<fr::RuleCode::I002>);
static_assert(fr::rule_bijection_v<fr::RuleCode::H001>);
static_assert(fr::rule_bijection_v<fr::RuleCode::F002>);

// ─── 2. ValidComposition concept reaches through the alias ────────

static_assert(fr::ValidComposition<TestProbe>,
    "fr::ValidComposition must accept Fn<int> probe.");

// Concept aliasing preserves the concept's evaluation semantics —
// fr::ValidComposition<F> == sfn::ValidComposition<F> for any F.
static_assert(fr::ValidComposition<TestProbe>
              == sfn::ValidComposition<TestProbe>);

// ─── 3. CollisionRules<F> trait alias preserves identity ──────────

static_assert(std::is_same_v<
    fr::CollisionRules<TestProbe>,
    sfn::CollisionRules<TestProbe>>,
    "fr::CollisionRules<F> must alias substrate trait");

static_assert(fr::CollisionRules<TestProbe>::valid);

// ─── 4. All 20 per-rule concepts + AllRulesOK ─────────────────────

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

// ─── 5. Cardinality FLOOR mirror — per FIXY-U-127 / U-128 ─────────
//
// The EXACT ceiling pin (`== 28`) lives in fixy/Rules.h colocated
// with the source-of-truth constant; THIS TU only holds the FLOOR
// pin (`>= 28`) which catches the inverse direction — an accidental
// REMOVAL of a U-062 surface entry.  Growth past 28 is silent here
// and auto-tracked by the header's `==` ceiling.

static_assert(fr::u062_self_test::u062_surface_cardinality >= 28,
    "floor: fixy::rule:: U-062 surface cardinality regressed below "
    "28 — an entry was removed without updating both Rules.h's "
    "colocated ceiling pin AND this floor witness.");

int main() {
    // Compile-time-only sentinel; no runtime witness needed.
    return 0;
}
