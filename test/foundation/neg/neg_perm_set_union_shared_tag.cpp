// A union of two permission sets that share a tag.  One CSL authority
// cannot have two holders, so perm_set_union refuses the overlap in its
// own class, before a handle holds the joined set.
//
// The alias names the result type and does not complete it.  The joined
// set lists the shared tag two times, so its completion would give a
// second error.

#include <foundation/permissions/PermSet.h>

namespace user_code {
struct Alpha {};
struct Beta {};
}  // namespace user_code

namespace perm = foundation::permissions;

using Overlap =
    perm::perm_set_union_t<perm::PermSet<user_code::Alpha, user_code::Beta>, perm::PermSet<user_code::Alpha>>;

int main() { return 0; }
