// A rule that a namespace states by its annotation obeys each check of a
// rule table.  The rule of relay and num gives the value as a join of two
// copies, so it makes three agents from two with no annotation grows, and
// the table stops the build.

#include <foundation/algebra/net/Reflect.h>

namespace relays {

using foundation::algebra::net::Give;
using foundation::algebra::net::point_at_agent;
using foundation::algebra::net::point_at_interface;
using foundation::algebra::net::ReflectedSide;
using foundation::algebra::net::RuleWire;
using foundation::algebra::net::Take;

struct Value {};

struct[[= foundation::algebra::net::agent{}]] num {
    Give<Value> value;
};
struct[[= foundation::algebra::net::agent{}]] relay {
    Take<Value> in;
    Give<Value> out;
};
struct[[= foundation::algebra::net::agent{}]] join {
    Give<Value> out;
    Take<Value> left;
    Take<Value> right;
};

[[= foundation::algebra::net::rule<relay, num>{}]] consteval ReflectedSide relay_grows() {
    return ReflectedSide{.agents = {^^join, ^^num, ^^num},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                   RuleWire{point_at_agent(1, 0), point_at_agent(0, 1)},
                                   RuleWire{point_at_agent(2, 0), point_at_agent(0, 2)}},
                         .agent_count = 3,
                         .wire_count = 3};
}

[[= foundation::algebra::net::rule<relay, join>{}]] consteval ReflectedSide relay_meets_join() {
    return ReflectedSide{.agents = {^^join},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                   RuleWire{point_at_agent(0, 1), point_at_interface(1)},
                                   RuleWire{point_at_agent(0, 2), point_at_interface(2)}},
                         .agent_count = 1,
                         .wire_count = 3};
}

}  // namespace relays

namespace {

inline constexpr foundation::algebra::net::RuleVerdict verdict = foundation::algebra::net::check_rules_of<^^relays>();
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
