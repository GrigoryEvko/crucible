// A role for a crypto path asks IsConstantTime whether its Security grade
// closes the timing channel.  This file tries to make as_secret read as
// constant time: it writes an explicit specialization of IsConstantTime,
// as it would for a variable template.  IsConstantTime is a concept, and
// the template-id of a concept declares nothing.

#include <fixy/Atom.h>

template <>
inline constexpr bool fixy::atom::IsConstantTime<fixy::atom::as_secret> = true;

int main() { return 0; }
