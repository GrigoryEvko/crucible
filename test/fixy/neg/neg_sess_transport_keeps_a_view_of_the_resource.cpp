// Negative compile test.  This file must fail to compile.
//
// A step lends the Resource to its transport for one call.  This write keeps
// a View of the words of the Resource in an Option that it captures by
// reference, so the View outlives the step.  WriteTransport of
// fixy/session/Transport.h refuses the write, because the write stores a
// borrow outside the call (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).
//
// Expected diagnostic: no send of the handle takes the write, and the note
// names StoresNoBorrowOutside of the write.

#include <fixy/Core.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Ctx.h>

#include <utility>

namespace neg_sess_transport_keeps_a_view_of_the_resource_types {
namespace s = ::fixy::session;
struct Wire {
    ::fixy::FixedArray<int, 4> words{};
};
using Proto = s::Loop<s::Send<int, s::Continue>>;
}  // namespace neg_sess_transport_keeps_a_view_of_the_resource_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_transport_keeps_a_view_of_the_resource_types::Proto;
    using neg_sess_transport_keeps_a_view_of_the_resource_types::Wire;
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Proto>(ctx, Wire{});
    ::fixy::Option<::fixy::View<int const, 4>> kept{};
    auto next = std::move(head).send(1, [&kept](Wire& wire, int&) noexcept {
        Wire const& lent = wire;
        kept = ::fixy::some(lent.words.view());
        return true;
    });
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    return kept.is_some() ? 1 : 0;
}
