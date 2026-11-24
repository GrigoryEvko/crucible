// A Ranged<0, N> holds the value N, which no Index<N> holds, so it does not
// convert to an Index<N>.  A Ranged<0, N - 1> converts with no check.

#include <foundation/core/Index.h>

int main() {
    ::foundation::core::Index<8> const slot = ::foundation::core::Ranged<0, 8>::constant<3>();
    return static_cast<int>(slot.raw());
}
