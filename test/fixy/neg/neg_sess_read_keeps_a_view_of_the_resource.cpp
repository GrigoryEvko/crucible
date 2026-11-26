// Negative compile test.  This file must fail to compile.
//
// A step lends the Resource to its read for one call.  This read of the word
// of a choice keeps a View of the words of the Resource in an Option that it
// captures by reference, so the View outlives the step.  ReadTransport of
// fixy/session/Transport.h refuses the read, because the read stores a
// borrow outside the call (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).
//
// Expected diagnostic: no branch of the handle takes the read, and the note
// names StoresNoBorrowOutside of the read.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <cstddef>
#include <utility>

namespace neg_sess_read_keeps_a_view_of_the_resource_types {
namespace s = ::fixy::session;
struct Wire {
    ::fixy::FixedArray<int, 4> words{};
};
using Choice = s::Offer<s::End, s::End>;
}  // namespace neg_sess_read_keeps_a_view_of_the_resource_types

int main() {
    namespace s = ::fixy::session;
    using neg_sess_read_keeps_a_view_of_the_resource_types::Choice;
    using neg_sess_read_keeps_a_view_of_the_resource_types::Wire;
    ::fixy::TestRunnerCtx const ctx{::foundation::effects::testing::test()};
    auto head = s::mint_session<Choice>(ctx, Wire{});
    ::fixy::Option<::fixy::View<int const, 4>> kept{};
    std::move(head).branch(
        [&kept](Wire& wire) noexcept -> ::fixy::Option<std::size_t> {
            Wire const& lent = wire;
            kept = ::fixy::some(lent.words.view());
            return ::fixy::some(std::size_t{0});
        },
        [](auto at_end) noexcept { static_cast<void>(std::move(at_end).close()); });
    return kept.is_some() ? 1 : 0;
}
