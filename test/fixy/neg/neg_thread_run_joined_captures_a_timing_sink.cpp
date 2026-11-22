// Negative compile test.  This file must fail to compile.
//
// A timing sink belongs to one owner thread (thread_bound{} of
// foundation/Transfer.h), so run_joined refuses a body that writes it through
// a reference from another thread.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/TimingSink.h>

int main() {
    auto sink = ::fixy::observe::mint_timing_sink<2>();
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto body = [&sink]() noexcept { sink.mark(::fixy::index<0>()); };
    [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body));
    return 0;
}
