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
// The compile-time attacks are negative fixtures under test/fixy/neg,
// named neg_sess_* and neg_rule_r004_*, and one is pinned here as a
// static_assert: a hot binding that waits in a receive compiles.

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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
    {"leak_new_without_delete",
     "linearity: storage that is never destroyed never runs the destructor, so no policy sees the drop"},
    {"leak_released_unique_ptr",
     "linearity: unique_ptr::release gives up the only owner, and the handle is never destroyed"},
    {"leak_coroutine_frame",
     "linearity: a coroutine frame that is never destroyed holds a live handle that no destructor reaches"},
    {"quick_exit_with_static_handle",
     "linearity: quick_exit skips the destructors of objects with static storage duration"},
    {"detach_with_a_false_reason",
     "linearity: detach is a named escape, and a false reason drops the protocol without a diagnostic"},
    {"off_policy_drop", "linearity: check::Off is an explicit hatch that ignores a dropped protocol"},
    {"off_policy_use_after_move",
     "linearity: check::Off has no liveness flag, so a moved-from handle steps the protocol again"},
    {"one_thread_holds_both_ends_after_fork",
     "forest: a body moves its endpoint through shared memory to the thread that holds the dual"},
    {"two_forked_sessions_in_a_cycle",
     "forest: two forked sessions exchange endpoints through shared memory, and the waits form a cycle"},
};

// The ledger can only shrink.  Lower this number when a row leaves.
constexpr std::size_t kLedgerCeiling = 9;
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

[[noreturn]] void drop_by_unique_ptr_reset() {
    auto owner = std::make_unique<FreshHandle>(fresh());
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

// ── Attacks that succeed ─────────────────────────────────────────────

[[noreturn]] void leak_new_without_delete() {
    [[maybe_unused]] auto* leaked = new FreshHandle{fresh()};
    std::_Exit(kSilentExit);
}

[[noreturn]] void leak_released_unique_ptr() {
    auto owner = std::make_unique<FreshHandle>(fresh());
    [[maybe_unused]] FreshHandle* released = owner.release();
    std::_Exit(kSilentExit);
}

[[noreturn]] void leak_coroutine_frame() {
    Task task = hold_across_suspension(fresh());
    task.abandon();
    std::_Exit(kSilentExit);
}

[[noreturn]] void quick_exit_with_static_handle() {
    static FreshHandle kept = fresh();
    (void)kept;
    std::quick_exit(kSilentExit);
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

// The left body takes the right endpoint, so one thread holds the two
// ends of one channel.  It then waits on its own end first, for the Ping
// that only its other end can send.
struct TakesBothBody {
    template <typename Head>
    auto operator()(Head head, perm::Permission<attack_tags::Left>, BgCtx const&) noexcept {
        RightHead other = g_right_endpoint.take("the left body, for the right endpoint");
        auto [ping, done] = std::move(head).recv([](LeftEnd& e) noexcept { return Ping{take(e.pipe->to_left, "left recv")}; });
        g_right_done.put(std::move(other).send(Ping{ping.value}, [](RightEnd& e, Ping&& p) noexcept {
            put(e.pipe->to_left, p.value);
        }));
        return std::move(done);
    }
};

[[noreturn]] void one_thread_holds_both_ends_after_fork() {
    Pipe pipe{};
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_channel<LeftProto, attack_tags::Left, attack_tags::Right>(
        ctx, perm::mint_permission_root<attack_tags::Whole>(), LeftEnd{&pipe}, RightEnd{&pipe}, TakesBothBody{},
        GiveAwayBody{});
    perm::permission_drop(std::move(back));
    std::_Exit(kSilentExit);
}

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
        auto [ping, done] = std::move(head).recv([](LeftEnd& e) noexcept { return Ping{take(e.pipe->to_left, "A recv")}; });
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
        auto [ping, done] = std::move(head).recv([](LeftEnd& e) noexcept { return Ping{take(e.pipe->to_left, "B recv")}; });
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
// A hot binding that holds a handle at a receive compiles.  The wait is
// the transport's, and the binding states no wait strategy, so W001 has
// nothing to read.  This is not in the runtime ledger because nothing
// runs; it is pinned here so that a rule for it replaces the pin.
struct hot_invariant final {};
using HotWaitsInRecv =
    ::fixy::collision::live_rules<::fixy::atom::regime::hot, ::fixy::atom::cost_constant,
                                  ::fixy::atom::refined_with<hot_invariant>, ::fixy::atom::as_public,
                                  ::fixy::atom::session::live_handle<s::Recv<Ping, s::End>>>;
static_assert(HotWaitsInRecv::valid,
              "a hot binding that holds a handle at a receive is admitted: no rule reads the wait of a transport");

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
    {"swap_two_live_handles", Outcome::Correct, swap_two_live_handles},
    {"self_move_assign", Outcome::Correct, self_move_assign},
    {"cancellation_races_a_send", Outcome::Correct, cancellation_races_a_send},
    {"leak_new_without_delete", Outcome::Silent, leak_new_without_delete},
    {"leak_released_unique_ptr", Outcome::Silent, leak_released_unique_ptr},
    {"leak_coroutine_frame", Outcome::Silent, leak_coroutine_frame},
    {"quick_exit_with_static_handle", Outcome::Silent, quick_exit_with_static_handle},
    {"detach_with_a_false_reason", Outcome::Silent, detach_with_a_false_reason},
    {"off_policy_drop", Outcome::Silent, off_policy_drop},
    {"off_policy_use_after_move", Outcome::Silent, off_policy_use_after_move},
    {"one_thread_holds_both_ends_after_fork", Outcome::Deadlock, one_thread_holds_both_ends_after_fork},
    {"two_forked_sessions_in_a_cycle", Outcome::Deadlock, two_forked_sessions_in_a_cycle},
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
    return failures == 0 ? 0 : 1;
}
