// A View of four elements holds no window of five, and its extent proves it
// at compile time.

#include <foundation/core/Region.h>

inline auto windows_past(::foundation::core::View<int, 4> elements) { return elements.windows<5>(); }

int main() {}
