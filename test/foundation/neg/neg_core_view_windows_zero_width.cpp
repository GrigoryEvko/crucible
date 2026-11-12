// A window of no element is no window, so the width 0 is refused at compile
// time.

#include <foundation/core/Region.h>

inline auto windows_of_nothing(::foundation::core::View<int> elements) { return elements.windows<0>(); }

int main() {}
