// CRUCIBLE_POST on a field of a returned struct fires during constant
// evaluation.  The trap under `if consteval` is not a constant
// expression, so a violated predicate stops the static_assert below
// from evaluating.

#include <foundation/contracts/Post.h>

namespace {

struct R {
    int v = 0;
};

[[nodiscard]] constexpr R compute_positive_field(int const x) noexcept {
    R r{x - 1};
    CRUCIBLE_POST(r, r.v > 0);
    return r;
}

// x == 1 gives r.v == 0, which violates the postcondition.
static_assert(compute_positive_field(1).v == 0);

}  // namespace

int main() { return 0; }
