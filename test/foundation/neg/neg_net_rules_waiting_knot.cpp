// The new agents of a right side do not wait on each other.  origin meets
// taker, and the right side makes two knots: each one gives its loop port to
// the input of the other one.  No input of the pair takes part, so only a
// sort of the new agents finds the cycle.  Each pair with dual principal
// ports has a rule, so the table is total.

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

inline constexpr KindId origin{0};
inline constexpr KindId taker{1};
inline constexpr KindId first_knot{2};
inline constexpr KindId second_knot{3};

inline constexpr RuleTable<1, 4, 3> knot_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "origin", .port_names = {"out"}, .port_types = {give(TypeId{0})}},
                   {.name = "taker", .port_names = {"in", "result"}, .port_types = {take(TypeId{0}), give(TypeId{0})}},
                   {.name = "first_knot",
                    .port_names = {"in", "loop", "out"},
                    .port_types = {take(TypeId{0}), give(TypeId{0}), give(TypeId{0})}},
                   {.name = "second_knot",
                    .port_names = {"in", "loop"},
                    .port_types = {take(TypeId{0}), give(TypeId{0})}}}},
    // The interface: taker.result (0).
    .rules = {Rule{"origin_meets_taker", origin, taker,
                   RightSide{.agents = {first_knot, second_knot},
                             .wires = {RuleWire{point_at_agent(0, 1), point_at_agent(1, 0)},
                                       RuleWire{point_at_agent(1, 1), point_at_agent(0, 0)},
                                       RuleWire{point_at_agent(0, 2), point_at_interface(0)}},
                             .agent_count = 2,
                             .wire_count = 3}},
              // The interface: first_knot.loop (0), first_knot.out (1).
              Rule{"origin_meets_first_knot", origin, first_knot,
                   RightSide{.agents = {origin, origin},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                       RuleWire{point_at_agent(1, 0), point_at_interface(1)}},
                             .agent_count = 2,
                             .wire_count = 2}},
              // The interface: second_knot.loop (0).
              Rule{"origin_meets_second_knot", origin, second_knot,
                   RightSide{.agents = {origin},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)}},
                             .agent_count = 1,
                             .wire_count = 1}}}};

inline constexpr RuleVerdict verdict = check_rules(knot_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
