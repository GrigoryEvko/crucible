// A match is total: it takes one arm for the payload and one arm for the
// empty Option.  A call with only the payload arm would forget the empty
// case, so it does not compile.

#include <foundation/core/Choice.h>

int main() {
    return ::foundation::core::Option<int>::some(3).match([](int value) noexcept { return value; });
}
