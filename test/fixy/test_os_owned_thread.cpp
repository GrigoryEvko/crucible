// Tests of the owned thread of fixy/os/OwnedThread.h.  Each test starts one
// thread through mint_owned_thread and ends it in one of the ways that the
// owner gives: an explicit join, the end of the owner, a move and then a
// join, and the end of an Option that holds the owner.  The thread of an
// owner ends when its owner ends or stops it, while each other member of the
// owner is alive.  The tsan preset runs the same binary.

#include <fixy/os/OwnedThread.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

#include <cstdint>
#include <utility>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the C library reports the attributes of the calling thread")
#include <pthread.h>

namespace {

// The 16 bytes of the name of the calling thread, in two words.
struct NameWords {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
};

NameWords read_calling_thread_name() noexcept {
    char name[16]{};
    static_cast<void>(::pthread_getname_np(::pthread_self(), name, sizeof(name)));
    NameWords words{};
    __builtin_memcpy(&words.low, name, 8);
    __builtin_memcpy(&words.high, name + 8, 8);
    return words;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// An owner moves to the thread that joins it.  Two threads do not read one
// owner, because the holder empties it on a join (foundation/Transfer.h).
static_assert(::foundation::transfer::Sendable<::fixy::thread::OwnedThread>
              && !::foundation::transfer::Shareable<::fixy::thread::OwnedThread>);

// The owner holds the handle, the frame and the end of the frame, and the
// owner core adds no byte.
static_assert(sizeof(::fixy::thread::OwnedThread) == sizeof(std::uint64_t) + 2 * sizeof(void*));

namespace {

namespace eff = ::foundation::effects;
using ::foundation::core::Atomic;
using ::foundation::os::Errno;
using ::fixy::thread::OwnedThread;
using ::fixy::thread::ThreadOptions;

using StarterCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

constinit thread_local bool is_main_thread = false;

// The cells that the bodies of the cases write.  The owned thread escapes the
// frame that starts it, so a body captures nothing of that frame and writes
// only these cells of static storage.  Each case first resets the cells that
// it reads, and the cases run one after another.
constinit Atomic<bool> has_run{false};
constinit Atomic<bool> ran_on_main{true};
constinit Atomic<bool> may_finish{false};
constinit Atomic<bool> has_finished{false};
constinit Atomic<bool> may_read{false};
constinit Atomic<std::uint64_t> seen_low{0};
constinit Atomic<std::uint64_t> seen_high{0};
constinit Atomic<std::uint32_t> body_ends{0};
constinit Atomic<bool> has_canary_ended{false};
constinit Atomic<bool> did_body_see_live_members{false};
constinit Atomic<std::uint32_t> stop_hook_calls{0};
constinit Atomic<std::uint32_t> consumed_value{0};

void reset_cells() noexcept {
    has_run.store_release(false);
    ran_on_main.store_release(true);
    may_finish.store_release(false);
    has_finished.store_release(false);
    may_read.store_release(false);
    seen_low.store_release(0);
    seen_high.store_release(0);
    body_ends.store_release(0);
    has_canary_ended.store_release(false);
    did_body_see_live_members.store_release(false);
    stop_hook_calls.store_release(0);
    consumed_value.store_release(0);
}

// A linear value that a body holds by value: a refused start must give the
// body back, so no door drops the token in silence.
class[[= ::foundation::linear::linear{}]] LinearToken {
public:
    constexpr explicit LinearToken(std::uint32_t value) noexcept : value_{value} {}
    LinearToken(LinearToken const&) = delete("a linear token has one owner");
    LinearToken& operator=(LinearToken const&) = delete("a linear token has one owner");
    constexpr LinearToken(LinearToken&&) noexcept = default;
    constexpr LinearToken& operator=(LinearToken&&) noexcept = default;
    constexpr ~LinearToken() = default;

    [[nodiscard]] constexpr std::uint32_t consume() && noexcept { return value_; }

private:
    std::uint32_t value_ = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

[[nodiscard]] OwnedThread start_or_end(::foundation::core::Result<OwnedThread, Errno>&& started) noexcept {
    return std::move(started).expect("the thread door starts a thread with the default options");
}

// The body runs on a thread of its own, and the join waits for its end.
void test_join_waits_for_the_body() {
    reset_cells();
    OwnedThread thread =
        start_or_end(::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, []() noexcept {
            ran_on_main.store_release(is_main_thread);
            has_run.store_release(true);
        }));
    CRUCIBLE_FATAL_INVARIANT(thread.holds_thread());
    std::move(thread).join();
    CRUCIBLE_FATAL_INVARIANT(has_run.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(!ran_on_main.load_acquire());
}

// The body waits for the main thread, so the thread still runs when the
// mint returns.  The end of the owner joins it.
void test_end_of_the_owner_joins() {
    reset_cells();
    {
        OwnedThread thread = start_or_end(
            ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, []() noexcept {
                while (!may_finish.load_acquire())
                    CRUCIBLE_SPIN_PAUSE;
                has_finished.store_release(true);
            }));
        CRUCIBLE_FATAL_INVARIANT(!has_finished.load_acquire());
        may_finish.store_release(true);
    }
    CRUCIBLE_FATAL_INVARIANT(has_finished.load_acquire());
}

// A move gives the thread to the target and leaves the source empty.  A join
// of the empty source ends the process.
void test_move_takes_the_source_first() {
    reset_cells();
    OwnedThread source =
        start_or_end(::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, []() noexcept {
            while (!may_finish.load_acquire())
                CRUCIBLE_SPIN_PAUSE;
            has_finished.store_release(true);
        }));
    OwnedThread target{std::move(source)};
    CRUCIBLE_FATAL_INVARIANT(target.holds_thread());
    CRUCIBLE_FATAL_INVARIANT(!has_finished.load_acquire());
    may_finish.store_release(true);
    std::move(target).join();
    CRUCIBLE_FATAL_INVARIANT(has_finished.load_acquire());
}

// An Option of the owner takes the thread with no move assignment of the
// owner, and the end of the Option joins the thread.
void test_option_holds_the_owner() {
    reset_cells();
    {
        ::foundation::core::Option<OwnedThread> slot{};
        slot = ::foundation::core::Option<OwnedThread>::some(start_or_end(::fixy::thread::mint_owned_thread(
            StarterCtx{eff::testing::bg()}, ThreadOptions{}, []() noexcept { has_finished.store_release(true); })));
        CRUCIBLE_FATAL_INVARIANT(slot.is_some());
    }
    CRUCIBLE_FATAL_INVARIANT(has_finished.load_acquire());
}

// The frame keeps a copy of the name, so the thread takes its name after
// the options of the call end.
void test_name_outlives_the_options() {
    reset_cells();
    OwnedThread thread = []() noexcept {
        ThreadOptions const options{.name = "owned-probe"};
        return start_or_end(::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, options, []() noexcept {
            while (!may_read.load_acquire())
                CRUCIBLE_SPIN_PAUSE;
            NameWords const seen = read_calling_thread_name();
            seen_low.store_release(seen.low);
            seen_high.store_release(seen.high);
        }));
    }();
    may_read.store_release(true);
    std::move(thread).join();
    CRUCIBLE_FATAL_INVARIANT(seen_low.load_acquire() == 0x72702d64656e776fULL);  // "owned-pr"
    CRUCIBLE_FATAL_INVARIANT(seen_high.load_acquire() == 0x000000000065626fULL);  // "obe"
}

// The body that a refused start never runs.  The mint ends it one time.  A
// moved-from body is not live, so only the last body of a chain of moves
// counts its end.
struct CountedBody {
    bool is_live = true;

    CountedBody() noexcept = default;
    CountedBody(CountedBody&& other) noexcept : is_live{other.is_live} { other.is_live = false; }
    CountedBody(CountedBody const&) = delete("the test counts each body that ends");
    CountedBody& operator=(CountedBody const&) = delete("the test counts each body that ends");
    CountedBody& operator=(CountedBody&&) = delete("the test counts each body that ends");
    ~CountedBody() {
        if (is_live) static_cast<void>(body_ends.fetch_add_acq_rel(1));
    }

    void operator()() noexcept { has_run.store_release(true); }
};

// An option that is not valid is the error of the mint.  No thread runs,
// and the body ends one time.
void test_refused_start_runs_no_body() {
    reset_cells();
    auto const refused = ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()},
                                                           ThreadOptions{.guard_bytes = 0}, CountedBody{});
    CRUCIBLE_FATAL_INVARIANT(refused.is_err());
    CRUCIBLE_FATAL_INVARIANT(refused.err().value_or(Errno::eperm) == Errno::einval);
    CRUCIBLE_FATAL_INVARIANT(!has_run.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(body_ends.load_acquire() == 1);
}

// A refused start of a body that holds a linear value gives the body back in
// the error.  The case starts the body that came back with the default
// options, and the body consumes its token on its own thread.
void test_refused_start_gives_back_a_linear_body() {
    reset_cells();
    auto body = [token = LinearToken{7}]() mutable noexcept {
        consumed_value.store_release(std::move(token).consume());
    };
    using Refused = ::fixy::thread::RefusedStart<decltype(body)>;
    auto refused = ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{.guard_bytes = 0},
                                                     std::move(body));
    // An OwnedThread holds the address of its frame, so the arms join the
    // thread and give a plain value.
    std::uint32_t const restarted_value = std::move(refused).match(
        [](OwnedThread) noexcept -> std::uint32_t {
            ::foundation::core::fatal("the mint refuses a guard of zero bytes");
        },
        [](Refused refusal) noexcept -> std::uint32_t {
            CRUCIBLE_FATAL_INVARIANT(refusal.error == Errno::einval);
            auto [unrun] = std::move(refusal.parts);
            ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, std::move(unrun))
                .expect("the mint starts the body that came back with the default options")
                .join();
            return consumed_value.load_acquire();
        });
    CRUCIBLE_FATAL_INVARIANT(restarted_value == 7);
}

// A member whose end the body of the thread of its owner must not see.
struct Canary {
    Canary() noexcept = default;
    Canary(Canary const&) = delete("the test watches one canary");
    Canary& operator=(Canary const&) = delete("the test watches one canary");
    ~Canary() { has_canary_ended.store_release(true); }
};

// An owner whose body loops over its members until the stop hook ends the
// loop (the thread of an owner of fixy/os/OwnedThread.h).  The thread is the
// last member, so it ends before the canary and the cells of the loop.  The
// body and the starter use only the atomic cells, and each member is
// private, so the owner states synchronized{} (foundation/Transfer.h).
class[[= ::foundation::transfer::synchronized{}]] LoopingOwner {
public:
    LoopingOwner() noexcept = default;
    LoopingOwner(LoopingOwner const&) = delete("the thread borrows its owner at one address");
    LoopingOwner& operator=(LoopingOwner const&) = delete("the thread borrows its owner at one address");
    LoopingOwner(LoopingOwner&&) = delete("the thread borrows its owner at one address");
    LoopingOwner& operator=(LoopingOwner&&) = delete("the thread borrows its owner at one address");
    ~LoopingOwner() = default;

    // The key of the doors that start and stop the thread.  The key stays on
    // the thread that claimed it (the proof of a door of foundation/Transfer.h).
    // Its constructor is private, so only claim_starter builds one (the build
    // of a token).
    class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] StarterKey final {
        StarterKey() noexcept {}
        friend class LoopingOwner;

    public:
        StarterKey(StarterKey const&) = delete("a key names the one starter of the owner");
        StarterKey& operator=(StarterKey const&) = delete("a key names the one starter of the owner");
        ~StarterKey() = default;
    };

    // Gives the key to the calling thread.  One atomic exchange gives it to
    // one thread for the life of the owner, and a second claim ends the
    // process.  A claim door: only the thread that won the exchange starts
    // and stops the thread (the doors of a synchronized class of
    // foundation/Transfer.h).
    [[nodiscard]] StarterKey claim_starter() noexcept {
        if (is_starter_claimed_.exchange_acq_rel(true)) {
            ::foundation::core::fatal("a second claim of the starter key of one owner");
        }
        return StarterKey{};
    }

    // Starts the thread with the options, and waits until the body loops.
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, Errno>
    start(StarterKey& /*key*/, ThreadOptions const& options) noexcept {
        should_stop_.store_release(false);
        has_looped_.store_release(false);
        ::foundation::core::Result<::foundation::core::Unit, Errno> started =
            ::fixy::thread::mint_owner_thread<&LoopingOwner::thread_, &LoopingOwner::request_stop_>(
                StarterCtx{eff::testing::bg()}, options, *this, [](LoopingOwner& owner) noexcept { owner.run_(); });
        if (started.is_ok()) {
            while (!has_looped_.load_acquire())
                CRUCIBLE_SPIN_PAUSE;
        }
        return started;
    }

    // Starts a body that holds a linear value with an option that the door
    // refuses, and ends the body that came back.  Gives the error number.
    [[nodiscard]] Errno refuse_a_linear_body(StarterKey& /*key*/) noexcept {
        auto body = [token = LinearToken{9}](LoopingOwner& owner) mutable noexcept {
            consumed_value.store_release(std::move(token).consume());
            owner.run_();
        };
        using Refused = ::fixy::thread::RefusedStart<decltype(body)>;
        return ::fixy::thread::mint_owner_thread<&LoopingOwner::thread_, &LoopingOwner::request_stop_>(
                   StarterCtx{eff::testing::bg()}, ThreadOptions{.guard_bytes = 0}, *this, std::move(body))
            .match(
                [](::foundation::core::Unit) noexcept -> Errno {
                    ::foundation::core::fatal("the door refuses a guard of zero bytes");
                },
                [](Refused refusal) noexcept -> Errno {
                    auto [unrun] = std::move(refusal.parts);
                    ::foundation::linear::end_unrun_body(std::move(unrun),
                                                         ::foundation::linear::UnrunReason::refusal_test);
                    return refusal.error;
                });
    }

    void stop(StarterKey& /*key*/) noexcept { thread_.stop_and_join(); }

    [[nodiscard]] bool holds_thread() const noexcept { return thread_.holds_thread(); }

private:
    void run_() noexcept {
        while (!should_stop_.load_acquire()) {
            static_cast<void>(laps_.fetch_add_acq_rel(1));
            has_looped_.store_release(true);
            CRUCIBLE_SPIN_PAUSE;
        }
        did_body_see_live_members.store_release(!has_canary_ended.load_acquire() && laps_.load_acquire() > 0);
    }

    void request_stop_() noexcept {
        static_cast<void>(stop_hook_calls.fetch_add_acq_rel(1));
        should_stop_.store_release(true);
    }

    Canary canary_{};
    Atomic<bool> is_starter_claimed_{false};
    Atomic<bool> should_stop_{false};
    Atomic<bool> has_looped_{false};
    Atomic<std::uint64_t> laps_{0};
    ::fixy::thread::OwnerThread<LoopingOwner> thread_{};
};

// The OwnerThread moves and is read exactly as its owner is, because its
// pointer names the object that holds it (by_arguments{} of
// foundation/Transfer.h).
static_assert(::foundation::transfer::Sendable<LoopingOwner>
              && ::foundation::transfer::Sendable<::fixy::thread::OwnerThread<LoopingOwner>>);

// The end of an owner whose body still loops calls the stop hook and joins
// before any other member ends, so the body sees each member alive to its
// end.
void test_end_of_an_owner_stops_its_thread_first() {
    reset_cells();
    {
        LoopingOwner owner{};
        LoopingOwner::StarterKey key = owner.claim_starter();
        owner.start(key, ThreadOptions{}).expect("the door starts the thread of an owner with the default options");
        CRUCIBLE_FATAL_INVARIANT(owner.holds_thread());
    }
    CRUCIBLE_FATAL_INVARIANT(did_body_see_live_members.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(has_canary_ended.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(stop_hook_calls.load_acquire() == 1);
}

// stop_and_join ends the thread at an earlier line and leaves the member
// empty, so the end of the owner calls the hook no more.  The owner can start
// a new thread after it.
void test_early_stop_empties_the_member() {
    reset_cells();
    {
        LoopingOwner owner{};
        LoopingOwner::StarterKey key = owner.claim_starter();
        owner.start(key, ThreadOptions{}).expect("the door starts the thread of an owner with the default options");
        owner.stop(key);
        CRUCIBLE_FATAL_INVARIANT(!owner.holds_thread());
        CRUCIBLE_FATAL_INVARIANT(stop_hook_calls.load_acquire() == 1);
        owner.start(key, ThreadOptions{}).expect("the door starts a second thread after the first one joined");
        owner.stop(key);
        CRUCIBLE_FATAL_INVARIANT(stop_hook_calls.load_acquire() == 2);
    }
    CRUCIBLE_FATAL_INVARIANT(stop_hook_calls.load_acquire() == 2);
    CRUCIBLE_FATAL_INVARIANT(did_body_see_live_members.load_acquire());
}

// A refused option is the error of the door, and the member stays empty.
void test_refused_start_leaves_the_member_empty() {
    reset_cells();
    LoopingOwner owner{};
    LoopingOwner::StarterKey key = owner.claim_starter();
    auto const refused = owner.start(key, ThreadOptions{.guard_bytes = 0});
    CRUCIBLE_FATAL_INVARIANT(refused.err().value_or(Errno::eperm) == Errno::einval);
    CRUCIBLE_FATAL_INVARIANT(!owner.holds_thread());
    CRUCIBLE_FATAL_INVARIANT(stop_hook_calls.load_acquire() == 0);
}

// A refused start of the thread of an owner with a body that holds a linear
// value gives the body back in the error, and no body runs.
void test_refused_owner_start_gives_back_a_linear_body() {
    reset_cells();
    LoopingOwner owner{};
    LoopingOwner::StarterKey key = owner.claim_starter();
    CRUCIBLE_FATAL_INVARIANT(owner.refuse_a_linear_body(key) == Errno::einval);
    CRUCIBLE_FATAL_INVARIANT(!owner.holds_thread());
    CRUCIBLE_FATAL_INVARIANT(consumed_value.load_acquire() == 0);
    CRUCIBLE_FATAL_INVARIANT(stop_hook_calls.load_acquire() == 0);
}

}  // namespace

int main() {
    is_main_thread = true;
    test_join_waits_for_the_body();
    test_end_of_the_owner_joins();
    test_move_takes_the_source_first();
    test_option_holds_the_owner();
    test_name_outlives_the_options();
    test_refused_start_runs_no_body();
    test_refused_start_gives_back_a_linear_body();
    test_end_of_an_owner_stops_its_thread_first();
    test_early_stop_empties_the_member();
    test_refused_start_leaves_the_member_empty();
    test_refused_owner_start_gives_back_a_linear_body();
    return 0;
}
