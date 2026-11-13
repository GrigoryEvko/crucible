// fixy/Corpus.h asks IsClassifiedCarrier whether a Security grade holds
// classified data.  This file tries to make as_secret read as no carrier:
// it writes an explicit specialization of IsClassifiedCarrier, as it
// would for a variable template.  IsClassifiedCarrier is a concept, and
// the template-id of a concept declares nothing.

#include <fixy/Atom.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
template <>
inline constexpr bool fixy::atom::IsClassifiedCarrier<fixy::atom::as_secret> = false;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
