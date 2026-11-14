// The cost of the watch of fixy/session/Watch.h on a declared read and a
// declared write.  Under check::Off the handle keeps no record, so the
// scope that it opens for a declared call has no endpoint: the scope reads
// no clock, and its poll only pauses.  Under check::Enforced the scope is
// tracked, and it reads the clock first at its first trace point, after
// 1024 polls.  The check file of Watch.h holds the size of the scope, so a
// scope holds no wait-for chain to clear.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Watch.h>

#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <utility>
#include "../test_assert.h"

namespace {

namespace s = ::fixy::session;

struct Wire {
    int value = 0;
};

// The poll count of one trace point of a scope.
constexpr int kPollsToTracePoint = 1024;

// What a declared call saw of the scope that the handle gave it.
struct Seen {
    std::uint64_t at_entry_ns = 0;
    std::uint64_t after_polls_ns = 0;
};

// What the declared write and the declared read of one session saw.
struct SeenByCalls {
    Seen write{};
    Seen read{};
};

// Polls the scope to its first trace point, and records what the scope
// says of its clock before and after.
void note_scope(s::watch::wait_scope& wait, Seen& seen) noexcept {
    seen.at_entry_ns = wait.started_at_ns();
    for (int poll = 0; poll < kPollsToTracePoint; ++poll)
        wait.poll();
    seen.after_polls_ns = wait.started_at_ns();
}

template <typename Policy>
[[nodiscard]] SeenByCalls run_declared_calls() {
    using Echo = s::Send<int, s::Recv<int, s::End>>;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    SeenByCalls seen{};
    int echoed = 0;
    auto head = s::mint_session<Echo, Policy>(ctx, Wire{});
    auto reads = std::move(head).send(7, [&seen](Wire& wire, int&& value, s::watch::wait_scope& wait) noexcept {
        note_scope(wait, seen.write);
        wire.value = value;
    });
    auto at_end = std::move(std::move(reads)
                                .recv([&seen, &echoed](Wire& wire, s::watch::wait_scope& wait) noexcept {
                                    note_scope(wait, seen.read);
                                    echoed = wire.value;
                                    return wire.value;
                                })
                                .second);
    static_cast<void>(std::move(at_end).close());
    assert(echoed == 7);
    return seen;
}

void off_reads_no_clock() {
    const SeenByCalls seen = run_declared_calls<s::check::Off>();
    assert(seen.write.at_entry_ns == 0);
    assert(seen.write.after_polls_ns == 0);
    assert(seen.read.at_entry_ns == 0);
    assert(seen.read.after_polls_ns == 0);
}

// The positive control: the same calls under check::Enforced read the
// clock at the first trace point, and not before it.
void enforced_reads_the_clock_at_the_trace_point() {
    const SeenByCalls seen = run_declared_calls<s::check::Enforced>();
    assert(seen.write.at_entry_ns == 0);
    assert(seen.read.at_entry_ns == 0);
    assert(seen.write.after_polls_ns != 0);
    assert(seen.read.after_polls_ns != 0);
}

}  // namespace

int main() {
    off_reads_no_clock();
    enforced_reads_the_clock_at_the_trace_point();
    crucible::test::pass("test_session_watch_cost: OK\n");
    return 0;
}
