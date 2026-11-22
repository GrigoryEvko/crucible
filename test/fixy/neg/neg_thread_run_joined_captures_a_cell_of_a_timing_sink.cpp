// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An atomic cell hands values of its payload from one thread to another, so
// it is Sendable only when its payload is (payload_cell{} of
// foundation/Transfer.h).  An Option of a Ref of a timing sink of one owner
// thread is not, so run_joined refuses a body that reaches the sink through
// such a cell.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/TimingSink.h>

namespace {
// The brand of the borrow of the sink.
struct SinkBrand {};
}  // namespace

int main() {
    using Sink = ::fixy::observe::TimingSink<2>;
    using SinkLink = ::fixy::Option<::fixy::Ref<Sink, SinkBrand>>;
    [[maybe_unused]] Sink sink = ::fixy::observe::mint_timing_sink<2>();
    ::fixy::Atomic<SinkLink> cell{SinkLink{}};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&cell]() noexcept {
        for (auto const& reached : cell.load_acquire())
            reached->mark(::fixy::index<0>());
    };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
