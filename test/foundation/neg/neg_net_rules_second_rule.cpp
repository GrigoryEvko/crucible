// A pair of agents has at most one rule, so each step of a reduction commutes
// with each other step.  A second rule for num and add, in the other order,
// stops the build, and the refusal names the two rules.

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

inline constexpr RightSide add_left_side{.agents = {KindId{2}},
                                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                                   RuleWire{point_at_agent(0, 1), point_at_interface(1)}},
                                         .agent_count = 1,
                                         .wire_count = 2};
inline constexpr RightSide num_side{.agents = {KindId{0}},
                                    .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)}},
                                    .agent_count = 1,
                                    .wire_count = 1};

inline constexpr RuleTable<1, 3, 3> sum_table{
    .signature = {.type_names = {"Value"},
                  .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                            {.name = "add",
                             .port_names = {"left", "right", "sum"},
                             .port_types = {take(TypeId{0}), take(TypeId{0}), give(TypeId{0})}},
                            {.name = "add_left",
                             .port_names = {"right", "sum"},
                             .port_types = {take(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"add_meets_num", KindId{1}, KindId{0}, add_left_side},
              Rule{"add_left_meets_num", KindId{2}, KindId{0}, num_side},
              Rule{"num_meets_add_again", KindId{0}, KindId{1}, add_left_side}}};

inline constexpr RuleVerdict verdict = check_rules(sum_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
