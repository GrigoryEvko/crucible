// fixy/Corpus.h asks whether a Security grade is a classified carrier.
// This file tries to make as_secret read as no carrier.  It specializes
// the variable template that the answer once was.  The answer is a
// concept over one reflection query, so the specialization has nothing
// to name.

#include <fixy/Atom.h>

template <>
inline constexpr bool fixy::atom::is_classified_carrier_v<fixy::atom::as_secret> = false;

int main() { return 0; }
