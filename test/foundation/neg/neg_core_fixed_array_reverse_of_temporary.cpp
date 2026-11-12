// A reverse loop over a temporary FixedArray would read an object that the
// full expression ends.

#include <foundation/core/Array.h>

inline auto dangling_reverse() { return ::foundation::core::FixedArray<int, 4>{}.reverse(); }

int main() {}
