// Tests of the owned thread of fixy/os/OwnedThread.h.  Each test starts one
// thread through mint_owned_thread and ends it in one of the ways that the
// owner gives: an explicit join, the end of the owner, a move and then a
// join, and the end of an Option that holds the owner.  The tsan preset runs
// the same binary.

#include <fixy/Borrowed.h>
#include <fixy/os/OwnedThread.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>
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

namespace {

namespace eff = ::foundation::effects;
using ::foundation::core::Atomic;
using ::foundation::os::Errno;
using ::fixy::thread::OwnedThread;
using ::fixy::thread::ThreadOptions;

using StarterCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

constinit thread_local bool is_main_thread = false;

[[nodiscard]] OwnedThread start_or_end(::foundation::core::Result<OwnedThread, Errno>&& started) noexcept {
    return std::move(started).expect("the thread door starts a thread with the default options");
}

// The body runs on a thread of its own, and the join waits for its end.
void test_join_waits_for_the_body() {
    Atomic<bool> has_run{false};
    Atomic<bool> ran_on_main{true};
    OwnedThread thread =
        start_or_end(::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, [&]() noexcept {
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
    Atomic<bool> may_finish{false};
    Atomic<bool> has_finished{false};
    {
        OwnedThread thread = start_or_end(
            ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, [&]() noexcept {
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
    Atomic<bool> may_finish{false};
    Atomic<bool> has_finished{false};
    OwnedThread source =
        start_or_end(::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, ThreadOptions{}, [&]() noexcept {
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
    Atomic<bool> has_finished{false};
    {
        ::foundation::core::Option<OwnedThread> slot{};
        slot = ::foundation::core::Option<OwnedThread>::some(start_or_end(::fixy::thread::mint_owned_thread(
            StarterCtx{eff::testing::bg()}, ThreadOptions{}, [&]() noexcept { has_finished.store_release(true); })));
        CRUCIBLE_FATAL_INVARIANT(slot.is_some());
    }
    CRUCIBLE_FATAL_INVARIANT(has_finished.load_acquire());
}

// The frame keeps a copy of the name, so the thread takes its name after
// the options of the call end.
void test_name_outlives_the_options() {
    Atomic<bool> may_read{false};
    NameWords seen{};
    OwnedThread thread = [&]() noexcept {
        ThreadOptions const options{.name = "owned-probe"};
        return start_or_end(::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()}, options, [&]() noexcept {
            while (!may_read.load_acquire())
                CRUCIBLE_SPIN_PAUSE;
            seen = read_calling_thread_name();
        }));
    }();
    may_read.store_release(true);
    std::move(thread).join();
    CRUCIBLE_FATAL_INVARIANT(seen.low == 0x72702d64656e776fULL);  // "owned-pr"
    CRUCIBLE_FATAL_INVARIANT(seen.high == 0x000000000065626fULL);  // "obe"
}

// The body that a refused start never runs.  The mint ends it one time.
struct CountedBody {
    ::fixy::WeakRef<Atomic<bool>> has_run{};
    ::fixy::WeakRef<Atomic<std::uint32_t>> ends{};

    CountedBody(Atomic<bool>& run_flag, Atomic<std::uint32_t>& end_count) noexcept
        : has_run{run_flag}, ends{end_count} {}
    CountedBody(CountedBody&& other) noexcept : has_run{other.has_run}, ends{other.ends} {
        other.has_run = ::fixy::WeakRef<Atomic<bool>>{};
        other.ends = ::fixy::WeakRef<Atomic<std::uint32_t>>{};
    }
    CountedBody(CountedBody const&) = delete("the test counts each body that ends");
    CountedBody& operator=(CountedBody const&) = delete("the test counts each body that ends");
    CountedBody& operator=(CountedBody&&) = delete("the test counts each body that ends");
    ~CountedBody() {
        if (ends.has_value()) static_cast<void>(ends->fetch_add_acq_rel(1));
    }

    void operator()() noexcept { has_run->store_release(true); }
};

// An option that is not valid is the error of the mint.  No thread runs,
// and the body ends one time.
void test_refused_start_runs_no_body() {
    Atomic<bool> has_run{false};
    Atomic<std::uint32_t> ends{0};
    auto const refused = ::fixy::thread::mint_owned_thread(StarterCtx{eff::testing::bg()},
                                                           ThreadOptions{.guard_bytes = 0}, CountedBody{has_run, ends});
    CRUCIBLE_FATAL_INVARIANT(refused.is_err());
    CRUCIBLE_FATAL_INVARIANT(refused.err().value_or(Errno::eperm) == Errno::einval);
    CRUCIBLE_FATAL_INVARIANT(!has_run.load_acquire());
    CRUCIBLE_FATAL_INVARIANT(ends.load_acquire() == 1);
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
    return 0;
}
