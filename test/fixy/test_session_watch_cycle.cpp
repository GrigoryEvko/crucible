// The deadlock detector of fixy/session/Watch.h on cycles of waits that the
// test writes into the tables of the watch.  The test thread waits on one
// endpoint.  A thread slot that no thread runs holds the peer of that
// endpoint and waits on a second channel, and the test thread holds the
// other end of that channel.  So each trace of the wait finds a cycle of two
// links.  The four endpoints are four sessions that the test thread opens
// and links in two channels, and then partly hands to the idle slot.
//
// A digest of a cycle only filters, because two different cycles can share
// one.  For a fixed state of the digest, the mix of one word is a bijection.
// So for each generation of the idle slot, one wait epoch of the slot gives
// the digest of the first cycle: a family of different cycles of one digest.
// The test installs a new member of the family before each trace point.  No
// two traces then see one cycle, and the wait must never report a deadlock.
// A wait that trusts the digest alone reports one at its second trace.
//
// The positive control: the same cycle at two traces of one wait is a
// deadlock, and the wait aborts in a child process.  A wait inside that wait
// traces the cycle first, and the outer wait does not take that trace for
// its own.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Watch.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/Mix.h>

#include <cstdint>
#include <utility>

#include "../padding_bytes.h"

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

namespace s = ::fixy::session;
namespace w = ::fixy::session::watch;
namespace wd = ::fixy::session::watch::detail;

// The poll count of one trace point of a wait.
constexpr int kPollsToTracePoint = 1024;

// The bound of the trace points of one case.  A wait traces first at
// suspect_after_trace_points, and then once for each
// confirm_after_trace_points, so a case that passes needs some thousands.
constexpr std::uint32_t kMaxTracePoints = 1u << 20;

// The one-based thread slot that no thread runs.
constexpr std::uint32_t kIdleSlot = w::thread_capacity;

// The generation of the idle slot in the first cycle of the family.
constexpr std::uint32_t kFirstGeneration = 7;

// The constants of the step of the digest (combine_ids of
// foundation/reflect/Mix.h): the two factors of the finalizer of MurmurHash3,
// and the constant it adds.
constexpr std::uint64_t kFirstFactor = 0xFF51AFD7ED558CCDULL;
constexpr std::uint64_t kSecondFactor = 0xC4CEB9FE1A85EC53ULL;
constexpr std::uint64_t kAddend = 0x9E3779B97F4A7C15ULL;

// The multiplicative inverse of an odd word modulo 2^64.  Each Newton step
// doubles the count of the correct low bits, from 3 to 96.
[[nodiscard]] constexpr std::uint64_t inverse_of(std::uint64_t odd) noexcept {
    std::uint64_t inverse = odd;
    for (int step = 0; step < 5; ++step)
        inverse *= 2 - odd * inverse;
    return inverse;
}

static_assert(kFirstFactor * inverse_of(kFirstFactor) == 1);
static_assert(kSecondFactor * inverse_of(kSecondFactor) == 1);

// The inverse of the finalizer.  A shift of 33 bits with an exclusive or is
// its own inverse.
[[nodiscard]] constexpr std::uint64_t unfinalize(std::uint64_t value) noexcept {
    value ^= value >> 33;
    value *= inverse_of(kSecondFactor);
    value ^= value >> 33;
    value *= inverse_of(kFirstFactor);
    value ^= value >> 33;
    return value;
}

// The word that the mix takes from `state` to `target`.
[[nodiscard]] constexpr std::uint64_t word_between(std::uint64_t state, std::uint64_t target) noexcept {
    return (unfinalize(target) ^ state) - kAddend - (state << 6) - (state >> 2);
}

static_assert(::foundation::reflect::combine_ids(12345, word_between(12345, 678)) == 678,
              "the test inverts combine_ids, the step of the digest of Watch.h");

// The four endpoints of the cycle.  The test thread waits on `waited` and
// holds `held`.  The idle slot holds `waited_peer` and waits on `idle_waits`.
struct Cycle {
    w::endpoint_id waited = w::endpoint_id::none;
    w::endpoint_id waited_peer = w::endpoint_id::none;
    w::endpoint_id idle_waits = w::endpoint_id::none;
    w::endpoint_id held = w::endpoint_id::none;
};

// The generation and the wait epoch of the idle slot in one cycle.
struct Member {
    std::uint32_t generation = 0;
    std::uint64_t wait_epoch = 0;
};

[[nodiscard]] std::uint32_t index_of(w::endpoint_id endpoint) noexcept { return static_cast<std::uint32_t>(endpoint); }

// The Resources of the four sessions.  The two ends of a channel share one
// priority.  The test thread waits on the channel of priority 1 and holds
// an end of the channel of priority 0, so the priority order admits its
// wait.
struct HighEnd {
    static constexpr w::priority session_priority{1};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct LowEnd {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Idle = s::Recv<int, s::End>;

// Opens four sessions for the four endpoints of the cycle and links them
// in two channels, runs `body` on the cycle, and detaches the four
// sessions.  The test thread opens each session, and then the idle slot
// takes `waited_peer` (install below) and `idle_waits` from it, and waits
// on `idle_waits`.
template <typename Body>
void with_cycle(Body body) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto waited_end = s::mint_session<Idle>(ctx, HighEnd{});
    auto waited_peer_end = s::mint_session<Idle>(ctx, HighEnd{});
    auto idle_end = s::mint_session<Idle>(ctx, LowEnd{});
    auto held_end = s::mint_session<Idle>(ctx, LowEnd{});
    const Cycle cycle{.waited = waited_end.watch_endpoint(),
                      .waited_peer = waited_peer_end.watch_endpoint(),
                      .idle_waits = idle_end.watch_endpoint(),
                      .held = held_end.watch_endpoint()};
    CRUCIBLE_TEST_ASSERT(kTest, w::is_live(cycle.waited) && w::is_live(cycle.held));
    w::link(cycle.waited, cycle.waited_peer);
    w::link(cycle.idle_waits, cycle.held);
    wd::thread_record& idle = wd::thread_at_(kIdleSlot);
    const bool is_reserved = idle.is_taken.cas_acq_rel(0, 1).is_ok();
    CRUCIBLE_TEST_ASSERT(kTest, is_reserved);
    idle.blocked_on.store_release(index_of(cycle.idle_waits));
    wd::record_at_(index_of(cycle.idle_waits)).owner.store_release(wd::pack(kIdleSlot, kFirstGeneration));
    body(cycle);
    idle.blocked_on.store_release(0);
    idle.is_taken.store_release(0);
    std::move(waited_end).detach(s::detach_reason::TestInstrumentation{});
    std::move(waited_peer_end).detach(s::detach_reason::TestInstrumentation{});
    std::move(idle_end).detach(s::detach_reason::TestInstrumentation{});
    std::move(held_end).detach(s::detach_reason::TestInstrumentation{});
}

// Gives the idle slot the generation and the wait epoch of `member`, and
// names the idle slot the holder of `waited_peer`.
void install(const Cycle& cycle, Member member) noexcept {
    wd::thread_record& idle = wd::thread_at_(kIdleSlot);
    idle.generation.store_release(member.generation);
    idle.wait_epoch.store_release(member.wait_epoch);
    wd::record_at_(index_of(cycle.waited_peer)).owner.store_release(wd::pack(kIdleSlot, member.generation));
}

// The state of the digest of the cycle before the holder of its first link,
// in the order of the words of detail::cycle_digest.
[[nodiscard]] std::uint64_t state_before_holder(const Cycle& cycle) noexcept {
    std::uint64_t state = ::foundation::reflect::combine_ids(0, 2);
    state = ::foundation::reflect::combine_ids(state, (std::uint64_t{index_of(cycle.waited)} << 32)
                                                          | w::record_generation(cycle.waited));
    return ::foundation::reflect::combine_ids(state, index_of(cycle.waited_peer));
}

// The member of `generation` whose digest state after the first link is the
// state of `first` there.  The words after it are the same in each member.
[[nodiscard]] Member member_like(std::uint64_t before_holder, Member first, std::uint32_t generation) noexcept {
    const std::uint64_t target = ::foundation::reflect::combine_ids(
        ::foundation::reflect::combine_ids(before_holder, wd::pack(kIdleSlot, first.generation)), first.wait_epoch);
    const std::uint64_t after_holder =
        ::foundation::reflect::combine_ids(before_holder, wd::pack(kIdleSlot, generation));
    return Member{.generation = generation, .wait_epoch = word_between(after_holder, target)};
}

void poll_to_trace_point(w::wait_scope& wait) noexcept {
    for (int poll = 0; poll < kPollsToTracePoint; ++poll)
        wait.poll();
}

// The idle slot gets a new generation before each trace point, so no two
// traces of the wait see one cycle, but each trace sees one digest.  The
// wait traces three times and reports no deadlock.
void cycles_of_one_digest_are_no_deadlock(const Cycle& cycle) {
    const std::uint64_t self = wd::owner_token();
    const Member first{.generation = kFirstGeneration, .wait_epoch = 0x1234};
    install(cycle, first);
    wd::suspect_cycle const& suspect = wd::suspect_at_(wd::index_of(self));
    {
        w::wait_scope wait{cycle.waited};
        const std::uint64_t before_holder = state_before_holder(cycle);
        const std::uint64_t digest = wd::cycle_digest(wd::trace_chain(self, index_of(cycle.waited)));
        CRUCIBLE_TEST_ASSERT(kTest, digest != 0);
        std::uint32_t traces = 0;
        for (std::uint32_t point = 0; traces < 3; ++point) {
            CRUCIBLE_TEST_ASSERT(kTest, point < kMaxTracePoints);
            const Member member = member_like(before_holder, first, kFirstGeneration + point);
            install(cycle, member);
            const wd::wait_chain chain = wd::trace_chain(self, index_of(cycle.waited));
            CRUCIBLE_TEST_ASSERT(kTest, chain.is_cycle && chain.count == 2);
            CRUCIBLE_TEST_ASSERT(kTest, chain.links[0].holder == wd::pack(kIdleSlot, member.generation));
            CRUCIBLE_TEST_ASSERT(kTest, wd::cycle_digest(chain) == digest);
            poll_to_trace_point(wait);
            // A trace at this trace point saw this member, and no other
            // trace point saw it.
            if (suspect.chain.links[0].holder == wd::pack(kIdleSlot, member.generation)) ++traces;
        }
    }
}

// Polls `wait` until the suspect of the thread names the wait of
// `wait_epoch`: the wait traced the cycle.
void poll_until_traced_by(w::wait_scope& wait, wd::suspect_cycle const& suspect, std::uint64_t wait_epoch) noexcept {
    for (std::uint32_t point = 0; suspect.wait_epoch != wait_epoch; ++point) {
        CRUCIBLE_TEST_ASSERT(kTest, point < kMaxTracePoints);
        poll_to_trace_point(wait);
    }
}

// A wait inside a wait on one thread traces the cycle first.  The outer
// wait is then due, and its first trace sees the same cycle.  It does not
// take the trace of the inner wait for its own, so it reports nothing.  Its
// second trace repeats its first, link for link: a deadlock.  The polls of the
// second trace run in a child through expect_dies of the Check family, so the
// report of the deadlock ends the child.
void one_cycle_at_two_traces_of_one_wait_is_a_deadlock(const Cycle& cycle) {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    const std::uint64_t self = wd::owner_token();
    install(cycle, Member{.generation = kFirstGeneration, .wait_epoch = 0x5678});
    wd::suspect_cycle const& suspect = wd::suspect_at_(wd::index_of(self));
    wd::thread_record const& me = wd::thread_at_(wd::index_of(self));
    {
        w::wait_scope outer{cycle.waited};
        const std::uint64_t outer_epoch = me.wait_epoch.load_acquire();
        poll_to_trace_point(outer);
        CRUCIBLE_TEST_ASSERT(kTest, outer.trace_points() == 1);
        {
            w::wait_scope inner{cycle.waited};
            const std::uint64_t inner_epoch = me.wait_epoch.load_acquire();
            CRUCIBLE_TEST_ASSERT(kTest, inner_epoch == outer_epoch + 1);
            poll_until_traced_by(inner, suspect, inner_epoch);
        }
        poll_until_traced_by(outer, suspect, outer_epoch);
        ::fixy::check::expect_dies(
            test,
            [&outer] noexcept {
                for (std::uint32_t point = 0; point < kMaxTracePoints; ++point)
                    poll_to_trace_point(outer);
            },
            ::fixy::TextView{"fixy::session: DEADLOCK ACROSS SESSIONS"});
    }
}

// A chain and a suspect hold no padding byte.  The suspects of the threads
// are a large list, and the rule padded-list refuses a padded element there.
void chain_and_suspect_have_no_padding_byte() {
    crucible::test::expect_no_padding_byte<^^wd::wait_chain>(kTest);
    crucible::test::expect_no_padding_byte<^^wd::suspect_cycle>(kTest);
}

}  // namespace

int main() {
    chain_and_suspect_have_no_padding_byte();
    with_cycle(cycles_of_one_digest_are_no_deadlock);
    with_cycle(one_cycle_at_two_traces_of_one_wait_is_a_deadlock);
    ::fixy::check::pass(kTest, "test_session_watch_cycle: OK\n");
    return 0;
}
