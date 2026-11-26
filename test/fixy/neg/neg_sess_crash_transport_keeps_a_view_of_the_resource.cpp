// Negative compile test.  This file must fail to compile.
//
// A crash session lends the Resource to its write for one call.  This write
// keeps a View of the words of the Resource in an Option that it captures by
// reference, so the View outlives the step.  The send of the crash session
// refuses the write, because the write stores a borrow outside the call
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).
//
// Expected diagnostic: no send of the crash handle takes the write, and the
// note names StoresNoBorrowOutside of the write.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>
#include <foundation/effects/Ctx.h>

#include <utility>

// These types have external linkage.  The session folds their stable ids,
// and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_transport_keeps_a_view_of_the_resource_types {
namespace s = ::fixy::session;
struct Alice {};
struct Bob {};
struct Wire {
    ::fixy::FixedArray<int, 4> words{};
};
using Proto = s::Send<int, s::End>;
}  // namespace neg_sess_crash_transport_keeps_a_view_of_the_resource_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_crash_transport_keeps_a_view_of_the_resource_types;
    s::CrashCell<Bob, Alice> cell;
    s::CrashCell<Alice, Bob> own;
    ::fixy::TestRunnerCtx const ctx{::foundation::effects::testing::test()};
    auto handle =
        s::mint_crash_session<Proto, Alice, Bob, s::ReliableSet<Bob>>(ctx, Wire{}, cell, s::mint_crash_writer(own));
    ::fixy::Option<::fixy::View<int const, 4>> kept{};
    auto sent = std::move(handle).send(1, [&kept](Wire& wire, int&) noexcept {
        Wire const& lent = wire;
        kept = ::fixy::some(lent.words.view());
        return true;
    });
    (void)std::move(sent.next).close();
    return kept.is_some() ? 1 : 0;
}
