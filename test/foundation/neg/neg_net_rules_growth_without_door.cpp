// A step that adds agents allocates.  The rule of relay and num gives the
// value as a join of two copies, so it makes three agents from two.  It does
// not go through the growth door allow_growth, so the table stops the build.

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

inline constexpr RuleTable<1, 3, 2> relay_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                   {.name = "relay", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{0})}},
                   {.name = "join",
                    .port_names = {"out", "left", "right"},
                    .port_types = {give(TypeId{0}), take(TypeId{0}), take(TypeId{0})}}}},
    .rules = {Rule{"relay_grows", KindId{1}, KindId{0},
                   RightSide{.agents = {KindId{2}, KindId{0}, KindId{0}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                       RuleWire{point_at_agent(1, 0), point_at_agent(0, 1)},
                                       RuleWire{point_at_agent(2, 0), point_at_agent(0, 2)}},
                             .agent_count = 3,
                             .wire_count = 3}},
              Rule{"relay_meets_join", KindId{1}, KindId{2},
                   RightSide{.agents = {KindId{2}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                       RuleWire{point_at_agent(0, 1), point_at_interface(1)},
                                       RuleWire{point_at_agent(0, 2), point_at_interface(2)}},
                             .agent_count = 1,
                             .wire_count = 3}}}};

inline constexpr RuleVerdict verdict = check_rules(relay_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
