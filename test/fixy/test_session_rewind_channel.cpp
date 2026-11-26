// The rewind of the two ends of a channel, HandleFactory::rewind_channel of
// fixy/session/Handle.h, and the rewind of one end alone,
// HandleFactory::rewind.
//
// Each handle at End keeps the mark of the record that its step into End
// released, and the two ends of one channel keep two marks that name each
// other.  When the program runs, the rewind of a channel refuses two ends of
// two channels, and an end of a channel together with an end of no channel.
// The gate refuses two ends whose loops at End do not face each other, in
// the fixture neg_sess_rewind_channel_loops_differ.
//
// The rewind of one end alone refuses an end whose mark names a peer.  It
// would claim a record with no peer, and the watch would no longer see the
// channel.  Such an end rewinds with its peer, through the rewind of the
// channel.  An end of a session that is no channel rewinds alone.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace s = ::fixy::session;

namespace {

using ::fixy::check::expect_dies;
using ::fixy::check::pass;

struct LeftWire {
    int words = 0;
};
struct RightWire {
    int words = 0;
};

using Once = s::Send<int, s::End>;

// The text of the refusal: the predicate of the precondition of the rewind.
constexpr ::fixy::TextView kRefusal{"are_ends_of_one_channel"};

// The text of the refusal of a rewind of one end alone.
constexpr ::fixy::TextView kAloneRefusal{"is_end_of_no_channel"};

using Picks = s::Select<Once, s::End>;

// A handle at End keeps its mark only under a policy that keeps a record.
// Under check::Off it is the size of its Resource.  Under check::Enforced
// the mark adds two words to the four words of a checked handle with no
// count of steps.
static_assert(sizeof(s::SessionHandle<s::End, LeftWire, void, s::check::Off>) == sizeof(LeftWire));
static_assert(sizeof(s::SessionHandle<s::End, LeftWire, void, s::check::Enforced>) == 6 * sizeof(void*));

// The two ends of a test channel at End rewind to End, and then to the start
// of Once.  The first rewind links the two new records before each end steps
// into End, so the two marks of the handles at End name each other, and the
// second rewind takes them.  Each rewound end at Once has a live record.
void rewind_twice(::fixy::TestRunnerCtx const& test) {
    auto [left_end, right_end] = s::mint_test_channel<s::End>(test, LeftWire{}, RightWire{});
    auto [left_again, right_again] =
        s::HandleFactory::rewind_channel<s::End, void>(std::move(left_end), std::move(right_end));
    auto [left_head, right_head] =
        s::HandleFactory::rewind_channel<Once, void>(std::move(left_again), std::move(right_again));
    const bool is_each_end_live =
        s::watch::is_live(left_head.watch_endpoint()) && s::watch::is_live(right_head.watch_endpoint());
    std::move(left_head).detach(s::detach_reason::TestInstrumentation{});
    std::move(right_head).detach(s::detach_reason::TestInstrumentation{});
    CRUCIBLE_TEST_ASSERT(test, is_each_end_live);
    pass(test, "  rewind_twice: PASSED\n");
}

// The two ends of a test channel reach End inside a loop: the left end in
// the Loop of its protocol, and the right end in the dual Loop.  The two
// loops face each other, so the gate admits the pair, and the rewind takes
// it.
void rewind_ends_in_a_loop(::fixy::TestRunnerCtx const& test) {
    using Repeats = s::Loop<s::Select<s::Send<int, s::Continue>, s::End>>;
    auto [left_head, right_head] = s::mint_test_channel<Repeats>(test, LeftWire{}, RightWire{});
    auto left_end = std::move(left_head).select_local<1>(test);
    auto right_end = std::move(right_head).pick_local<1>(test);
    auto [left_again, right_again] =
        s::HandleFactory::rewind_channel<s::End, void>(std::move(left_end), std::move(right_end));
    static_cast<void>(std::move(left_again).close());
    static_cast<void>(std::move(right_again).close());
    pass(test, "  rewind_ends_in_a_loop: PASSED\n");
}

// The left end of one test channel and the right end of a second one.  The
// rewind would link two records whose sessions are not one channel, so it
// ends the process.
void ends_of_two_channels_are_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            auto [first_left, first_right] = s::mint_test_channel<s::End>(test, LeftWire{}, RightWire{});
            auto [second_left, second_right] = s::mint_test_channel<s::End>(test, LeftWire{}, RightWire{});
            auto [left_again, right_again] =
                s::HandleFactory::rewind_channel<s::End, void>(std::move(first_left), std::move(second_right));
            static_cast<void>(std::move(left_again).close());
            static_cast<void>(std::move(right_again).close());
            static_cast<void>(std::move(first_right).close());
            static_cast<void>(std::move(second_left).close());
        },
        kRefusal);
    pass(test, "  ends_of_two_channels_are_refused: PASSED\n");
}

// The left end of a test channel and the end of a session that is no
// channel.  The mark of the session names no peer, so the rewind ends the
// process.
void end_of_no_channel_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            auto [left_end, right_end] = s::mint_test_channel<s::End>(test, LeftWire{}, RightWire{});
            auto alone = s::mint_session<s::End>(test, RightWire{});
            auto [left_again, right_again] =
                s::HandleFactory::rewind_channel<s::End, void>(std::move(left_end), std::move(alone));
            static_cast<void>(std::move(left_again).close());
            static_cast<void>(std::move(right_again).close());
            static_cast<void>(std::move(right_end).close());
        },
        kRefusal);
    pass(test, "  end_of_no_channel_is_refused: PASSED\n");
}

// The left end of a test channel rewinds alone, and its peer stays at End.
// The mark of the left end names the right end, so the rewind ends the
// process.  Each handle of the body is closed or detached, so a rewind that
// accepts the end lets the child exit with the code 0.
void end_of_a_channel_alone_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            auto [left_end, right_end] = s::mint_test_channel<s::End>(test, LeftWire{}, RightWire{});
            auto rewound = s::HandleFactory::rewind<Once, void>(std::move(left_end));
            std::move(rewound).detach(s::detach_reason::TestInstrumentation{});
            static_cast<void>(std::move(right_end).close());
        },
        kAloneRefusal);
    pass(test, "  end_of_a_channel_alone_is_refused: PASSED\n");
}

// The right end reaches End first, and the watch releases its record.  The
// left end then reaches End, and its mark still names the right end, so a
// rewind of the left end alone ends the process.
void end_after_its_peer_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            auto [left_head, right_head] = s::mint_test_channel<Picks>(test, LeftWire{}, RightWire{});
            auto right_end = std::move(right_head).pick_local<1>(test);
            auto left_end = std::move(left_head).select_local<1>(test);
            auto rewound = s::HandleFactory::rewind<Once, void>(std::move(left_end));
            std::move(rewound).detach(s::detach_reason::TestInstrumentation{});
            static_cast<void>(std::move(right_end).close());
        },
        kAloneRefusal);
    pass(test, "  end_after_its_peer_is_refused: PASSED\n");
}

// The rewind of a channel links its two new records, so an end that it gives
// is an end of a channel too, and a rewind of that end alone ends the process.
void rewound_end_alone_is_refused(::fixy::TestRunnerCtx const& test) {
    expect_dies(
        test,
        [&test] noexcept {
            auto [left_end, right_end] = s::mint_test_channel<s::End>(test, LeftWire{}, RightWire{});
            auto [left_again, right_again] =
                s::HandleFactory::rewind_channel<s::End, void>(std::move(left_end), std::move(right_end));
            auto rewound = s::HandleFactory::rewind<Once, void>(std::move(left_again));
            std::move(rewound).detach(s::detach_reason::TestInstrumentation{});
            static_cast<void>(std::move(right_again).close());
        },
        kAloneRefusal);
    pass(test, "  rewound_end_alone_is_refused: PASSED\n");
}

// The end of a session that is no channel names no peer, so it rewinds alone
// to a live record.  The rollback of fixy/session/Checkpoint.h takes this
// path.
void end_of_a_session_rewinds_alone(::fixy::TestRunnerCtx const& test) {
    auto alone = s::mint_session<s::End>(test, LeftWire{});
    auto rewound = s::HandleFactory::rewind<Once, void>(std::move(alone));
    const bool is_rewound_live = s::watch::is_live(rewound.watch_endpoint());
    std::move(rewound).detach(s::detach_reason::TestInstrumentation{});
    CRUCIBLE_TEST_ASSERT(test, is_rewound_live);
    pass(test, "  end_of_a_session_rewinds_alone: PASSED\n");
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    rewind_twice(test);
    rewind_ends_in_a_loop(test);
    ends_of_two_channels_are_refused(test);
    end_of_no_channel_is_refused(test);
    end_of_a_session_rewinds_alone(test);
    end_of_a_channel_alone_is_refused(test);
    end_after_its_peer_is_refused(test);
    rewound_end_alone_is_refused(test);
    return 0;
}
