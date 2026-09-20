// Linear<Permission<Tag>> wraps a token that is already a move-only
// exactly-once linearity token.  The old tree refused this through two
// table entries; the port dropped them, and this fixture is what keeps
// the refusal from depending on anyone remembering to write an entry.
//
// The permission templates are named in one concept body in
// fixy/Qtt.h, declared there and instantiated nowhere, so this fixture
// reaches the refusal without foundation/permissions/Permission.h
// becoming a prerequisite of the wrapper.
//
// The second required diagnostic is the compiler's rendering of the
// permission specialization inside the Qtt instantiation, which the
// source does not spell.

#include <fixy/Qtt.h>

struct NegQttPermissionTag;

using LinearOverPermission = fixy::Linear<::foundation::permissions::Permission<NegQttPermissionTag>>;

static_assert(sizeof(LinearOverPermission) > 0);

int main() { return 0; }
