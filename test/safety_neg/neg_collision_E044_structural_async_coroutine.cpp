// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-FOUND-067 Phase 2 STRUCTURAL fixture: E044 fires for a Fn with
// constant-time discipline (marks_ct = true) AND ReentrancyMode::Coroutine
// WITHOUT any per-Fn `marks_async` specialization.  The OR-fold in
// `has_async_v<F>` reads F's Reentrancy axis directly and detects
// Coroutine as a flavor of async — so E044's `has_ct_v && has_async_v`
// gate trips automatically.
//
// Before FOUND-067 Phase 2 this binding would compile cleanly because
// marks_async<Bad> defaulted to std::false_type and no explicit
// specialization existed.  After Phase 2, structural detection
// graduates the L002/E044/I004 family from "dormant unless author
// specializes" to "fires automatically when Reentrancy::Coroutine or
// Row<Bg> is structurally engaged".  E044 specifically is uniquely
// catchable here: R001 requires hot_path × Coroutine; R002 requires
// Borrow × Coroutine; R003 requires Bg × Coroutine.  None of those
// fire for marks_ct × Coroutine alone — E044 is the discriminator.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>` so the
// `static_assert(ValidComposition<Fn>)` inside Fn runs validate() which
// reads CollisionRules::async (now OR-folded via partial-spec
// Reentrancy parameter — cycle-safe per
// feedback_collision_rules_partial_spec_cycle.md).
//
// Expected diagnostic substring: "E044".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_e044_structural {

using Bad = fn::Fn<
    int,                                       // 1  Type — bare int (no HotPath wrapper)
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage — Linear (NOT Borrow — avoids R002/L007)
    fx::Row<>,                                 // 4  EffectRow — empty (NOT Bg — avoids R003/L007)
    fn::SecLevel::Public,                      // 5  Security (NOT Classified — avoids I004 path)
    fn::proto::None,                           // 6  Protocol — None (avoids I004 session_protocol arm)
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost — bounded (avoids H001/H003)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::Coroutine,             // 16 Reentrancy — COROUTINE (E044 trigger
                                                //                 via structural has_async_v)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_e044_structural

// marks_ct engaged — the constant-time discipline trigger.  CT × async
// is E044.  Note: marks_async is NOT specialized; the structural
// detection (Reentrancy::Coroutine) supplies the async half.
namespace crucible::safety::fn::collision {
    template <> struct marks_ct<::neg_collision_e044_structural::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_e044_structural::Bad the_fixture{};

int main() { return 0; }
