// Each gate that reads the component walk of
// foundation/reflect/TypeComponents.h sees through a function type and a
// pointer to member.
//
// A function hands out its return type and can write through a
// parameter.  A pointer to member is a function from its class to its
// member.  The walk lists those types as components, so a payload that
// reaches a view, a throw, an endpoint or an effect row only through a
// function or a pointer to member answers as if it held that type.  One
// cell of each pair below states the reach, and the other states that a
// function or a pointer to member that reaches nothing is still a plain
// value.

#include <fixy/Throws.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Payload.h>
#include <foundation/effects/Capability.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/ReadView.h>

#include <cstdio>
#include <type_traits>

namespace component_walk_gates {

namespace eff = ::foundation::effects;
namespace fp = ::foundation::permissions;
namespace s = ::fixy::session;
namespace ctrl = ::fixy::atom::ctrl;

struct Plain {
    int count = 0;
    double mean = 0.0;
};

using BgWork = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

// The extract gate of a pure Computation has its cells in the self-test
// of foundation/effects/Computation.h, because the gate is a detail and
// a test outside the header reaches no detail namespace.

// ── The result gate of a ReadView door ───────────────────────────────

struct ViewRegion {
    using permission_row = eff::Row<>;
};
struct ViewBrand {};
using View = fp::ReadView<ViewRegion, ViewBrand>;
struct HoldsCallReference {
    int (&call)();
};
static_assert(!fp::ReadViewResultStaysInside<HoldsCallReference>,
              "a function reaches a view through state that no type names, as a pointer to one does");
static_assert(!fp::ReadViewResultStaysInside<int (*)()>);
static_assert(!fp::ReadViewResultStaysInside<int (Plain::*)() const>);
static_assert(!fp::ReadViewResultStaysInside<View Plain::*>);
static_assert(fp::ReadViewResultStaysInside<int Plain::*>);

// ── The throws search ────────────────────────────────────────────────

struct sample_exception {};
static_assert(fixy::type_tree_contains_throws_v<ctrl::throws<> (*)()>);
static_assert(fixy::type_tree_contains_throws_v<void (*)(ctrl::throws<sample_exception> const&) noexcept>);
static_assert(fixy::type_tree_contains_throws_v<void (Plain::*)(ctrl::throws<>)>);
static_assert(fixy::type_tree_contains_throws_v<ctrl::throws<> Plain::*>);
static_assert(!fixy::type_tree_contains_throws_v<int (*)(double)>);
static_assert(!fixy::type_tree_contains_throws_v<int Plain::*>);

// ── The delegation query of a session payload ────────────────────────

using Endpoint = s::SessionHandle<s::Recv<int, s::End>, int*>;
static_assert(s::payload_delegation_carrier_v<Endpoint Plain::*> == s::DelegationCarrier::Endpoint,
              "a pointer to an endpoint member reaches the endpoint");
static_assert(s::payload_delegation_carrier_v<int Plain::*> == s::DelegationCarrier::None);

// ── The effect row of a channel payload ──────────────────────────────

static_assert(std::is_same_v<fixy::concurrent::payload_row_t<BgWork (*)()>, eff::Row<eff::Effect::Bg>>,
              "a function that returns an engaged computation carries its row");
static_assert(std::is_same_v<fixy::concurrent::payload_row_t<void (*)(eff::Capability<eff::Effect::IO, eff::Init>)>,
                             eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<fixy::concurrent::payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg> Plain::*>,
                             eff::Row<eff::Effect::Alloc>>);
static_assert(std::is_same_v<fixy::concurrent::payload_row_t<int (*)(double)>, eff::Row<>>);

}  // namespace component_walk_gates

int main() {
    std::puts("test_component_walk_gates: each gate sees through a function type and a pointer to member");
    return 0;
}
