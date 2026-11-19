// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An atomic cell hands values of its payload from one thread to another, so
// it is Sendable only when its payload is (payload_cell{} of
// foundation/Transfer.h).  A WeakRef of a timing sink of one owner thread is
// not, so run_joined refuses a body that reaches the sink through such a cell.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/TimingSink.h>

int main() {
    using Sink = ::fixy::observe::TimingSink<2>;
    Sink sink = ::fixy::observe::mint_timing_sink<2>();
    ::fixy::Atomic<::fixy::WeakRef<Sink>> cell{::fixy::WeakRef<Sink>{sink}};
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&cell]() noexcept { cell.load_acquire()->mark(::fixy::index<0>()); };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body);
    return 0;
}
