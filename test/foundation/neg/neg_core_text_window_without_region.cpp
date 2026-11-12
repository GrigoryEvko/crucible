// foundation/core/Text.h declares the reads of a TextView, and
// foundation/core/Region.h defines them.  A unit that includes only Text.h
// cannot use a window, because the compiler has not deduced its type.

#include <foundation/core/Text.h>

inline bool has_window(::foundation::core::TextView text) { return text.window(0, 1).is_some(); }

int main() {}
