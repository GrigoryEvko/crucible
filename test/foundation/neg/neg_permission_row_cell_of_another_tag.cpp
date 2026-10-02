// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The row of a tag is the walk that the variable template row_cell_at
// keeps, and a translation unit can specialize a variable template.  The
// constructors of a cell are private, but the walk itself is a function
// that any caller can name.  Here the cell of an IO tag is the walk of a
// pure tag, which would give the IO tag the empty row and let a mint with
// no context own an IO region.  The cell carries the tag that it walked,
// and the read compares it with its own, so the forged row never reaches
// the question.
//
// Expected diagnostic: the read calls the function that names a cell of
// another tag.

#include <foundation/permissions/Permission.h>

#include <meta>

namespace {

namespace fp = ::foundation::permissions;

struct IoTag {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

struct PureTag {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

template <>
inline constexpr ::foundation::permissions::detail::row_cell foundation::permissions::detail::row_cell_at<
    ^^IoTag,
    std::meta::members_of(^^::foundation::permissions::permission_rows, std::meta::access_context::unprivileged())
        .size()> = ::foundation::permissions::detail::walk_row(^^PureTag,
                                                               std::meta::members_of(
                                                                   ^^::foundation::permissions::permission_rows,
                                                                   std::meta::access_context::unprivileged())
                                                                   .size());

namespace {

[[maybe_unused]] constexpr bool answer = fp::permission_row_empty(^^IoTag);

}  // namespace

int main() { return 0; }
