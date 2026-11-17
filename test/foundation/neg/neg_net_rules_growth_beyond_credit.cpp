// A rule that adds agents passes only when the step weights of its pair pay
// for them.  relay >< num gives the value as a join of two copies, so it
// makes 3 agents from 2, through the growth door.  The kinds have no step
// weight, so the pair holds no credit for the added agent.

#include <foundation/algebra/net/Rules.h>

namespace {

using foundation::algebra::net::allow_growth;
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

inline constexpr KindId num{0};
inline constexpr KindId relay{1};
inline constexpr KindId join{2};

inline constexpr RuleTable<1, 3, 2> relay_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                   {.name = "relay", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{0})}},
                   {.name = "join",
                    .port_names = {"out", "left", "right"},
                    .port_types = {give(TypeId{0}), take(TypeId{0}), take(TypeId{0})}}}},
    // relay >< num(a) => join(num(a), num(a)).  The interface: relay.out (0).
    .rules = {allow_growth(Rule{"relay_grows", relay, num,
                                RightSide{.agents = {join, num, num},
                                          .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                                    RuleWire{point_at_agent(1, 0), point_at_agent(0, 1)},
                                                    RuleWire{point_at_agent(2, 0), point_at_agent(0, 2)}},
                                          .agent_count = 3,
                                          .wire_count = 3}},
                           "a relay gives each value as a pair of two copies"),
              // relay >< join(l, r) => join(l, r).  The interface: relay.out
              // (0), join.left (1), join.right (2).
              Rule{"relay_meets_join", relay, join,
                   RightSide{.agents = {join},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                       RuleWire{point_at_agent(0, 1), point_at_interface(1)},
                                       RuleWire{point_at_agent(0, 2), point_at_interface(2)}},
                             .agent_count = 1,
                             .wire_count = 3}}}};

inline constexpr RuleVerdict verdict = check_rules(relay_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
