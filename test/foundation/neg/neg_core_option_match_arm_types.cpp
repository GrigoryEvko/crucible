// The two arms of a match give one result type.  Arms that give different
// types would need a conversion that the caller does not see, so the match
// refuses them.

#include <foundation/core/Choice.h>

int main() {
    long const result = ::foundation::core::Option<int>::some(3).match([](int value) noexcept { return value; },
                                                                       [] noexcept { return 0L; });
    return static_cast<int>(result);
}
