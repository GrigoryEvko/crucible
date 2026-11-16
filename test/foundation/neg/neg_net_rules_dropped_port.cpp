// A right side keeps each free port of its pair.  The rule of probe and num
// gives the output of probe no wire, so the step would lose the agent at that
// port.

#include <foundation/algebra/net/Rules.h>

namespace {

using foundation::algebra::net::give;
using foundation::algebra::net::KindId;
using foundation::algebra::net::RightSide;
using foundation::algebra::net::Rule;
using foundation::algebra::net::RuleTable;
using foundation::algebra::net::RuleVerdict;
using foundation::algebra::net::take;
using foundation::algebra::net::TypeId;

inline constexpr RuleTable<1, 2, 1> probe_table{
    .signature =
        {.type_names = {"Value"},
         .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{0})}},
                   {.name = "probe", .port_names = {"in", "out"}, .port_types = {take(TypeId{0}), give(TypeId{0})}}}},
    .rules = {Rule{"probe_drops_out", KindId{1}, KindId{0}, RightSide{}}}};

inline constexpr RuleVerdict verdict = check_rules(probe_table);
static_assert(verdict.is_admitted(), verdict);

}  // namespace

int main() { return 0; }
