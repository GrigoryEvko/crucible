// An Index<N> proves a number below N, and no number is below 0.  So the
// count 0 is refused at compile time.

#include <foundation/core/Index.h>

inline auto check_of_nothing() { return ::foundation::core::Index<0>::check(0); }

int main() {}
