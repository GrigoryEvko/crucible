// foundation/core/Region.h declares get of a View, and foundation/core/Array.h
// defines it with the Ref family.  A unit that includes only Region.h cannot
// use get, because the compiler has not deduced its type.

#include <foundation/core/Region.h>

inline bool has_element(::foundation::core::View<int const> elements) { return elements.get(0).is_some(); }

int main() {}
