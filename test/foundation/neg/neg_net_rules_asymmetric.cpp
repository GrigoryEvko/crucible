// A pair of two agents of one kind has no order, so its rule is symmetric.
// The rule of two pivots puts the free port of the left pivot at the
// principal port of a new pivot, and the free port of the right one at its
// other port, so the result depends on which pivot is the left one.

#include <foundation/algebra/net/Rules.h>

namespace {

using foundation::algebra::net::KindId;
using foundation::algebra::net::meet;
using foundation::algebra::net::point_at_agent;
using foundation::algebra::net::point_at_interface;
using foundation::algebra::net::RightSide;
using foundation::algebra::net::Rule;
using foundation::algebra::net::RuleTable;
using foundation::algebra::net::RuleVerdict;
using foundation::algebra::net::RuleWire;
using foundation::algebra::net::TypeId;

inline constexpr RuleTable<1, 1, 1> pivot_table{
    .signature = {.type_names = {"Term"},
                  .kinds = {{.name = "pivot",
                             .port_names = {"principal", "aux"},
                             .port_types = {meet(TypeId{0}), meet(TypeId{0})}}}},
    .rules = {Rule{"pivots_twist", KindId{0}, KindId{0},
                   RightSide{.agents = {KindId{0}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                       RuleWire{point_at_agent(0, 1), point_at_interface(1)}},
                             .agent_count = 1,
                             .wire_count = 2}}}};

inline constexpr RuleVerdict verdict = check_rules(pivot_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
