// Affine<Permission<Tag>> is the unsound half of the pair: the token
// carries an exactly-once obligation, and the Affine wrapper makes the
// discharge optional.  The token carries the annotation linear of
// foundation/Linear.h, and the wrapper reads that marker.
//
// Its sibling neg_qtt_linear_over_permission.cpp holds the other
// recogniser over the same token.
//
// The second required diagnostic is the compiler's rendering of the
// permission specialization inside the Qtt instantiation, which the
// source does not spell.

#include <fixy/Qtt.h>
#include <foundation/permissions/Permission.h>

struct NegQttAffineTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct NegQttAffineBrand {};

using AffineOverPermission = fixy::Affine<::foundation::permissions::Permission<NegQttAffineTag, NegQttAffineBrand>>;

static_assert(sizeof(AffineOverPermission) > 0);

int main() { return 0; }
