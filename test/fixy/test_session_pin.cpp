// The pin of a session handle (HandleFactory::pin of fixy/session/Handle.h).
//
// A pin holds a handle on the calling thread for the run of a body.  The
// body steps the handle under the pinned policy, which keeps each check of
// the tracked policy and has no holder check, and the pin turns the handle
// that the body gives back to the tracked policy.  The cells below run the
// pin, also with a body that captures this of a stage that holds the handle,
// read the count of the steps across it, show that the pinned handle
// cannot leave its thread, and show that a dropped pinned handle still
// reports the drop.  Two more cells run a bind and a release with no body,
// and a session that opens under the pinned policy.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <utility>

namespace {

namespace s = ::fixy::session;

struct Ping {
    int value = 0;
};

struct Wire {
    int last_sent = 0;
};

constexpr auto write_ping = [](Wire& wire, Ping& ping) noexcept {
    wire.last_sent = ping.value;
    return true;
};

using Pings = s::Loop<s::Send<Ping, s::Continue>>;
using Once = s::Send<Ping, s::End>;

using TrackedHandle = decltype(s::mint_session<Pings>(::foundation::effects::testing::foreground(), Wire{}));
using PinnedHandle = s::SessionHandle<typename TrackedHandle::protocol, Wire, typename TrackedHandle::loop_ctx,
                                      s::check::Pinned, ::foundation::permissions::EmptyPermSet>;

// The pinned policy is thread_bound and not Shareable, so a pinned handle
// cannot move to another thread, and the tracked handle can.
static_assert(::foundation::transfer::Sendable<TrackedHandle>);
static_assert(!::foundation::transfer::Sendable<PinnedHandle>);
static_assert(!::foundation::transfer::Shareable<PinnedHandle>);
static_assert(s::AbandonmentPolicy<s::check::Pinned> && s::AbandonmentPolicy<s::check::PinnedCancel>);

// A body that reaches the handle through a capture is refused, and a body
// that captures a value of its caller is admitted.
static_assert(s::PinBody<decltype([](PinnedHandle pinned) noexcept { return pinned; })>);

// A pin runs its body on the pinned handle, and the turn back gives a tracked
// handle at the position where the body stops.  A turn is no step, so the
// count of the steps grows only by the steps of the body.
void pin_steps_the_handle(::fixy::TestRunnerCtx const& test) {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Pings>(ctx, Wire{});
    const std::uint64_t minted = head.step_count();

    auto still = s::HandleFactory::pin(ctx, std::move(head), [](PinnedHandle pinned) noexcept { return pinned; });
    CRUCIBLE_TEST_ASSERT(test, still.step_count() == minted);
    CRUCIBLE_TEST_ASSERT(test, still.is_live());

    int sent = 0;
    auto stepped = s::HandleFactory::pin(ctx, std::move(still), [&sent](PinnedHandle pinned) noexcept {
        for (int round = 1; round <= 3; ++round) {
            pinned = std::move(pinned).send(Ping{round}, write_ping);
            ++sent;
        }
        return pinned;
    });
    CRUCIBLE_TEST_ASSERT(test, sent == 3);
    CRUCIBLE_TEST_ASSERT(test, stepped.step_count() == minted + 3);

    // The tracked handle steps on after the pin.
    auto next = std::move(stepped).send(Ping{4}, write_ping);
    CRUCIBLE_TEST_ASSERT(test, next.step_count() == minted + 4);
    std::move(next).detach(s::detach_reason::TestInstrumentation{});
    ::fixy::check::pass(test, "  pin_steps_the_handle: PASSED\n");
}

// A stage parks the handle of one session in an Option, and pins the handle
// of another session with a body that captures this, to count the sends in a
// member.  The site of a handle is a HandleSite, whose file is a StaticText,
// so the store check of the pin reads no borrow in the stage and takes the
// body.
class Stage {
public:
    explicit Stage(TrackedHandle parked) noexcept { parked_.fill(std::move(parked)); }

    [[nodiscard]] TrackedHandle send_three(TrackedHandle handle) noexcept {
        const auto ctx = ::foundation::effects::testing::foreground();
        return s::HandleFactory::pin(ctx, std::move(handle), [this](PinnedHandle pinned) noexcept {
            for (int round = 1; round <= 3; ++round) {
                pinned = std::move(pinned).send(Ping{round}, write_ping);
                ++sent_;
            }
            return pinned;
        });
    }

    [[nodiscard]] int sent() const noexcept { return sent_; }

    // Ends the stage and gives its parked handle back.
    [[nodiscard]] TrackedHandle give_back() && noexcept {
        return parked_.take().expect("the stage parks its handle until it gives it back");
    }

private:
    ::fixy::Option<TrackedHandle> parked_{};
    int sent_ = 0;
};

void pin_a_body_of_a_stage(::fixy::TestRunnerCtx const& test) {
    const auto ctx = ::foundation::effects::testing::foreground();
    Stage stage{s::mint_session<Pings>(ctx, Wire{})};
    auto head = s::mint_session<Pings>(ctx, Wire{});
    const std::uint64_t minted = head.step_count();
    TrackedHandle stepped = stage.send_three(std::move(head));
    CRUCIBLE_TEST_ASSERT(test, stage.sent() == 3);
    CRUCIBLE_TEST_ASSERT(test, stepped.step_count() == minted + 3);
    std::move(stepped).detach(s::detach_reason::TestInstrumentation{});
    TrackedHandle parked = std::move(stage).give_back();
    CRUCIBLE_TEST_ASSERT(test, parked.is_live());
    std::move(parked).detach(s::detach_reason::TestInstrumentation{});
    ::fixy::check::pass(test, "  pin_a_body_of_a_stage: PASSED\n");
}

// A body can end the protocol: the turn back gives the handle at End, which
// closes.
void pin_reaches_the_end(::fixy::TestRunnerCtx const& test) {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Once>(ctx, Wire{});
    using OncePinned = s::SessionHandle<Once, Wire, void, s::check::Pinned, ::foundation::permissions::EmptyPermSet>;
    auto at_end = s::HandleFactory::pin(
        ctx, std::move(head), [](OncePinned pinned) noexcept { return std::move(pinned).send(Ping{7}, write_ping); });
    const Wire wire = std::move(at_end).close();
    CRUCIBLE_TEST_ASSERT(test, wire.last_sent == 7);
    ::fixy::check::pass(test, "  pin_reaches_the_end: PASSED\n");
}

// A bind holds a handle on the calling thread for the rest of its life, with
// no body, and a release gives the tracked handle back at the same position.
// A bind and a release are no steps, so the count grows only by the sends.
void bind_and_release_the_handle(::fixy::TestRunnerCtx const& test) {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Pings>(ctx, Wire{});
    const std::uint64_t minted = head.step_count();
    PinnedHandle bound = s::HandleFactory::bind(ctx, std::move(head));
    bound = std::move(bound).send(Ping{1}, write_ping);
    bound = std::move(bound).send(Ping{2}, write_ping);
    CRUCIBLE_TEST_ASSERT(test, bound.step_count() == minted + 2);
    TrackedHandle released = s::HandleFactory::release(std::move(bound));
    CRUCIBLE_TEST_ASSERT(test, released.step_count() == minted + 2);
    CRUCIBLE_TEST_ASSERT(test, released.is_live());
    std::move(released).detach(s::detach_reason::TestInstrumentation{});
    ::fixy::check::pass(test, "  bind_and_release_the_handle: PASSED\n");
}

// A session opens under the pinned policy: the claim of its record names the
// calling thread, so the mint notes no other holder, and the type keeps each
// handle of the session on that thread.
void mint_a_pinned_session(::fixy::TestRunnerCtx const& test) {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Once, s::check::Pinned>(ctx, Wire{});
    static_assert(!::foundation::transfer::Sendable<decltype(head)>);
    auto at_end = std::move(head).send(Ping{5}, write_ping);
    const Wire wire = std::move(at_end).close();
    CRUCIBLE_TEST_ASSERT(test, wire.last_sent == 5);
    ::fixy::check::pass(test, "  mint_a_pinned_session: PASSED\n");
}

// A pinned handle that the body drops is a dropped protocol, as under the
// tracked policy, and the report names the site of the mint.
void drop_a_pinned_handle() {
    const auto ctx = ::foundation::effects::testing::foreground();
    auto head = s::mint_session<Pings>(ctx, Wire{});
    auto back = s::HandleFactory::pin(ctx, std::move(head), [](PinnedHandle pinned) noexcept -> PinnedHandle {
        {
            PinnedHandle dropped = std::move(pinned).send(Ping{1}, write_ping);
            static_cast<void>(dropped);
        }
        // The end of the scope above reports the drop and ends the process.
        ::fixy::unreachable();
    });
    std::move(back).detach(s::detach_reason::TestInstrumentation{});
}

void pin_reports_a_drop(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(test, drop_a_pinned_handle,
                               ::fixy::TextView{"Construction at:  test/fixy/test_session_pin.cpp:"});
    ::fixy::check::pass(test, "  pin_reports_a_drop: PASSED\n");
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    pin_steps_the_handle(test);
    pin_a_body_of_a_stage(test);
    pin_reaches_the_end(test);
    bind_and_release_the_handle(test);
    mint_a_pinned_session(test);
    pin_reports_a_drop(test);
    return 0;
}
