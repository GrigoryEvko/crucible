// A right side holds no active pair.  The rule of ping and pong gives a new
// ping and a new pong whose principal ports meet, so the step makes its own
// pair again and the reduction never stops.  The count of the agents stays
// the same, so the class of growth alone does not see it.

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

inline constexpr RuleTable<1, 2, 1> ping_pong_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "ping", .port_names = {"out", "in"}, .port_types = {give(TypeId{0}), take(TypeId{0})}},
                   {.name = "pong", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"ping_meets_pong", KindId{0}, KindId{1},
                   RightSide{.agents = {KindId{0}, KindId{1}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_agent(1, 0)},
                                       RuleWire{point_at_agent(0, 1), point_at_interface(0)},
                                       RuleWire{point_at_agent(1, 1), point_at_interface(1)}},
                             .agent_count = 2,
                             .wire_count = 3}}}};

inline constexpr RuleVerdict verdict = check_rules(ping_pong_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
