// CRUCIBLE_DIAG_ASSERT prints the tag in its message, so the tag must name
// a tag of the catalog.  A class that derives from tag_base and is not in
// the closed set of Category is no catalog tag, and the macro refuses it
// also when the condition holds.

#include <foundation/Quarantine.h>
#include <foundation/diag/Tag.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::diag {
struct LooksLikeACatalogTag : tag_base {};
}  // namespace foundation::diag
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_DIAG_ASSERT(true, LooksLikeACatalogTag, "the condition holds, and the tag is outside the catalog");

int main() { return 0; }
