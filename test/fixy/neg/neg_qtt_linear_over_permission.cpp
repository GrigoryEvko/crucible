// Linear<Permission<Tag>> wraps a token that is already a move-only
// exactly-once linearity token.  The token carries the annotation linear
// of foundation/Linear.h, and the wrapper reads that marker, so no table
// has to name the token.
//
// The second required diagnostic is the compiler's rendering of the
// permission specialization inside the Qtt instantiation, which the
// source does not spell.

#include <fixy/Qtt.h>
#include <foundation/permissions/Permission.h>

struct NegQttPermissionTag {
    using permission_row = ::foundation::effects::Row<>;
};

using LinearOverPermission = fixy::Linear<::foundation::permissions::Permission<NegQttPermissionTag>>;

static_assert(sizeof(LinearOverPermission) > 0);

int main() { return 0; }
