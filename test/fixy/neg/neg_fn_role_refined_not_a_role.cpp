// A role names an fn.  An alias that names the payload itself is no role,
// so the role mint refuses it, also for a Refined argument whose predicate
// the alias would match.

#include <fixy/Fn.h>
#include <fixy/Predicates.h>
#include <fixy/Refined.h>

namespace {
template <class T>
using bare_payload = T;
}  // namespace

int main() {
    auto const proof = ::fixy::mint_refined<::fixy::positive>(3);
    auto const binding = ::fixy::mint_fn_for<bare_payload>(proof);
    return binding.value() == 3 ? 0 : 1;
}
