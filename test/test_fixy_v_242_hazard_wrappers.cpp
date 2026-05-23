// FIXY-V-242 sentinel TU: the 5 value-level Graded carriers for the
// Agent-10 hazard axes —
//   safety/ControlFlow.h   ControlFlowPinned<Tier, T>
//   safety/CallShape.h     CallShapePinned<Tier, T>
//   safety/StackUse.h      StackUsePinned<Tier, T>
//   safety/GlobalState.h   GlobalStatePinned<Tier, T>
//   safety/Stdio.h         StdioPinned<Tier, T>
//
// Each is a regime-1 `Graded<ModalityKind::Absolute, <Axis>Lattice::
// At<Tier>, T>` carrier (sizeof == sizeof(T)) with CAPABILITY-CEILING
// semantics: bottom = safest, satisfies<C> = leq(Tier, C), widen<>
// goes UP the chain (over-approximate capability is sound; tighten is
// not).  This TU compiles all 5 headers' embedded static_asserts under
// project warning flags (the header-only-static_assert-blind-spot
// discipline) and runs each header's runtime_smoke_test() from main().
//
// V-242 ships the wrappers + their HS14 neg-compile fixtures (2 per
// mint = 10) + this positive TU.  row_hash_contribution (RowHashFold.h)
// and the fixy::wrap re-exports mirror the Witness arc's separate-task
// pattern (V-055 / V-056) and are out of V-242's scope.

#include <crucible/safety/CallShape.h>
#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/GlobalState.h>
#include <crucible/safety/StackUse.h>
#include <crucible/safety/Stdio.h>

#include <type_traits>

namespace sf  = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

// ── EBO collapse — every wrapper is byte-equivalent to its payload ──
static_assert(sizeof(sf::ControlFlowPinned<cal::ControlFlow::Pure, int>)        == sizeof(int));
static_assert(sizeof(sf::CallShapePinned<cal::CallShape::Direct, int>)          == sizeof(int));
static_assert(sizeof(sf::StackUsePinned<cal::StackUse::ConstantFrame, int>)     == sizeof(int));
static_assert(sizeof(sf::GlobalStatePinned<cal::GlobalState::Stateless, int>)   == sizeof(int));
static_assert(sizeof(sf::StdioPinned<cal::Stdio::NoStdio, int>)                 == sizeof(int));

// ── All five carriers are Absolute-modality ─────────────────────────
static_assert(sf::ControlFlowPinned<cal::ControlFlow::Pure, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::CallShapePinned<cal::CallShape::Direct, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::StackUsePinned<cal::StackUse::ConstantFrame, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::GlobalStatePinned<cal::GlobalState::Stateless, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::StdioPinned<cal::Stdio::NoStdio, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);

// ── Cross-axis type distinctness — the 5 wrappers are disjoint types ─
//
// A binding pinning one hazard axis must not accidentally satisfy
// another axis's wrapper slot (strong-typing discipline).  Pin the
// pairwise distinctness for the safest-tier instantiation of each.
static_assert(!std::is_same_v<sf::ControlFlowPinned<cal::ControlFlow::Pure, int>,
                              sf::CallShapePinned<cal::CallShape::Direct, int>>);
static_assert(!std::is_same_v<sf::StackUsePinned<cal::StackUse::ConstantFrame, int>,
                              sf::GlobalStatePinned<cal::GlobalState::Stateless, int>>);
static_assert(!std::is_same_v<sf::StdioPinned<cal::Stdio::NoStdio, int>,
                              sf::ControlFlowPinned<cal::ControlFlow::Pure, int>>);

// ── Ceiling-direction satisfies — bottom satisfies the strictest ────
//
// Hot-path admission imposes the bottom tier as the ceiling; only the
// bottom-tier carrier satisfies it.  These pin the load-bearing
// admission semantics for all 5 axes (the inverse-of-Witness direction).
static_assert(sf::ControlFlowPinned<cal::ControlFlow::Pure, int>::satisfies<cal::ControlFlow::Pure>);
static_assert(!sf::ControlFlowPinned<cal::ControlFlow::MaySignal, int>::satisfies<cal::ControlFlow::Pure>);
static_assert(sf::CallShapePinned<cal::CallShape::Direct, int>::satisfies<cal::CallShape::Direct>);
static_assert(!sf::CallShapePinned<cal::CallShape::Unbounded, int>::satisfies<cal::CallShape::Direct>);
static_assert(sf::StackUsePinned<cal::StackUse::ConstantFrame, int>::satisfies<cal::StackUse::ConstantFrame>);
static_assert(!sf::StackUsePinned<cal::StackUse::Unbounded, int>::satisfies<cal::StackUse::ConstantFrame>);
static_assert(sf::GlobalStatePinned<cal::GlobalState::Stateless, int>::satisfies<cal::GlobalState::Stateless>);
static_assert(!sf::GlobalStatePinned<cal::GlobalState::InitOrderHazard, int>::satisfies<cal::GlobalState::Stateless>);
static_assert(sf::StdioPinned<cal::Stdio::NoStdio, int>::satisfies<cal::Stdio::NoStdio>);
static_assert(!sf::StdioPinned<cal::Stdio::InteractiveRead, int>::satisfies<cal::Stdio::NoStdio>);

// ── widen UP is well-formed; the wrapper exposes both &/&& overloads ─
static_assert(std::is_same_v<
    decltype(std::declval<const sf::ControlFlowPinned<cal::ControlFlow::Pure, int>&>()
                 .widen<cal::ControlFlow::MaySignal>()),
    sf::ControlFlowPinned<cal::ControlFlow::MaySignal, int>>);

}  // namespace

int main() {
    sf::detail::control_flow_pinned_self_test::runtime_smoke_test();
    sf::detail::call_shape_pinned_self_test::runtime_smoke_test();
    sf::detail::stack_use_pinned_self_test::runtime_smoke_test();
    sf::detail::global_state_pinned_self_test::runtime_smoke_test();
    sf::detail::stdio_pinned_self_test::runtime_smoke_test();
    return 0;
}
