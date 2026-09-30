// fixy/Corpus.h asks IsClassifiedCarrier whether a Security grade holds
// classified data.  This file tries to make as_secret read as no carrier:
// it writes an explicit specialization of IsClassifiedCarrier, as it
// would for a variable template.  IsClassifiedCarrier is a concept, and
// the template-id of a concept declares nothing.

#include <fixy/Atom.h>

template <>
inline constexpr bool fixy::atom::IsClassifiedCarrier<fixy::atom::as_secret> = false;

int main() { return 0; }
