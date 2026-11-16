// A step that adds agents allocates.  The rule of relay and num makes three
// agents from two, and it does not go through the growth door allow_growth,
// so the table stops the build.

#include <foundation/algebra/net/Rules.h>

namespace {

using foundation::algebra::net::give;
using foundation::algebra::net::KindId;
using foundation::algebra::net::point_at_agent;
using foundation::algebra::net::point_at_interface;
using foundation::algebra::net::RightSide;
using foundation::algebra::net::Rule;
using foundation::algebra::net::RuleTable;
using foundation::algebra::net::RuleVerdict;
using foundation::algebra::net::RuleWire;
using foundation::algebra::net::take;
using foundation::algebra::net::TypeId;

inline constexpr RuleTable<1, 2, 1> relay_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                   {.name = "relay", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"relay_grows", KindId{1}, KindId{0},
                   RightSide{.agents = {KindId{0}, KindId{1}, KindId{1}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_agent(1, 0)},
                                       RuleWire{point_at_agent(1, 1), point_at_agent(2, 0)},
                                       RuleWire{point_at_agent(2, 1), point_at_interface(0)}},
                             .agent_count = 3,
                             .wire_count = 3}}}};

inline constexpr RuleVerdict verdict = check_rules(relay_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
