// A step of a dataflow rule adds no path from an input to an output.  maker
// gives to user, so through the pair x reaches y and v, and u reaches v.  The
// right side joins y to u, so u reaches y.  A net in which y feeds u through
// an agent outside the pair has no cycle, and one step of the rule closes a
// cycle of waits in it.

#include <foundation/algebra/net/Rules.h>

namespace {

using foundation::algebra::net::give;
using foundation::algebra::net::KindId;
using foundation::algebra::net::point_at_interface;
using foundation::algebra::net::RightSide;
using foundation::algebra::net::Rule;
using foundation::algebra::net::RuleTable;
using foundation::algebra::net::RuleVerdict;
using foundation::algebra::net::RuleWire;
using foundation::algebra::net::take;
using foundation::algebra::net::TypeId;

// The interface of the pair: maker.x (0), maker.y (1), user.u (2), user.v (3).
inline constexpr RuleTable<1, 2, 1> crossing_table{
    .signature = {.type_names = {"Value"},
                  .kinds = {{.name = "maker",
                             .port_names = {"out", "x", "y"},
                             .port_types = {give(TypeId{0}), take(TypeId{0}), give(TypeId{0})}},
                            {.name = "user",
                             .port_names = {"in", "u", "v"},
                             .port_types = {take(TypeId{0}), take(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"maker_meets_user", KindId{0}, KindId{1},
                   RightSide{.agents = {},
                             .wires = {RuleWire{point_at_interface(0), point_at_interface(3)},
                                       RuleWire{point_at_interface(1), point_at_interface(2)}},
                             .agent_count = 0,
                             .wire_count = 2}}}};

inline constexpr RuleVerdict verdict = check_rules(crossing_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
