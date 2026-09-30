// A role for a crypto path asks whether its Security grade closes the
// timing channel.  This file tries to make as_secret read as constant
// time.  It specializes the variable template that the answer once was.
// The answer is a concept over one reflection query, so the
// specialization has nothing to name.

#include <fixy/Atom.h>

template <>
inline constexpr bool fixy::atom::is_constant_time_v<fixy::atom::as_secret> = true;

int main() { return 0; }
