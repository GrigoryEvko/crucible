// The cost of the watch of fixy/session/Watch.h on a declared read and a
// declared write.  Under check::Off the handle keeps no record, so the
// scope that it opens for a declared call has no endpoint: the scope counts
// no trace point, and its poll only pauses.  Under check::Enforced the scope
// is tracked, and it counts its first trace point after 1024 polls.  The
// check file of Watch.h holds the size of the scope, so a scope holds no
// wait-for chain to clear.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Watch.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace s = ::fixy::session;

struct Wire {
    int value = 0;
};

// The poll count of one trace point of a scope.
constexpr std::uint32_t kPollsToTracePoint = s::watch::polls_per_trace_point;

// What a declared call saw of the scope that the handle gave it: the count
// of its trace points at the entry and after one trace point of polls.
struct Seen {
    std::uint64_t at_entry = 0;
    std::uint64_t after_polls = 0;
};

// What the declared write and the declared read of one session saw.
struct SeenByCalls {
    Seen write{};
    Seen read{};
};

// Polls the scope to its first trace point, and records the count of its
// trace points before and after.
void note_scope(s::watch::wait_scope& wait, Seen& seen) noexcept {
    seen.at_entry = wait.trace_points();
    for (std::uint32_t poll = 0; poll < kPollsToTracePoint; ++poll)
        wait.poll();
    seen.after_polls = wait.trace_points();
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
    auto [received, at_end] = std::move(reads).recv([&seen, &echoed](Wire& wire, s::watch::wait_scope& wait) noexcept {
        note_scope(wait, seen.read);
        echoed = wire.value;
        return wire.value;
    });
    static_cast<void>(received);
    static_cast<void>(std::move(at_end).close());
    CRUCIBLE_TEST_ASSERT(kTest, echoed == 7);
    return seen;
}

void off_counts_no_trace_point() {
    const SeenByCalls seen = run_declared_calls<s::check::Off>();
    CRUCIBLE_TEST_ASSERT(kTest, seen.write.at_entry == 0);
    CRUCIBLE_TEST_ASSERT(kTest, seen.write.after_polls == 0);
    CRUCIBLE_TEST_ASSERT(kTest, seen.read.at_entry == 0);
    CRUCIBLE_TEST_ASSERT(kTest, seen.read.after_polls == 0);
}

// The positive control: the same calls under check::Enforced count the
// first trace point, and not before it.
void enforced_counts_the_trace_point() {
    const SeenByCalls seen = run_declared_calls<s::check::Enforced>();
    CRUCIBLE_TEST_ASSERT(kTest, seen.write.at_entry == 0);
    CRUCIBLE_TEST_ASSERT(kTest, seen.read.at_entry == 0);
    CRUCIBLE_TEST_ASSERT(kTest, seen.write.after_polls == 1);
    CRUCIBLE_TEST_ASSERT(kTest, seen.read.after_polls == 1);
}

}  // namespace

int main() {
    off_counts_no_trace_point();
    enforced_counts_the_trace_point();
    ::fixy::check::pass(kTest, "test_session_watch_cost: OK\n");
    return 0;
}
