// to() gives the fresh brand of the call.  A caller that names a brand would
// claim it for an object that it does not hold, such as a stack object with
// the brand of an arena, so to() refuses each explicit template argument.

#include <foundation/core/Ref.h>

struct ArenaBrand {};

int main() {
    int number = 1;
    auto const claimed = ::foundation::core::to<int, ArenaBrand>(number);
    return *claimed;
}
