// A payload does not convert to a Side, so a call with the plain value does
// not compile, and the call site must state the role.

#include <foundation/core/Side.h>

struct Count final {};

inline int count_of(::foundation::core::Side<Count, int> count) { return count.value(); }

int main() { return count_of(3); }
