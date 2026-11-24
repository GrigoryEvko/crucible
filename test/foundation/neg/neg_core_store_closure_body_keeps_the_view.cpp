// The control of neg_core_store_const_closure_keeps_the_view: the same
// closure, which captures a variable of the caller by a reference that is not
// const and keeps the View of the frame in it, is itself the body of the
// frame door with_view of a GrowList.  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the body.

#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Region.h>

int main() {
    auto list = ::foundation::core::mint_grow_list<int>(64);
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto const keeper = [&kept](::foundation::core::View<int const> elements) noexcept {
        kept = ::foundation::core::some(elements);
        return elements.size();
    };
    auto const count = list.with_view(keeper);
    return count == 0 && kept.is_some() ? 0 : 1;
}
