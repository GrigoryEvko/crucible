// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-067 Phase 1 STRUCTURAL fixture: H001 fires WITHOUT any
// per-Fn `marks_hot_path` specialization.  The Fn's type_t is
// HotPath<Hot, int> — the §XVI canonical outer wrapper — and the
// collision::is_hot_path_v<F> OR-fold detects it structurally via
// extract::is_hot_path_v + extract::hot_path_tier_v == Hot.
//
// Before FOUND-067 this binding would compile cleanly because
// marks_hot_path<Bad> defaulted to std::false_type and no explicit
// specialization existed.  After FOUND-067, structural detection
// graduates the H001..H003 / H010 / W001 / V201 / V301 / S001 / R001
// family from "dormant unless author specializes" to "fires
// automatically on any production-shape HotPath<Hot, ...> wrapper".
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>` so the
// `static_assert(ValidComposition<Fn>)` inside Fn runs the validate()
// leg.  No marks_hot_path specialization — the structural detector
// alone drives the rejection.
//
// Expected diagnostic substring: "H001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/HotPath.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h001_structural {

// Fn payload is HotPath<Hot, int> — outermost wrapper declares hot tier.
// Cost is Unbounded — H001 trigger.  NO marks_hot_path specialization
// anywhere in this TU: the structural detector alone graduates the rule.
using HotInt = ::crucible::safety::HotPath<
    ::crucible::safety::HotPathTier_v::Hot, int>;

using Bad = fn::Fn<
    HotInt,                                    // 1  Type — HotPath<Hot, int>
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow — empty
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (H001)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h001_structural

// NO marks_hot_path specialization — H001 fires via the structural
// path alone.  This is the load-bearing FOUND-067 witness.

[[maybe_unused]] neg_collision_h001_structural::Bad the_fixture{};

int main() { return 0; }
