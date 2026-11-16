// A Side is no aggregate, and its constructor is explicit, so a braced list
// with no type does not make one.

#include <foundation/core/Side.h>

struct Count final {};

inline ::foundation::core::Side<Count, int> make_count() { return {3}; }

int main() {}
