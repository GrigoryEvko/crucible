// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and runs each header's runtime smoke
// test from main.

#include <crucible/safety/CallShape.h>
#include <crucible/safety/ControlFlow.h>
#include <crucible/safety/GlobalState.h>
#include <crucible/safety/StackUse.h>
#include <crucible/safety/Stdio.h>

#include <type_traits>

namespace sf = ::crucible::safety;
namespace cal = ::crucible::algebra::lattices;

namespace {

static_assert(sizeof(sf::ControlFlowPinned<cal::ControlFlow::Pure, int>) == sizeof(int));
static_assert(sizeof(sf::CallShapePinned<cal::CallShape::Direct, int>) == sizeof(int));
static_assert(sizeof(sf::StackUsePinned<cal::StackUse::ConstantFrame, int>) == sizeof(int));
static_assert(sizeof(sf::GlobalStatePinned<cal::GlobalState::Stateless, int>) == sizeof(int));
static_assert(sizeof(sf::StdioPinned<cal::Stdio::NoStdio, int>) == sizeof(int));

static_assert(sf::ControlFlowPinned<cal::ControlFlow::Pure, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::CallShapePinned<cal::CallShape::Direct, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::StackUsePinned<cal::StackUse::ConstantFrame, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::GlobalStatePinned<cal::GlobalState::Stateless, int>::modality
              == ::crucible::algebra::ModalityKind::Absolute);
static_assert(sf::StdioPinned<cal::Stdio::NoStdio, int>::modality == ::crucible::algebra::ModalityKind::Absolute);

// A value pinning one hazard axis must not satisfy another axis's wrapper
// slot. These pin pairwise distinctness at the safest tier of each.
static_assert(!std::is_same_v<sf::ControlFlowPinned<cal::ControlFlow::Pure, int>,
                              sf::CallShapePinned<cal::CallShape::Direct, int>>);
static_assert(!std::is_same_v<sf::StackUsePinned<cal::StackUse::ConstantFrame, int>,
                              sf::GlobalStatePinned<cal::GlobalState::Stateless, int>>);
static_assert(
    !std::is_same_v<sf::StdioPinned<cal::Stdio::NoStdio, int>, sf::ControlFlowPinned<cal::ControlFlow::Pure, int>>);

// Bottom is the safest tier and satisfies is leq(Tier, ceiling), so a
// carrier widens up the chain. Over-approximating a capability is sound.
// Tightening one is not. Hot-path admission imposes the bottom tier as
// the ceiling, so only the bottom-tier carrier satisfies it.
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

static_assert(std::is_same_v<decltype(std::declval<const sf::ControlFlowPinned<cal::ControlFlow::Pure, int>&>()
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
