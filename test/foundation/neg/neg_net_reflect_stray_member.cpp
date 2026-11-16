// Each data member of an agent kind is a port.  The member count of num has
// the type int, so the adapter cannot read it as a port, and check_rules_of
// refuses the namespace and names the member and the agent.

#include <foundation/algebra/net/Reflect.h>

namespace stray_member {

struct Value {};

struct[[= foundation::algebra::net::agent{}]] num {
    foundation::algebra::net::Give<Value> value;
    int count;
};

}  // namespace stray_member

namespace {

inline constexpr foundation::algebra::net::RuleVerdict verdict =
    foundation::algebra::net::check_rules_of<^^stray_member>();
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
