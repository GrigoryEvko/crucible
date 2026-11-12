// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// is_global_well_formed is an alias template over the concept that the
// projection gate reads, so no user specialization makes a global type
// with an unbound variable well-formed.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Global.h>

#include <type_traits>

namespace fixy::session::global {
template <>
struct is_global_well_formed<Var> : std::true_type {};
}  // namespace fixy::session::global

int main() { return 0; }
