// A Side has no comparison: the role holds no order and no equality of its
// own, so a compare of two Sides does not compile.

#include <foundation/core/Side.h>

struct Count final {};

inline bool is_same_count(::foundation::core::Side<Count, int> const& first,
                          ::foundation::core::Side<Count, int> const& second) {
    return first == second;
}

int main() {}
