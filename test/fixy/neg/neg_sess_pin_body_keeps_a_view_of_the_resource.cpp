// Negative compile test.  This file must fail to compile.
//
// A pin lends the pinned handle to its body, and the release moves the
// handle on.  This body keeps a View of the words of the Resource of the
// pinned handle in an Option that it captures by reference, so the View
// outlives the pin.  HandleFactory::pin of fixy/session/Handle.h refuses the
// body, because the body stores a borrow outside the call
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).
//
// Expected diagnostic: no pin takes the body, and the note names
// StoresNoBorrowOutside of the body.

#include <fixy/Core.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_pin_body_keeps_a_view_of_the_resource_types {
namespace s = ::fixy::session;
struct Wire {
    ::fixy::FixedArray<int, 4> words{};
};
using Proto = s::Loop<s::Send<int, s::Continue>>;
}  // namespace neg_sess_pin_body_keeps_a_view_of_the_resource_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_pin_body_keeps_a_view_of_the_resource_types::Proto;
    using neg_sess_pin_body_keeps_a_view_of_the_resource_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Proto>(ctx, Wire{});
    ::fixy::Option<::fixy::View<int const, 4>> kept{};
    auto back = s::HandleFactory::pin(ctx, std::move(head), [&kept](auto pinned) noexcept {
        Wire const& lent = pinned.resource();
        kept = ::fixy::some(lent.words.view());
        return pinned;
    });
    std::move(back).detach(s::detach_reason::TestInstrumentation{});
    return kept.is_some() ? 1 : 0;
}
