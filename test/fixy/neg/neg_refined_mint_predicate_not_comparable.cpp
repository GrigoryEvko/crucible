// The checked mint refuses a payload that the predicate's expression
// cannot evaluate.  Each predicate class states that expression as a
// constraint on its call operator, so the refusal names the
// invocability gate at the call site, not an error inside the body.

#include <fixy/Refined.h>

namespace refined_not_comparable {
struct Uncomparable {
    int value = 0;
};
}  // namespace refined_not_comparable

int main() {
    auto refused = fixy::mint_refined<fixy::positive, refined_not_comparable::Uncomparable>(
        refined_not_comparable::Uncomparable{1});
    (void)refused;
    return 0;
}
