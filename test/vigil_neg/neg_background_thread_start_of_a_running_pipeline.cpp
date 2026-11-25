// Negative-compile test.  This file must fail to compile.
//
// Two pipelines of one thread would write each stage from two threads.  A start
// takes the Stopped handle by move and gives a Running handle, which has no
// start.

#include <crucible/BackgroundThread.h>

#include <fixy/Ctx.h>

#include <utility>

namespace {

struct Scope final {};

// The handle and the context are parameters, so the fixture starts no thread.
[[maybe_unused]] void start_two_times(crucible::BackgroundThread::Stopped<Scope> stopped,
                                      ::fixy::InitLoadCtx const& startup) {
    auto running = std::move(stopped).start(startup);
    static_cast<void>(std::move(running).start(startup));
}

}  // namespace

int main() { return 0; }
