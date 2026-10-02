// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The row of a tag is the walk that the variable template row_cell_at
// keeps, and a translation unit can specialize a variable template.  Here
// the cell of an IO tag is a copy of the cell of a pure tag, which would
// give the IO tag the empty row.  The copy constructor of a cell is
// private, and the access check applies to the initializer of an explicit
// specialization, so the copy does not compile.
//
// Expected diagnostic: the copy constructor of the cell is private.

#include <foundation/permissions/Permission.h>

#include <meta>

namespace {

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
        .size()> =
    ::foundation::permissions::detail::row_cell_at<^^PureTag,
                                                   std::meta::members_of(^^::foundation::permissions::permission_rows,
                                                                         std::meta::access_context::unprivileged())
                                                       .size()>;

int main() { return 0; }
