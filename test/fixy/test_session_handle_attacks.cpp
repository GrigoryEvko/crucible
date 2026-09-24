// An adversarial campaign against the session handle, with legal code.
//
// Each attack below uses the public surface as it is meant to be used:
// no cast that removes a qualifier, no reinterpretation, no undefined
// behaviour, no reopened namespace and no friend door.  The goal of each
// attack is to drop a protocol, to use a handle twice, or to make a
// deadlock, and to do it with code that compiles.
//
// An attack runs in a child process, because the result of a caught
// attack is std::abort.  The parent reads the wait status and sorts it:
//
//   Caught    the child died on a signal: the policy aborted.
//   Silent    the child reached its end, and no policy acted.
//   Deadlock  a wait in the child passed its deadline.
//   Correct   the child checked its own result and found it correct.
//
// Every wait has a deadline, so no attack can hang the test run.
//
// ── The ledger ───────────────────────────────────────────────────────
//
// An attack that is Silent or Deadlock succeeded: the discipline did not
// stop it.  Each such attack has a row in known_limitations, with the
// condition of LinearActris (Jacobs, Hinrichsen and Krebbers, POPL 2024)
// that it breaks:
//
//   linearity   each endpoint is consumed exactly once;
//   forest      threads and channels form a forest, so no cycle of waits
//               can form.
//
// The ledger can only shrink.  An attack that stops succeeding fails this
// test until its row is removed, and an attack that starts succeeding
// fails it until a row is written.  The row count is pinned below, and
// the pin can only go down.
//
// Two routes are refused when the program compiles, and are negative
// fixtures: a new-expression of a handle (neg_sess_handle_heap_new) and
// std::make_unique of a handle (neg_sess_handle_make_unique).  The other
// compile-time attacks are named neg_sess_* and neg_rule_r004_*, and one
// is pinned here as a static_assert: a hot binding that waits in a
// receive compiles.
//
// ── What fixy/session/Watch.h adds ───────────────────────────────────
//
// A handle that is never destroyed leaks its protocol, whatever holds
// it: storage from ::new, an owner that was released, a coroutine frame
// that was never destroyed, a static object that quick_exit skips.  The
// watch keeps a record of each live session, and std::exit and
// std::quick_exit report a live record and abort.  So each leak route
// below ends through exit or quick_exit and is caught.  std::abort and the
// fatal signals report the live records through the signal handler of the
// watch, and a child that captures its standard error proves each one.
// The route that runs no code at all is one row for all of them:
// leak_then_skip_exit_hooks.
//
// The watch also orders the sessions by priority.  A wait on an endpoint
// whose peer the thread holds, or while the thread holds another endpoint
// of a priority that is not lower, is refused before the thread waits.
// The two forest attacks use polling transports and are refused so, and
// the captured diagnostics show which check refused each.  A transport
// that waits inside its own call publishes no wait, which is the row
// blocking_transport_hides_the_cycle.

#include <fixy/Fn.h>
#include <fixy/ScopedView.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;
namespace eff = ::foundation::effects;

// The permission trees of the forked attacks.  The split manifests must
// be written in foundation::permissions, so the tags come first.
namespace attack_tags {
struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};
struct Whole2 {
    using permission_row = eff::Row<>;
};
struct Left2 {
    using permission_row = eff::Row<>;
};
struct Right2 {
    using permission_row = eff::Row<>;
};
}  // namespace attack_tags

template <>
struct foundation::permissions::splits_into_pack<attack_tags::Whole, attack_tags::Left, attack_tags::Right>
    : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack_authoring_witness<attack_tags::Whole, attack_tags::Left,
                                                                    attack_tags::Right> : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack<attack_tags::Whole2, attack_tags::Left2, attack_tags::Right2>
    : std::true_type {};
template <>
struct foundation::permissions::splits_into_pack_authoring_witness<attack_tags::Whole2, attack_tags::Left2,
                                                                    attack_tags::Right2> : std::true_type {};

namespace {

// ── The outcomes and the ledger ──────────────────────────────────────

enum class Outcome : std::uint8_t { Caught, Silent, Deadlock, Correct };

constexpr int kSilentExit = 0;
constexpr int kDeadlockExit = 42;
constexpr int kCorrectExit = 43;

struct attack_row {
    std::string_view name{};
    Outcome expected{};
};

struct limitation_row {
    std::string_view attack{};
    std::string_view breaks{};
};

// The attacks that succeed, and the condition each one breaks.
constexpr limitation_row known_limitations[] = {
    {"leak_then_skip_exit_hooks",
     "linearity: the watch reads the live sessions at std::exit, std::quick_exit, std::abort and each fatal signal.  "
     "std::_Exit and SIGKILL end the process without running one more instruction of it, so no reader of the watch "
     "can run"},
    {"detach_with_a_false_reason",
     "linearity: a detach reason states an intent, and no type can check that the intent is true.  The reason is a "
     "tag, so each detach names its class and a search finds it"},
    {"off_policy_drop",
     "linearity: check::Off is empty by contract, so that a handle under it costs exactly its Resource.  With no "
     "state, nothing can see a dropped protocol"},
    {"off_policy_use_after_move",
     "linearity: a liveness flag needs a byte, and with alignment it grows an 8-byte handle to 16.  check::Off "
     "gives that byte up by contract, so a moved-from handle steps the protocol again"},
    {"blocking_transport_hides_the_cycle",
     "forest: a transport that waits inside its own call publishes no wait, so the deadlock detector of "
     "fixy/session/Watch.h cannot see the cycle.  A polling transport lets the handle wait, and the detector sees it"},
};

// The ledger can only shrink.  Lower this number when a row leaves.
constexpr std::size_t kLedgerCeiling = 5;
static_assert(sizeof(known_limitations) / sizeof(known_limitations[0]) <= kLedgerCeiling,
              "the ledger of known limitations grew.  A new successful attack is a finding: fix it, or report it "
              "and raise nothing here without a decision.");

// ── The watchdog ─────────────────────────────────────────────────────

constexpr auto kAttackDeadline = std::chrono::milliseconds{800};

[[noreturn]] void deadlock_detected(const char* where) noexcept {
    std::fprintf(stderr, "[attack] watchdog: %s waited past its deadline\n", where);
    std::_Exit(kDeadlockExit);
}

// ── Resources ────────────────────────────────────────────────────────

struct Ping {
    int value = 0;
};
struct Pong {
    int value = 0;
};

struct Wire {
    int last_sent = 0;
};

using Once = s::Send<Ping, s::Recv<Pong, s::End>>;

struct Mailbox {
    std::atomic<int> value{0};
    std::atomic<bool> full{false};
};

struct Pipe : ::foundation::Pinned<Pipe> {
    Mailbox to_left;
    Mailbox to_right;
    std::atomic<bool> left_cancelled{false};
};

struct LeftEnd {
    Pipe* pipe = nullptr;
};
struct RightEnd {
    Pipe* pipe = nullptr;
};

// The left end can cancel: it tells the right end through the pipe.
void cancel_session(LeftEnd& end) noexcept { end.pipe->left_cancelled.store(true, std::memory_order_release); }

void put(Mailbox& box, int value) noexcept {
    box.value.store(value, std::memory_order_relaxed);
    box.full.store(true, std::memory_order_release);
}

[[nodiscard]] int take(Mailbox& box, const char* where) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + kAttackDeadline;
    while (!box.full.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) deadlock_detected(where);
        std::this_thread::yield();
    }
    const int value = box.value.load(std::memory_order_relaxed);
    box.full.store(false, std::memory_order_release);
    return value;
}

// A polling transport over the left box of a pipe: an empty result while
// the box is empty, so the handle waits through fixy/session/Watch.h.
// The deadline still holds, so no attack can hang the test run.
struct PollLeftBox {
    const char* where = "";
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kAttackDeadline;

    template <typename End>
    [[nodiscard]] std::optional<Ping> operator()(End& end) const noexcept {
        Mailbox& box = end.pipe->to_left;
        if (!box.full.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() > deadline) deadlock_detected(where);
            return std::nullopt;
        }
        const int value = box.value.load(std::memory_order_relaxed);
        box.full.store(false, std::memory_order_release);
        return Ping{value};
    }
};

// A slot that carries one value between threads.  The attacks use it to
// move a handle where the fork did not put it.
template <typename T>
struct Slot {
    std::mutex lock;
    std::optional<T> value;

    void put(T item) noexcept {
        const std::scoped_lock guard{lock};
        value.emplace(std::move(item));
    }

    [[nodiscard]] T take(const char* where) noexcept {
        const auto deadline = std::chrono::steady_clock::now() + kAttackDeadline;
        for (;;) {
            {
                const std::scoped_lock guard{lock};
                if (value.has_value()) {
                    T item = std::move(*value);
                    value.reset();
                    return item;
                }
            }
            if (std::chrono::steady_clock::now() > deadline) deadlock_detected(where);
            std::this_thread::yield();
        }
    }
};

[[nodiscard]] auto fresh() noexcept { return s::mint_session_handle<Once, Wire>(Wire{}); }

using FreshHandle = decltype(fresh());

// ── Drop routes the policy catches ───────────────────────────────────

[[noreturn]] void drop_by_early_return() {
    const auto body = [](bool stop_early) {
        auto handle = fresh();
        if (stop_early) return;
        std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    };
    body(true);
    std::_Exit(kSilentExit);
}

[[noreturn]] void drop_by_optional_reset() {
    std::optional<FreshHandle> holder;
    holder.emplace(fresh());
    holder.reset();
    std::_Exit(kSilentExit);
}

[[noreturn]] void drop_by_vector_clear() {
    std::vector<FreshHandle> handles;
    handles.push_back(fresh());
    handles.clear();
    std::_Exit(kSilentExit);
}

// A handle refuses a new-expression, so heap storage holds it inside an
// owner type.  The owner's destructor destroys the handle.
struct Holder {
    FreshHandle handle;
};

[[noreturn]] void drop_by_unique_ptr_reset() {
    auto owner = std::make_unique<Holder>(Holder{fresh()});
    owner.reset();
    std::_Exit(kSilentExit);
}

[[noreturn]] void drop_by_variant_reassign() {
    std::variant<FreshHandle, int> slot{std::in_place_index<0>, fresh()};
    slot = 7;
    std::_Exit(kSilentExit);
}

[[noreturn]] void drop_by_move_only_function() {
    {
        std::move_only_function<void()> never_called = [handle = fresh()]() mutable noexcept {
            std::move(handle).detach(s::detach_reason::TestInstrumentation{});
        };
    }
    std::_Exit(kSilentExit);
}

[[noreturn]] void drop_on_thread_exit() {
    std::jthread worker{[] { auto handle = fresh(); }};
    worker.join();
    std::_Exit(kSilentExit);
}

[[noreturn]] void static_handle_at_exit() {
    static FreshHandle kept = fresh();
    (void)kept;
    std::exit(kSilentExit);
}

[[noreturn]] void assign_over_a_live_handle() {
    auto target = fresh();
    target = fresh();
    std::move(target).detach(s::detach_reason::TestInstrumentation{});
    std::_Exit(kSilentExit);
}

// ── Use after move ───────────────────────────────────────────────────

[[noreturn]] void use_after_move() {
    auto handle = fresh();
    auto taken = std::move(handle);
    std::move(taken).detach(s::detach_reason::TestInstrumentation{});
    auto stepped = std::move(handle).send(Ping{1}, [](Wire&, Ping&&) noexcept {});
    std::move(stepped).detach(s::detach_reason::TestInstrumentation{});
    std::_Exit(kSilentExit);
}

[[noreturn]] void close_twice() {
    auto at_end = s::mint_session_handle<s::Send<Ping, s::End>, Wire>(Wire{}).send(Ping{}, [](Wire&, Ping&&) noexcept {});
    auto& alias = at_end;
    (void)std::move(at_end).close();
    (void)std::move(alias).close();
    std::_Exit(kSilentExit);
}

[[noreturn]] void read_resource_after_move() {
    auto handle = fresh();
    auto taken = std::move(handle);
    std::move(taken).detach(s::detach_reason::TestInstrumentation{});
    const int seen = handle.resource().last_sent;
    (void)seen;
    std::_Exit(kSilentExit);
}

// A view of a moved-from handle would look at a position that nothing
// holds.  The view gate's precondition reads the handle's liveness.
[[noreturn]] void view_a_moved_from_handle() {
    auto handle = fresh();
    auto taken = std::move(handle);
    std::move(taken).detach(s::detach_reason::TestInstrumentation{});
    const auto view = ::fixy::mint_view<s::position::AtSend>(handle);
    (void)view;
    std::_Exit(kSilentExit);
}

// A swap of two live handles moves each through a temporary, and each
// source is consumed before it is assigned to.  Both handles stay live.
[[noreturn]] void swap_two_live_handles() {
    auto first = fresh();
    auto second = fresh();
    std::swap(first, second);
    std::move(first).detach(s::detach_reason::TestInstrumentation{});
    std::move(second).detach(s::detach_reason::TestInstrumentation{});
    std::_Exit(kCorrectExit);
}

[[noreturn]] void self_move_assign() {
    auto handle = fresh();
    auto& same = handle;
    handle = std::move(same);
    auto after = std::move(handle).send(Ping{3}, [](Wire&, Ping&&) noexcept {});
    std::move(after).detach(s::detach_reason::TestInstrumentation{});
    std::_Exit(kCorrectExit);
}

// ── Coroutines ───────────────────────────────────────────────────────

struct Task {
    struct promise_type {
        Task get_return_object() noexcept { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { std::abort(); }
    };

    std::coroutine_handle<promise_type> frame;

    explicit Task(std::coroutine_handle<promise_type> owned) noexcept : frame{owned} {}
    Task(Task&& other) noexcept : frame{std::exchange(other.frame, {})} {}
    Task& operator=(Task&&) = delete;
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        if (frame) frame.destroy();
    }

    // Gives up the frame without destroying it.
    void abandon() noexcept { frame = {}; }
};

Task hold_across_suspension(FreshHandle handle) {
    co_await std::suspend_always{};
    auto after = std::move(handle).send(Ping{4}, [](Wire&, Ping&&) noexcept {});
    std::move(after).detach(s::detach_reason::TestInstrumentation{});
}

[[noreturn]] void coroutine_destroyed_before_resume() {
    {
        Task task = hold_across_suspension(fresh());
        (void)task;
    }
    std::_Exit(kSilentExit);
}

// ── Leaks the exit report catches ────────────────────────────────────
//
// Each handle below is never destroyed, so no destructor sees the drop.
// The process ends through std::exit or std::quick_exit, and the watch
// reports the live session there and aborts.

[[noreturn]] void leak_global_new_then_exit() {
    // ::new skips the class-scope operator new that the handle deletes.
    [[maybe_unused]] auto* leaked = ::new FreshHandle{fresh()};
    std::exit(kSilentExit);
}

[[noreturn]] void leak_released_holder_then_exit() {
    auto owner = std::make_unique<Holder>(Holder{fresh()});
    [[maybe_unused]] Holder* released = owner.release();
    std::exit(kSilentExit);
}

[[noreturn]] void leak_coroutine_frame_then_exit() {
    Task task = hold_across_suspension(fresh());
    task.abandon();
    std::exit(kSilentExit);
}

[[noreturn]] void quick_exit_with_static_handle() {
    static FreshHandle kept = fresh();
    (void)kept;
    std::quick_exit(kSilentExit);
}

// ── Attacks that succeed ─────────────────────────────────────────────

// Every leak above ends through a hook.  This one ends the process with
// no hook, so no reader of the watch runs.
[[noreturn]] void leak_then_skip_exit_hooks() {
    [[maybe_unused]] auto* leaked = ::new FreshHandle{fresh()};
    std::_Exit(kSilentExit);
}

[[noreturn]] void detach_with_a_false_reason() {
    auto handle = fresh();
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    std::_Exit(kSilentExit);
}

[[noreturn]] void off_policy_drop() {
    {
        auto handle = s::mint_session_handle<Once, Wire, s::check::Off>(Wire{});
        (void)handle;
    }
    std::_Exit(kSilentExit);
}

[[noreturn]] void off_policy_use_after_move() {
    auto handle = s::mint_session_handle<Once, Wire, s::check::Off>(Wire{});
    auto taken = std::move(handle);
    auto first = std::move(taken).send(Ping{5}, [](Wire&, Ping&&) noexcept {});
    auto second = std::move(handle).send(Ping{6}, [](Wire&, Ping&&) noexcept {});
    (void)first;
    (void)second;
    std::_Exit(kSilentExit);
}

// ── Deadlock through the fork-shaped mint ────────────────────────────

using BgCtx = eff::detail::ctx_witnesses::BgWitness;

// The left side receives one Ping, and the right side sends it.
using LeftProto = s::Recv<Ping, s::End>;

// The right body gives its endpoint away through a slot, and then waits
// for its End handle to come back.  The brand makes it wait: no other End
// handle has its type.
struct GiveAwayBody;
using RightHead = s::detail::forked_head_t<s::dual_of_t<LeftProto>, RightEnd, s::DefaultAbandonmentPolicy, GiveAwayBody>;
using RightDone = decltype(std::declval<RightHead>().send(Ping{}, [](RightEnd&, Ping&&) noexcept {}));

Slot<RightHead> g_right_endpoint;
Slot<RightDone> g_right_done;

struct GiveAwayBody {
    RightDone operator()(RightHead head, perm::Permission<attack_tags::Right>, BgCtx const&) noexcept {
        g_right_endpoint.put(std::move(head));
        return g_right_done.take("the right body, for its End handle");
    }
};

// How a left body receives: through a polling transport, which lets the
// handle wait through the watch, or through a transport that waits
// inside its own call.
enum class Receive : std::uint8_t { Polling, Blocking };

// Receives one Ping on the left end of `head`, in the given way.
template <Receive How, typename Head>
[[nodiscard]] auto receive_ping(Head head, const char* where) noexcept {
    if constexpr (How == Receive::Polling) {
        return std::move(head).recv(PollLeftBox{where});
    } else {
        return std::move(head).recv([where](LeftEnd& e) noexcept { return Ping{take(e.pipe->to_left, where)}; });
    }
}

// The left body takes the right endpoint, so one thread holds the two
// ends of one channel.  It then waits on its own end first, for the Ping
// that only its other end can send.
template <Receive How>
struct TakesBothBody {
    template <typename Head>
    auto operator()(Head head, perm::Permission<attack_tags::Left>, BgCtx const&) noexcept {
        RightHead other = g_right_endpoint.take("the left body, for the right endpoint");
        auto [ping, done] = receive_ping<How>(std::move(head), "left recv");
        g_right_done.put(std::move(other).send(Ping{ping.value}, [](RightEnd& e, Ping&& p) noexcept {
            put(e.pipe->to_left, p.value);
        }));
        return std::move(done);
    }
};

template <Receive How>
[[noreturn]] void run_one_thread_holds_both_ends() {
    Pipe pipe{};
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_channel<LeftProto, attack_tags::Left, attack_tags::Right>(
        ctx, perm::mint_permission_root<attack_tags::Whole>(), LeftEnd{&pipe}, RightEnd{&pipe}, TakesBothBody<How>{},
        GiveAwayBody{});
    perm::permission_drop(std::move(back));
    std::_Exit(kSilentExit);
}

// The watch sees the wait on the polling transport, finds that the peer
// of that endpoint is held by the waiting thread itself, and aborts.
[[noreturn]] void one_thread_holds_both_ends_after_fork() { run_one_thread_holds_both_ends<Receive::Polling>(); }

// The same cycle through a transport that waits in its own call.
[[noreturn]] void blocking_transport_hides_the_cycle() { run_one_thread_holds_both_ends<Receive::Blocking>(); }

// Two forked sessions, each run from its own thread.  Each right body
// gives its endpoint to the left body of the OTHER session.  Each left
// body then waits on its own session for the message that its partner's
// thread would send only after its own wait ends.
struct CycleRightA;
struct CycleRightB;
using CycleHeadA = s::detail::forked_head_t<s::dual_of_t<LeftProto>, RightEnd, s::DefaultAbandonmentPolicy, CycleRightA>;
using CycleHeadB = s::detail::forked_head_t<s::dual_of_t<LeftProto>, RightEnd, s::DefaultAbandonmentPolicy, CycleRightB>;
using CycleDoneA = decltype(std::declval<CycleHeadA>().send(Ping{}, [](RightEnd&, Ping&&) noexcept {}));
using CycleDoneB = decltype(std::declval<CycleHeadB>().send(Ping{}, [](RightEnd&, Ping&&) noexcept {}));

Slot<CycleHeadA> g_cycle_endpoint_a;
Slot<CycleHeadB> g_cycle_endpoint_b;
Slot<CycleDoneA> g_cycle_done_a;
Slot<CycleDoneB> g_cycle_done_b;

struct CycleRightA {
    CycleDoneA operator()(CycleHeadA head, perm::Permission<attack_tags::Right>, BgCtx const&) noexcept {
        g_cycle_endpoint_a.put(std::move(head));
        return g_cycle_done_a.take("session A's right body, for its End handle");
    }
};
struct CycleRightB {
    CycleDoneB operator()(CycleHeadB head, perm::Permission<attack_tags::Right2>, BgCtx const&) noexcept {
        g_cycle_endpoint_b.put(std::move(head));
        return g_cycle_done_b.take("session B's right body, for its End handle");
    }
};

// Session A's left end holds session B's right endpoint, and the other
// way round.  Each waits on its own receive first.
struct CycleLeftA {
    template <typename Head>
    auto operator()(Head head, perm::Permission<attack_tags::Left>, BgCtx const&) noexcept {
        CycleHeadB other = g_cycle_endpoint_b.take("session A's left body, for session B's endpoint");
        auto [ping, done] = receive_ping<Receive::Polling>(std::move(head), "A recv");
        g_cycle_done_b.put(std::move(other).send(Ping{ping.value}, [](RightEnd& e, Ping&& p) noexcept {
            put(e.pipe->to_left, p.value);
        }));
        return std::move(done);
    }
};
struct CycleLeftB {
    template <typename Head>
    auto operator()(Head head, perm::Permission<attack_tags::Left2>, BgCtx const&) noexcept {
        CycleHeadA other = g_cycle_endpoint_a.take("session B's left body, for session A's endpoint");
        auto [ping, done] = receive_ping<Receive::Polling>(std::move(head), "B recv");
        g_cycle_done_a.put(std::move(other).send(Ping{ping.value}, [](RightEnd& e, Ping&& p) noexcept {
            put(e.pipe->to_left, p.value);
        }));
        return std::move(done);
    }
};

[[noreturn]] void two_forked_sessions_in_a_cycle() {
    Pipe pipe_a{};
    Pipe pipe_b{};
    const BgCtx ctx{eff::testing::bg()};
    std::jthread run_a{[&] {
        auto back = s::mint_forked_channel<LeftProto, attack_tags::Left, attack_tags::Right>(
            ctx, perm::mint_permission_root<attack_tags::Whole>(), LeftEnd{&pipe_a}, RightEnd{&pipe_a}, CycleLeftA{},
            CycleRightA{});
        perm::permission_drop(std::move(back));
    }};
    std::jthread run_b{[&] {
        auto back = s::mint_forked_channel<LeftProto, attack_tags::Left2, attack_tags::Right2>(
            ctx, perm::mint_permission_root<attack_tags::Whole2>(), LeftEnd{&pipe_b}, RightEnd{&pipe_b}, CycleLeftB{},
            CycleRightB{});
        perm::permission_drop(std::move(back));
    }};
    run_a.join();
    run_b.join();
    std::_Exit(kSilentExit);
}

// ── The priority order ───────────────────────────────────────────────
//
// A thread that waits on a session while it holds another session whose
// priority is not lower breaks the order of fixy/session/Watch.h, and the
// watch refuses the wait before the thread waits.  The other order is
// admitted.  Each Resource states its priority, and the read finds
// nothing on its first call, so the handle waits.

struct LowWire {
    static constexpr s::watch::priority session_priority{0};
    int value = 7;
};
struct HighWire {
    static constexpr s::watch::priority session_priority{1};
    int value = 7;
};

// A read that finds nothing on its first call and the value after it.
struct LateRead {
    int calls = 0;

    template <typename Wire>
    [[nodiscard]] std::optional<int> operator()(Wire& wire) noexcept {
        if (calls++ == 0) return std::nullopt;
        return wire.value;
    }
};

using OneRead = s::Recv<int, s::End>;

// Waits on the low session while it holds the high one.
[[noreturn]] void wait_low_while_holding_high() {
    auto high = s::mint_session_handle<OneRead, HighWire>(HighWire{});
    auto low = s::mint_session_handle<OneRead, LowWire>(LowWire{});
    auto [low_value, low_done] = std::move(low).recv(LateRead{});
    (void)std::move(low_done).close();
    std::move(high).detach(s::detach_reason::TestInstrumentation{});
    std::_Exit(low_value == 7 ? kSilentExit : kCorrectExit);
}

// Waits on the high session while it holds the low one.
[[noreturn]] void wait_high_while_holding_low() {
    auto low = s::mint_session_handle<OneRead, LowWire>(LowWire{});
    auto high = s::mint_session_handle<OneRead, HighWire>(HighWire{});
    auto [high_value, high_done] = std::move(high).recv(LateRead{});
    (void)std::move(high_done).close();
    auto [low_value, low_done] = std::move(low).recv(LateRead{});
    (void)std::move(low_done).close();
    std::_Exit(high_value == 7 && low_value == 7 ? kCorrectExit : kSilentExit);
}

// ── The cancellation path racing a send ──────────────────────────────
//
// The left side runs under check::Cancel and drops its handle at once.
// The right side sends a Ping at the same time and then waits for the
// Pong.  The right side must see the cancellation and stop within the
// deadline, every time.  The Ping it sent is discarded, which is the
// affine design: a message to a cancelled endpoint is lost.
[[noreturn]] void cancellation_races_a_send() {
    using LeftSide = s::Recv<Ping, s::Send<Pong, s::End>>;
    int discarded = 0;
    for (int round = 0; round < 200; ++round) {
        Pipe pipe{};
        std::jthread left{[&pipe] {
            auto handle = s::mint_session_handle<LeftSide, LeftEnd, s::check::Cancel>(LeftEnd{&pipe});
            (void)handle;
        }};
        auto right = s::mint_session_handle<s::dual_of_t<LeftSide>, RightEnd>(RightEnd{&pipe});
        auto waits = std::move(right).send(Ping{round}, [](RightEnd& e, Ping&& p) noexcept { put(e.pipe->to_left, p.value); });
        const auto deadline = std::chrono::steady_clock::now() + kAttackDeadline;
        for (;;) {
            if (waits.resource().pipe->to_right.full.load(std::memory_order_acquire)) {
                std::fprintf(stderr, "[attack] a cancelled left side sent a Pong\n");
                std::_Exit(kSilentExit);
            }
            if (waits.resource().pipe->left_cancelled.load(std::memory_order_acquire)) {
                std::move(waits).detach(s::detach_reason::AsyncCancellation{});
                break;
            }
            if (std::chrono::steady_clock::now() > deadline) deadlock_detected("the right side, for a Pong or a cancel");
            std::this_thread::yield();
        }
        left.join();
        if (pipe.to_left.full.load(std::memory_order_acquire)) ++discarded;
    }
    std::fprintf(stderr, "[attack] cancellation raced 200 sends: every one stopped, %d Pings were discarded\n",
                 discarded);
    std::_Exit(kCorrectExit);
}

// ── A hot path that waits in a receive ───────────────────────────────
//
// A hot binding that holds a handle at a receive waits in the transport,
// and the transport can wait in the kernel.  Collision rule W003 refuses
// the binding until it states its wait strategy.  With a stated spin the
// binding is admitted, and with a stated kernel wait W001 refuses it.
struct hot_invariant final {};
template <class... Wait>
using HotHoldsRecv =
    ::fixy::collision::live_rules<::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                                  ::fixy::atom::refined_with<hot_invariant>, ::fixy::atom::as_public,
                                  ::fixy::atom::session::live_handle<s::Recv<Ping, s::End>>, Wait...>;
static_assert(!HotHoldsRecv<>::W003_ok && !HotHoldsRecv<>::valid,
              "a hot binding that holds a handle at a receive and states no wait is refused");
static_assert(HotHoldsRecv<::fixy::atom::sync::spin_pause>::valid, "a stated spin is the hot-path wait");
static_assert(!HotHoldsRecv<::fixy::atom::sync::park>::W001_ok, "a stated kernel wait is refused by W001");

// ── The runner ───────────────────────────────────────────────────────

struct attack_case {
    std::string_view name;
    Outcome expected;
    void (*run)();
};

constexpr attack_case kAttacks[] = {
    {"drop_by_early_return", Outcome::Caught, drop_by_early_return},
    {"drop_by_optional_reset", Outcome::Caught, drop_by_optional_reset},
    {"drop_by_vector_clear", Outcome::Caught, drop_by_vector_clear},
    {"drop_by_unique_ptr_reset", Outcome::Caught, drop_by_unique_ptr_reset},
    {"drop_by_variant_reassign", Outcome::Caught, drop_by_variant_reassign},
    {"drop_by_move_only_function", Outcome::Caught, drop_by_move_only_function},
    {"drop_on_thread_exit", Outcome::Caught, drop_on_thread_exit},
    {"static_handle_at_exit", Outcome::Caught, static_handle_at_exit},
    {"assign_over_a_live_handle", Outcome::Caught, assign_over_a_live_handle},
    {"use_after_move", Outcome::Caught, use_after_move},
    {"close_twice", Outcome::Caught, close_twice},
    {"read_resource_after_move", Outcome::Caught, read_resource_after_move},
    {"view_a_moved_from_handle", Outcome::Caught, view_a_moved_from_handle},
    {"coroutine_destroyed_before_resume", Outcome::Caught, coroutine_destroyed_before_resume},
    {"leak_global_new_then_exit", Outcome::Caught, leak_global_new_then_exit},
    {"leak_released_holder_then_exit", Outcome::Caught, leak_released_holder_then_exit},
    {"leak_coroutine_frame_then_exit", Outcome::Caught, leak_coroutine_frame_then_exit},
    {"quick_exit_with_static_handle", Outcome::Caught, quick_exit_with_static_handle},
    {"one_thread_holds_both_ends_after_fork", Outcome::Caught, one_thread_holds_both_ends_after_fork},
    {"two_forked_sessions_in_a_cycle", Outcome::Caught, two_forked_sessions_in_a_cycle},
    {"wait_low_while_holding_high", Outcome::Caught, wait_low_while_holding_high},
    {"wait_high_while_holding_low", Outcome::Correct, wait_high_while_holding_low},
    {"swap_two_live_handles", Outcome::Correct, swap_two_live_handles},
    {"self_move_assign", Outcome::Correct, self_move_assign},
    {"cancellation_races_a_send", Outcome::Correct, cancellation_races_a_send},
    {"leak_then_skip_exit_hooks", Outcome::Silent, leak_then_skip_exit_hooks},
    {"detach_with_a_false_reason", Outcome::Silent, detach_with_a_false_reason},
    {"off_policy_drop", Outcome::Silent, off_policy_drop},
    {"off_policy_use_after_move", Outcome::Silent, off_policy_use_after_move},
    {"blocking_transport_hides_the_cycle", Outcome::Deadlock, blocking_transport_hides_the_cycle},
};

// Every attack that succeeds has a ledger row, and every ledger row
// names an attack that is expected to succeed.
[[nodiscard]] consteval bool ledger_matches_the_campaign() noexcept {
    for (const attack_case& attack : kAttacks) {
        const bool succeeds = attack.expected == Outcome::Silent || attack.expected == Outcome::Deadlock;
        bool listed = false;
        for (const limitation_row& row : known_limitations) {
            if (row.attack == attack.name) listed = true;
        }
        if (succeeds != listed) return false;
    }
    for (const limitation_row& row : known_limitations) {
        bool found = false;
        for (const attack_case& attack : kAttacks) {
            if (attack.name == row.attack) found = true;
        }
        if (!found || row.breaks.empty()) return false;
    }
    return true;
}
static_assert(ledger_matches_the_campaign(),
              "the ledger and the campaign disagree: a successful attack has no row, or a row names no successful "
              "attack");

[[nodiscard]] Outcome run_in_child(void (*attack)()) {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: an attack that the policy catches ends the
    // process, so it runs in a child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: attack harness, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        attack();
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: attack harness, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    if (WIFSIGNALED(status) != 0) return Outcome::Caught;
    const int code = WEXITSTATUS(status);
    if (code == kDeadlockExit) return Outcome::Deadlock;
    if (code == kCorrectExit) return Outcome::Correct;
    if (code == kSilentExit) return Outcome::Silent;
    std::fprintf(stderr, "an attack exited with code %d, which names no outcome\n", code);
    std::_Exit(2);
}

// ── The report at a fatal signal ─────────────────────────────────────
//
// Each case below ends the process on a signal that runs no exit hook.
// The child writes its standard error into a pipe, and the parent checks
// that the child did not exit cleanly and that the watch reported the
// live session.  Under AddressSanitizer a fault ends with the exit code of
// the sanitizer instead of the signal, so the check asks for a non-clean
// end and not for one signal.

[[noreturn]] void leak_then_abort() {
    [[maybe_unused]] auto* leaked = ::new FreshHandle{fresh()};
    std::abort();
}

[[noreturn]] void leak_then_segv() {
    [[maybe_unused]] auto* leaked = ::new FreshHandle{fresh()};
    static_cast<void>(std::raise(SIGSEGV));
    std::_Exit(kSilentExit);
}

[[noreturn]] void leak_then_bus_error() {
    [[maybe_unused]] auto* leaked = ::new FreshHandle{fresh()};
    static_cast<void>(std::raise(SIGBUS));
    std::_Exit(kSilentExit);
}

[[noreturn]] void leak_then_floating_point_error() {
    [[maybe_unused]] auto* leaked = ::new FreshHandle{fresh()};
    static_cast<void>(std::raise(SIGFPE));
    std::_Exit(kSilentExit);
}

// No session is live when the process aborts, so the report stays empty.
[[noreturn]] void abort_with_no_live_session() {
    auto handle = fresh();
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    std::abort();
}

struct captured_end {
    bool is_clean_exit = false;
    std::string output;
};

[[nodiscard]] captured_end run_capturing(void (*attack)()) {
    std::fflush(stderr);
    int channel[2] = {-1, -1};
    if (::pipe(channel) != 0) {
        std::fprintf(stderr, "pipe failed\n");
        std::_Exit(2);
    }
    // SPAWN-PROCESS-OK: the signal ends the process, so it runs in a
    // child whose standard error the parent reads.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: attack harness, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        ::close(channel[0]);
        ::dup2(channel[1], STDERR_FILENO);
        ::close(channel[1]);
        attack();
    }
    ::close(channel[1]);
    captured_end end{};
    std::array<char, 4096> buffer{};
    for (;;) {
        const ::ssize_t got = ::read(channel[0], buffer.data(), buffer.size());
        if (got <= 0) break;
        end.output.append(buffer.data(), static_cast<std::size_t>(got));
    }
    ::close(channel[0]);
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: attack harness, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    end.is_clean_exit = WIFEXITED(status) != 0 && WEXITSTATUS(status) == 0;
    return end;
}

struct signal_case {
    std::string_view name;
    bool expects_report;
    void (*run)();
};

constexpr signal_case kSignalCases[] = {
    {"leak_then_abort", true, leak_then_abort},
    {"leak_then_segv", true, leak_then_segv},
    {"leak_then_bus_error", true, leak_then_bus_error},
    {"leak_then_floating_point_error", true, leak_then_floating_point_error},
    {"abort_with_no_live_session", false, abort_with_no_live_session},
};

constexpr std::string_view kSignalReport = "LIVE PROTOCOL AT A FATAL SIGNAL";

[[nodiscard]] int run_signal_cases() {
    int failures = 0;
    for (const signal_case& entry : kSignalCases) {
        const captured_end end = run_capturing(entry.run);
        const bool has_report = end.output.find(kSignalReport) != std::string::npos
                             && end.output.find("live endpoint") != std::string::npos;
        const bool is_expected = !end.is_clean_exit && has_report == entry.expects_report;
        std::fprintf(stderr, "[signal] %-40.*s report %-3s %s\n", static_cast<int>(entry.name.size()),
                     entry.name.data(), has_report ? "yes" : "no", is_expected ? "as expected" : "UNEXPECTED");
        if (!is_expected) {
            ++failures;
            std::fprintf(stderr, "  child output:\n%s\n", end.output.c_str());
        }
    }
    return failures;
}

// ── Which check refused a wait ───────────────────────────────────────
//
// The priority order refuses each wait below before the thread waits, so
// the deadlock detector never runs for them.  The child's standard error
// names the check that refused the wait.

struct wait_refusal_case {
    std::string_view name;
    std::string_view diagnostic;
    void (*run)();
};

constexpr wait_refusal_case kWaitRefusals[] = {
    {"one_thread_holds_both_ends_after_fork", "[Wait_On_Held_Peer]", one_thread_holds_both_ends_after_fork},
    {"two_forked_sessions_in_a_cycle", "[Wait_Breaks_Priority_Order]", two_forked_sessions_in_a_cycle},
    {"wait_low_while_holding_high", "[Wait_Breaks_Priority_Order]", wait_low_while_holding_high},
};

[[nodiscard]] int run_wait_refusals() {
    int failures = 0;
    for (const wait_refusal_case& entry : kWaitRefusals) {
        const captured_end end = run_capturing(entry.run);
        const bool names_the_check = end.output.find(entry.diagnostic) != std::string::npos;
        const bool is_expected = !end.is_clean_exit && names_the_check
                              && end.output.find("DEADLOCK ACROSS SESSIONS") == std::string::npos;
        std::fprintf(stderr, "[wait] %-40.*s %s\n", static_cast<int>(entry.name.size()), entry.name.data(),
                     is_expected ? "refused before the wait" : "UNEXPECTED");
        if (!is_expected) {
            ++failures;
            std::fprintf(stderr, "  child output:\n%s\n", end.output.c_str());
        }
    }
    return failures;
}

[[nodiscard]] const char* outcome_name(Outcome outcome) noexcept {
    switch (outcome) {
        case Outcome::Caught: return "caught";
        case Outcome::Silent: return "silent";
        case Outcome::Deadlock: return "deadlock";
        case Outcome::Correct: return "correct";
        default: return "unknown";
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "[expected] the attack campaign below prints diagnostics from child processes\n");
    int failures = 0;
    std::size_t caught = 0;
    std::size_t drop_routes = 0;
    for (const attack_case& attack : kAttacks) {
        const Outcome seen = run_in_child(attack.run);
        std::fprintf(stderr, "[attack] %-40.*s expected %-8s seen %s\n", static_cast<int>(attack.name.size()),
                     attack.name.data(), outcome_name(attack.expected), outcome_name(seen));
        // The measure counts the routes that run under the default policy.
        // The two check::Off attacks measure the hatch, not the policy.
        const bool under_default_policy = !attack.name.starts_with("off_policy");
        if (under_default_policy && (attack.expected == Outcome::Caught || attack.expected == Outcome::Silent)) {
            ++drop_routes;
            if (seen == Outcome::Caught) ++caught;
        }
        if (seen != attack.expected) {
            ++failures;
            if (attack.expected == Outcome::Silent || attack.expected == Outcome::Deadlock) {
                std::fprintf(stderr, "  the attack no longer succeeds: remove its ledger row and lower the ceiling\n");
            } else {
                std::fprintf(stderr, "  the attack now succeeds: this is a new gap in the discipline\n");
            }
        }
    }
    std::fprintf(stderr, "[attack] the default policy caught %zu of %zu drop and reuse routes\n", caught, drop_routes);
    failures += run_signal_cases();
    failures += run_wait_refusals();
    return failures == 0 ? 0 : 1;
}
