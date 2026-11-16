// A port of a right side that takes the place of a free port keeps the type of
// that port.  The rule puts the input of a dup where probe gave a value, so
// the agent at that port would meet a port of the wrong type.

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

inline constexpr RuleTable<1, 3, 1> probe_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                   {.name = "probe", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{0})}},
                   {.name = "dup",
                    .port_names = {"in", "first", "second"},
                    .port_types = {take(TypeId{0}), give(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"probe_takes_a_value", KindId{1}, KindId{0},
                   RightSide{.agents = {KindId{2}},
                             .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)}},
                             .agent_count = 1,
                             .wire_count = 1}}}};

inline constexpr RuleVerdict verdict = check_rules(probe_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
