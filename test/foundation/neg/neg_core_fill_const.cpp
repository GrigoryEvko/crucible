// fill() writes each element.  A View of const elements gives no write
// access, so the fill gate refuses it.

#include <foundation/core/Region.h>

inline void fill_read_only(::foundation::core::View<int const> target) { ::foundation::core::fill(target, 0); }

int main() {}
