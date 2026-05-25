// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H003 — IO-arm trigger path (FIXY-FOUND-069).
//
// Companion fixture to neg_collision_H003_hotpath_alloc_unbounded.cpp
// (which covers the ALLOC-arm trigger).  H003_OK gates on:
//
//   concept H003_OK = !(is_hot_path_v<F> &&
//                       (row_has_effect_v<effect_row_t, Effect::Alloc> ||
//                        row_has_effect_v<effect_row_t, Effect::IO>) &&
//                       is_unbounded_cost<cost_t>::value);
//
// The inner middle conjunct is an OR-fold over (Alloc, IO).  The
// shipped fixture exercises the ALLOC arm via `fx::Row<fx::Effect::
// Alloc>`.  THIS fixture exercises the IO arm via `fx::Row<fx::Effect::
// IO>` — same outer shape (is_hot_path + unbounded cost), different
// inner OR-arm.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops `row_has_effect<Alloc>` from the inner OR
//     breaks the Alloc-arm path → caught by the original fixture.
//   * A refactor that drops `row_has_effect<IO>` from the inner OR
//     breaks the IO-arm path → caught by THIS fixture.
//   * Without both, the OR-fold could silently degenerate to a
//     single-effect rule and ship.
//
// Mismatch class: HotPath × Row<IO> × Unbounded cost.  Distinct from
// the Alloc class because the IO atom is the active OR-arm; an
// Unbounded-cost hot-path with IO is structurally identical to one
// with Alloc — both indicate the unbounded latency would manifest
// through observable side-channels.
//
// Expected diagnostic substring: "H003:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h003_io {

// A Fn with marks_hot_path + Row<IO> + cost::Unbounded.  H001 co-fires
// (any HotPath × unbounded); H003 alone catches the IO-path specific
// shape.  Diagnostic "H003:" appears via Fn body's
// static_assert(ValidComposition<F>).
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::IO>,                   // 4  EffectRow — IO engaged (H003 IO-arm
                                                //                trigger; companion to Alloc fixture)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (H003 trigger paired
                                                //                with marks_hot_path + IO)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h003_io

namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_h003_io::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_h003_io::Bad the_fixture{};

int main() { return 0; }
