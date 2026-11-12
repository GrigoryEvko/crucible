// An Option compares only with an Option of its own payload type, so a
// compare with an Option of a different payload type is refused.

#include <foundation/core/Choice.h>

int main() {
    auto const narrow = ::foundation::core::Option<int>::some(3);
    auto const wide = ::foundation::core::Option<long>::some(3L);
    return narrow == wide ? 0 : 1;
}
