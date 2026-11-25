// A function of a walk of the segments keeps the View of a segment in a
// variable that it captures by reference.  Each function of a walk stores no
// borrow outside the walk, so the walk refuses it.  The loop over the segments
// gives the View to its body in place.

#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>

#include <cstddef>

int main() {
    ::foundation::core::SegmentList<int> const list;
    ::foundation::core::View<int const> kept{};
    bool const has_run = list.segments().any_of([&kept](::foundation::core::View<int const> const& run) noexcept {
        kept = run;
        return true;
    });
    return has_run ? static_cast<int>(kept.size()) : 0;
}
