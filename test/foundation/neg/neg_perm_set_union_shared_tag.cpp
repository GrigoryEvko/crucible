// A union of two permission sets that share a tag.  One CSL authority
// cannot have two holders, so the union refuses the overlap before it
// builds the joined set, and no handle holds that set.

#include <foundation/permissions/PermSet.h>

namespace user_code {
struct Alpha {};
struct Beta {};
}  // namespace user_code

namespace perm = foundation::permissions;

using Overlap =
    perm::perm_set_union_t<perm::PermSet<user_code::Alpha, user_code::Beta>, perm::PermSet<user_code::Alpha>>;

int main() { return 0; }
