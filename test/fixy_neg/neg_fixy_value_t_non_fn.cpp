// ── neg_fixy_value_t_non_fn (FIXY-G2 HS14) ────────────────────────────
//
// value_t<F> requires IsFixyFn<F>; non-fn F must trigger the concept
// gate diagnostic.

#include <crucible/fixy/Fixy.h>

namespace {

using x = ::crucible::fixy::value_t<int>;

}  // namespace

int main() { return 0; }
