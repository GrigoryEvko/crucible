// Each step lowers the weight of the net, so each reduction ends.  A token
// that meets a node goes past it: the right side holds a new node and a new
// token, so the step keeps the count of the agents.  On a ring of nodes the
// token never stops.  The right side holds no active pair, and the step
// allocates nothing, so only the step weights see it.  No step weights can
// admit the rule, because its right side holds the kinds of its pair.

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

// The interface of the pair: token.rear (0), node.front (1).
inline constexpr RuleTable<1, 2, 1> token_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "token", .port_names = {"front", "rear"}, .port_types = {give(TypeId{0}), take(TypeId{0})}},
                   {.name = "node",
                    .port_names = {"rear", "front"},
                    .port_types = {take(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"token_meets_node", KindId{0}, KindId{1},
                   RightSide{.agents = {KindId{1}, KindId{0}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                       RuleWire{point_at_agent(0, 1), point_at_agent(1, 1)},
                                       RuleWire{point_at_agent(1, 0), point_at_interface(1)}},
                             .agent_count = 2,
                             .wire_count = 3}}}};

inline constexpr RuleVerdict verdict = check_rules(token_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
