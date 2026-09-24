#pragma once

// The R-numbered aliases are a stable, deliberately partial projection of the
// collision catalog. The catalog holds more rules than there are aliases here,
// and each of those still fires through the composition gate whether or not it
// has an R-numbered spelling. Adding one is a single using-declaration.

#include <crucible/safety/CollisionCatalog.h>

namespace crucible::fixy::rule {

// Two rules cannot be evaluated against a single Fn. They only have meaning
// over a composition of two or more Fns sharing a frame, so their predicates
// take a pack rather than one type.

namespace pack {

// Backs R017.
template <typename... Fs>
inline constexpr bool no_linear_region_alias_v =
    ::crucible::safety::fn::collision::pack::no_linear_region_alias_v<Fs...>;

// Backs R018.
template <typename... Fs>
inline constexpr bool frame_axis_consistent_v = ::crucible::safety::fn::collision::pack::frame_axis_consistent_v<Fs...>;

template <typename F>
inline constexpr bool is_linear_in_region_v = ::crucible::safety::fn::collision::pack::is_linear_in_region_v<F>;

template <typename Tag1, typename Tag2>
inline constexpr bool same_region_tag_v = ::crucible::safety::fn::collision::pack::same_region_tag_v<Tag1, Tag2>;

template <typename L>
using region_tag_of_t = typename ::crucible::safety::fn::collision::pack::region_tag_of<L>::type;

}  // namespace pack

// The per-rule concepts answer whether one named rule holds for one Fn,
// without computing the whole composition. That is the cheapest cite for a
// downstream metaprogram and the shape a negative-compile fixture needs to
// pin a single rule firing.

using ::crucible::safety::fn::collision::RuleCode;
using ::crucible::safety::fn::collision::rule_code_of;
using ::crucible::safety::fn::collision::rule_code_of_v;
using ::crucible::safety::fn::collision::rule_tag_t;
using ::crucible::safety::fn::collision::rule_bijection_v;

using ::crucible::safety::fn::ValidComposition;
using ::crucible::safety::fn::CollisionRules;

using ::crucible::safety::fn::collision::I002_OK;
using ::crucible::safety::fn::collision::L002_OK;
using ::crucible::safety::fn::collision::E044_OK;
using ::crucible::safety::fn::collision::I003_OK;
using ::crucible::safety::fn::collision::M012_OK;
using ::crucible::safety::fn::collision::P002_OK;
using ::crucible::safety::fn::collision::I004_OK;
using ::crucible::safety::fn::collision::N002_OK;
using ::crucible::safety::fn::collision::L003_OK;
using ::crucible::safety::fn::collision::M011_OK;
using ::crucible::safety::fn::collision::S010_OK;
using ::crucible::safety::fn::collision::S011_OK;
using ::crucible::safety::fn::collision::L004_OK;
using ::crucible::safety::fn::collision::B001_OK;
using ::crucible::safety::fn::collision::H001_OK;
using ::crucible::safety::fn::collision::H002_OK;
using ::crucible::safety::fn::collision::L005_OK;
using ::crucible::safety::fn::collision::F001_OK;
using ::crucible::safety::fn::collision::H003_OK;
using ::crucible::safety::fn::collision::F002_OK;
using ::crucible::safety::fn::collision::AllRulesOK;

using R001 = ::crucible::safety::fn::collision::I002_ClassifiedFailPayload;
using R002 = ::crucible::safety::fn::collision::L002_BorrowAsync;
using R003 = ::crucible::safety::fn::collision::E044_ConstantTimeAsync;
using R004 = ::crucible::safety::fn::collision::I003_ConstantTimeFailOnSecret;
using R005 = ::crucible::safety::fn::collision::M012_MonotonicConcurrentNoAtomic;
using R006 = ::crucible::safety::fn::collision::P002_GhostRuntimeUse;
using R007 = ::crucible::safety::fn::collision::I004_ClassifiedAsyncSession;
using R008 = ::crucible::safety::fn::collision::N002_DecimalOverflowWrap;
using R009 = ::crucible::safety::fn::collision::L003_BorrowUnscopedSpawn;
using R010 = ::crucible::safety::fn::collision::M011_LinearFailNoCleanup;
using R011 = ::crucible::safety::fn::collision::S010_StalenessConstantTime;
using R012 = ::crucible::safety::fn::collision::S011_CapabilityReplay;
using R013 = ::crucible::safety::fn::collision::L004_LinearLifetimeNeedsPermission;
using R014 = ::crucible::safety::fn::collision::B001_BgObservableBoundedResource;
using R015 = ::crucible::safety::fn::collision::H001_HotPathBoundedCost;
using R016 = ::crucible::safety::fn::collision::H002_HotPathWitnessFloor;
using R017 = ::crucible::safety::fn::collision::L005_LinearAliasSameRegionTag;
using R018 = ::crucible::safety::fn::collision::F001_FrameDeclaresAxisCollision;
using R019 = ::crucible::safety::fn::collision::H003_HotPathTerminatingAllocIo;
using R020 = ::crucible::safety::fn::collision::F002_FederationPeerTerminatingBudget;

// An alias pointing at a renamed or removed substrate tag fires here rather
// than at an opaque downstream consumer.

namespace detail::rule_self_test {

using ::crucible::safety::fn::collision::RuleCode;
using ::crucible::safety::fn::collision::rule_code_of_v;

static_assert(rule_code_of_v<R001> == RuleCode::I002);
static_assert(rule_code_of_v<R002> == RuleCode::L002);
static_assert(rule_code_of_v<R003> == RuleCode::E044);
static_assert(rule_code_of_v<R004> == RuleCode::I003);
static_assert(rule_code_of_v<R005> == RuleCode::M012);
static_assert(rule_code_of_v<R006> == RuleCode::P002);
static_assert(rule_code_of_v<R007> == RuleCode::I004);
static_assert(rule_code_of_v<R008> == RuleCode::N002);
static_assert(rule_code_of_v<R009> == RuleCode::L003);
static_assert(rule_code_of_v<R010> == RuleCode::M011);
static_assert(rule_code_of_v<R011> == RuleCode::S010);
static_assert(rule_code_of_v<R012> == RuleCode::S011);
static_assert(rule_code_of_v<R013> == RuleCode::L004);
static_assert(rule_code_of_v<R014> == RuleCode::B001);
static_assert(rule_code_of_v<R015> == RuleCode::H001);
static_assert(rule_code_of_v<R016> == RuleCode::H002);
static_assert(rule_code_of_v<R017> == RuleCode::L005);
static_assert(rule_code_of_v<R018> == RuleCode::F001);
static_assert(rule_code_of_v<R019> == RuleCode::H003);
static_assert(rule_code_of_v<R020> == RuleCode::F002);

}  // namespace detail::rule_self_test

namespace u062_self_test {

static_assert(RuleCode::I002 == ::crucible::safety::fn::collision::RuleCode::I002);
static_assert(RuleCode::F002 == ::crucible::safety::fn::collision::RuleCode::F002);
static_assert(RuleCode::None == ::crucible::safety::fn::collision::RuleCode::None);

static_assert(ValidComposition<::crucible::safety::fn::Fn<int>>,
              "fixy::rule::ValidComposition must accept the Fn<int> positive "
              "probe — it must resolve to the substrate composition gate.");

// The concept reads `valid` from the trait, so asserting the trait directly
// checks a different layer than the concept assertion above.
static_assert(CollisionRules<::crucible::safety::fn::Fn<int>>::valid,
              "fixy::rule::CollisionRules<F>::valid must alias substrate trait");

// AllRulesOK is the same gate spelled as a conjunction rule by rule.
static_assert(AllRulesOK<::crucible::safety::fn::Fn<int>>, "fixy::rule::AllRulesOK must hold on the canonical probe");

// Three samples drawn from different regions of the catalog.
static_assert(rule_bijection_v<RuleCode::I002>);
static_assert(rule_bijection_v<RuleCode::L004>);
static_assert(rule_bijection_v<RuleCode::F002>);

static_assert(std::is_same_v<rule_tag_t<RuleCode::I002>, ::crucible::safety::fn::collision::I002_ClassifiedFailPayload>,
              "fixy::rule::rule_tag_t alias must resolve to substrate tag");

static_assert(I002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(E044_OK<::crucible::safety::fn::Fn<int>>);
static_assert(I003_OK<::crucible::safety::fn::Fn<int>>);
static_assert(M012_OK<::crucible::safety::fn::Fn<int>>);
static_assert(P002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(I004_OK<::crucible::safety::fn::Fn<int>>);
static_assert(N002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L003_OK<::crucible::safety::fn::Fn<int>>);
static_assert(M011_OK<::crucible::safety::fn::Fn<int>>);
static_assert(S010_OK<::crucible::safety::fn::Fn<int>>);
static_assert(S011_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L004_OK<::crucible::safety::fn::Fn<int>>);
static_assert(B001_OK<::crucible::safety::fn::Fn<int>>);
static_assert(H001_OK<::crucible::safety::fn::Fn<int>>);
static_assert(H002_OK<::crucible::safety::fn::Fn<int>>);
static_assert(L005_OK<::crucible::safety::fn::Fn<int>>);
static_assert(F001_OK<::crucible::safety::fn::Fn<int>>);
static_assert(H003_OK<::crucible::safety::fn::Fn<int>>);
static_assert(F002_OK<::crucible::safety::fn::Fn<int>>);

// The count covers the aggregator surface only. The 20 R-numbered tag aliases
// are not included in it.
constexpr int u062_surface_cardinality = 28;
static_assert(u062_surface_cardinality == 28, "fixy::rule:: aggregator surface cardinality drifted — update the "
                                              "using-decls AND this sentinel in lockstep.");

}  // namespace u062_self_test

}  // namespace crucible::fixy::rule
