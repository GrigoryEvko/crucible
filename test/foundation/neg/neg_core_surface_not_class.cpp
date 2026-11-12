// The check reads the members of a class, so a type that is not a class is
// refused, and no check passes for it with no members to read.

#include <foundation/core/Verbs.h>

static_assert(::foundation::core::closed_surface<int>(::foundation::core::TextView{"is_some"}).is_closed());

int main() { return 0; }
