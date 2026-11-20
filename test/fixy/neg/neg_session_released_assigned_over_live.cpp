// An assignment over a live Released would write over its read loan, and the
// move assignment of a loan does nothing.  So the loan of the target would
// leave through no door, and its lender would wait for it with no end.  The
// assignment is refused.

#include <fixy/session/Flow.h>
#include <foundation/effects/Row.h>

namespace released_assignment {

struct Pages {
    using permission_row = ::foundation::effects::Row<>;
};

struct PagesBrand {};

using Release = ::fixy::session::Released<int, Pages, PagesBrand>;

void assign_over(Release& target, Release&& source) noexcept;

void assign_over(Release& target, Release&& source) noexcept { target = static_cast<Release&&>(source); }

}  // namespace released_assignment

int main() { return 0; }
