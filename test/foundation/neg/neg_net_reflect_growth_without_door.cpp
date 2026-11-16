// A rule that a namespace states by its annotation obeys each check of a
// rule table.  The rule of relay and num makes three agents from two with no
// annotation grows, so the table stops the build.

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

[[= foundation::algebra::net::rule<relay, num>{}]] consteval ReflectedSide relay_grows() {
    return ReflectedSide{.agents = {^^num, ^^relay, ^^relay},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_agent(1, 0)},
                                   RuleWire{point_at_agent(1, 1), point_at_agent(2, 0)},
                                   RuleWire{point_at_agent(2, 1), point_at_interface(0)}},
                         .agent_count = 3,
                         .wire_count = 3};
}

}  // namespace relays

namespace {

inline constexpr foundation::algebra::net::RuleVerdict verdict = foundation::algebra::net::check_rules_of<^^relays>();
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
