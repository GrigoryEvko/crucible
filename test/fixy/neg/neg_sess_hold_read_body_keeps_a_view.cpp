// Negative compile test.  This file must fail to compile.
//
// A hold lends a read view of a region that it borrows to the body of its
// read.  This body keeps a View of the words of the region in an Option that
// it captures by reference, so the View outlives the read, and the lender can
// write the words after the loan goes back.  PermHold::read of
// fixy/session/Flow.h refuses the body at its own call, because the body
// stores a borrow outside the call (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).
//
// Expected diagnostic: no read of the hold takes the body, and the note names
// StoresNoBorrowOutside of the body.

#include <fixy/Core.h>
#include <fixy/session/Flow.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_hold_read_body_keeps_a_view_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_hold_read_body_keeps_a_view_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_hold_read_body_keeps_a_view_types::Region;
    ::fixy::FixedArray<int, 4> words{};
    auto lender = s::mint_permission_hold(::foundation::permissions::mint_permission_root<Region>());
    auto lent = std::move(lender).template lend<Region>(0);
    auto taken = s::mint_permission_hold().accept_loan(std::move(lent.at(::fixy::index<0>())));
    ::fixy::Option<::fixy::View<int const, 4>> kept{};
    auto back = std::move(taken.at(::fixy::index<1>())).template read<Region>([&kept, &words](auto const&) noexcept {
        kept = ::fixy::some(words.view());
    });
    static_cast<void>(back);
    return kept.is_some() ? 1 : 0;
}
