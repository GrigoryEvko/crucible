// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule P010 (FIXY-FOUND-064):
//
//     F::usage_v == UsageMode::Ghost
//   ∧ ( row_has_effect_v<F::effect_row_t, effects::Effect::Alloc>
//     ∨ row_has_effect_v<F::effect_row_t, effects::Effect::IO>
//     ∨ row_has_effect_v<F::effect_row_t, effects::Effect::Block> )
//   ⇒ ill-formed
//
// Plain English: a Ghost-usage binding is erased at codegen (no emitted
// instructions, no register pressure, no stack footprint).  Alloc emits
// heap-touching code; IO emits syscall / kernel-mediated traffic; Block
// emits blocking primitives.  ALL THREE require emitted instructions;
// Ghost contractually forbids them.  Marking a Ghost binding with any
// of the three observable runtime-effect atoms in its effect row is a
// structural erasure-contract violation.
//
// Gap closed by P010 (FIXY-FOUND-064): P002 catches the marker-driven
// variant `Ghost × marks_runtime_ghost_use<F>::value` (a grant-driven
// detection that fires only when downstream code SPECIALIZES the trait
// on the offending Fn).  But a Ghost binding declared with
// effect_row_t = Row<Alloc> (or IO or Block) and NO marker specialization
// slips P002 entirely — yet is still the same erasure violation P002
// targets.  P010 catches the structural effect-row read where P002
// catches the marker-driven case.  Structurally parallel to H010
// (HotPath × Bg) but on the Usage axis instead of the HotPath marker.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the validate() leg — the
// concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "P010:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_p010 {

// A Ghost-usage Fn with Row<Alloc> — fires P010 cleanly (no overlap
// with H010's HotPath × Bg shape, no overlap with H001/H002/H003 which
// gate on marks_hot_path).  Using cost::Constant and refinement
// pred::True; Ghost-usage normally would not also be marked hot-path,
// so this fixture isolates the Ghost × Alloc contradiction.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Ghost,                      // 3  Usage — Ghost
    fx::Row<fx::Effect::Alloc>,                // 4  EffectRow — Alloc atom (observable)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_p010

[[maybe_unused]] neg_collision_p010::Bad the_fixture{};

int main() { return 0; }
