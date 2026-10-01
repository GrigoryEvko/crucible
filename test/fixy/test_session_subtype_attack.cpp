// Attacks on the protocol algebra and on subtyping, through correct use
// of the public surface only: no cast, no reopened namespace outside a
// documented registry, no undefined behaviour.  Each attack either
// fails, and the test pins the failure, or it succeeds, and the test
// pins the success on the known-limitation ledger at the end.  The
// ledger only shrinks: an entry whose attack no longer works fails its
// pin, and the entry goes.
//
// The runtime attacks run two threads over two bounded queues.  A
// watchdog ends a run that makes no progress for a fixed time and
// records a deadlock, so no attack can hang the test.
//
// The test is several source files of one executable, so that no
// translation unit holds every attack:
//
//   session_subtype_attack.h      the shared part
//   this file                     the family of anticipations, the
//                                 runner, the payload sizes and main
//   ..._recursion.cpp             the recursion games
//   ..._generated.cpp             the generated anticipations and their
//                                 runs
//   ..._choices.cpp               the empty choices, the labels and the
//                                 crash branches
//   ..._keyed.cpp                 the wire word of a keyed branch, on one
//                                 thread and on two
//   ..._fuel.cpp                  the fuel of the bounded search
//   ..._ledger.cpp                the known-limitation ledger

#include "session_subtype_attack.h"

#include <foundation/Platform.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace test_session_subtype_attack_types {

template <std::size_t K, std::size_t C>
inline constexpr bool family_verdict_v = s::is_subtype_async_v<early<K>, late<K>, ring<C>>;

// The check admits exactly the capacities that hold the anticipation:
// K from 1 to 4, capacity from 1 to 5.
template <std::size_t K, std::size_t... Cs>
consteval bool row_matches_capacity(std::index_sequence<Cs...>) {
    return ((family_verdict_v<K, Cs + 1> == (Cs + 1 >= K)) && ...);
}
template <std::size_t... Ks>
consteval bool family_matches_capacity(std::index_sequence<Ks...>) {
    return (row_matches_capacity<Ks + 1>(std::make_index_sequence<5>{}) && ...);
}
static_assert(family_matches_capacity(std::make_index_sequence<4>{}),
              "an off-by-one capacity is refused, one step in either direction");

// ── A bounded channel and a runner with a watchdog ───────────────────

inline constexpr std::size_t max_capacity = 8;

class bounded_queue {
public:
    explicit bounded_queue(std::size_t capacity) : capacity_{capacity} {}

    bool try_push(int value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= capacity_) return false;
        slots_[head % max_capacity] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(int& value) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        value = slots_[tail % max_capacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool is_empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    std::array<int, max_capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t capacity_;
};

// The state of one side of a run.
enum class side : int {
    running,
    waiting,
    finished
};

// One wait of a side for its queue.  The first waits of a step spin with
// a pause, the next waits give the processor to another thread, and the
// remaining waits sleep.  A side that waits through a deadlock then
// sleeps through the stall ticks, and it does not make a system call in
// a tight loop.  Each step starts its count at zero.
inline void wait_for_queue(std::uint32_t& waits_of_step) {
    constexpr std::uint32_t pause_waits = 64;
    constexpr std::uint32_t yield_waits = 256;
    if (waits_of_step < pause_waits) {
        CRUCIBLE_SPIN_PAUSE;
    } else if (waits_of_step < pause_waits + yield_waits) {
        std::this_thread::yield();
    } else {
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
    if (waits_of_step < pause_waits + yield_waits) ++waits_of_step;
}

// Runs the two scripts against each other.  A run is a deadlock when
// each side that has not finished waits, and no side makes progress for
// `stall_ticks`: a waiting side waits for the other side to act, and
// the other side waits too.  The watchdog then raises the stop flag,
// each thread leaves its wait, and both joins return.  A run with no
// progress for `hard_ticks` while a side still runs is a fault of this
// harness, and the test aborts with a diagnostic.
outcome run_pair(std::span<const step> left, std::span<const step> right, std::size_t capacity) {
    bounded_queue left_to_right{capacity};
    bounded_queue right_to_left{capacity};
    std::atomic<std::uint64_t> progress{0};
    std::atomic<bool> stop{false};
    std::atomic<int> finished{0};
    std::atomic<bool> wrong{false};
    std::array<std::atomic<side>, 2> sides{};
    const auto play = [&](std::span<const step> script, bounded_queue& out, bounded_queue& in,
                          std::atomic<side>& state) {
        for (const step& action : script) {
            state.store(side::waiting, std::memory_order_release);
            std::uint32_t waits_of_step = 0;
            if (action.is_send) {
                while (!out.try_push(action.message)) {
                    if (stop.load(std::memory_order_acquire)) return;
                    wait_for_queue(waits_of_step);
                }
            } else {
                int value = 0;
                while (!in.try_pop(value)) {
                    if (stop.load(std::memory_order_acquire)) return;
                    wait_for_queue(waits_of_step);
                }
                if (value != action.message) wrong.store(true, std::memory_order_release);
            }
            state.store(side::running, std::memory_order_release);
            progress.fetch_add(1, std::memory_order_acq_rel);
        }
        state.store(side::finished, std::memory_order_release);
        finished.fetch_add(1, std::memory_order_acq_rel);
    };
    bool is_deadlocked = false;
    {
        std::jthread first{[&] { play(left, left_to_right, right_to_left, sides[0]); }};
        std::jthread second{[&] { play(right, right_to_left, left_to_right, sides[1]); }};
        constexpr auto tick = std::chrono::milliseconds{5};
        constexpr int stall_ticks = 40;
        constexpr int hard_ticks = 2000;
        std::uint64_t last = progress.load(std::memory_order_acquire);
        int quiet = 0;
        while (finished.load(std::memory_order_acquire) < 2) {
            std::this_thread::sleep_for(tick);
            const std::uint64_t now = progress.load(std::memory_order_acquire);
            quiet = now == last ? quiet + 1 : 0;
            last = now;
            const bool is_blocked = sides[0].load(std::memory_order_acquire) != side::running
                                 && sides[1].load(std::memory_order_acquire) != side::running;
            if (is_blocked && quiet >= stall_ticks) {
                is_deadlocked = true;
                stop.store(true, std::memory_order_release);
                break;
            }
            if (quiet >= hard_ticks) {
                std::fprintf(stderr,
                             "test_session_subtype_attack: a run made no progress for %d ticks while a side "
                             "still ran; the harness is wrong\n",
                             hard_ticks);
                std::abort();
            }
        }
    }
    if (is_deadlocked) return outcome::deadlocked;
    if (wrong.load(std::memory_order_acquire)) return outcome::wrong_message;
    if (!left_to_right.is_empty() || !right_to_left.is_empty()) return outcome::orphan;
    return outcome::completed;
}

namespace {

int failures = 0;

}  // namespace

void expect(bool condition, std::string_view what) {
    if (condition) return;
    std::fprintf(stderr, "test_session_subtype_attack: %.*s\n", static_cast<int>(what.size()), what.data());
    ++failures;
}

// The check and the runtime agree on each member of the family, for
// every capacity from 1 to 4: the check admits exactly the runs that
// complete.
template <std::size_t K>
void family_agrees_with_runtime() {
    for (std::size_t capacity = 1; capacity <= 4; ++capacity) {
        const outcome observed = run_against_dual<early<K>, late<K>>(capacity);
        const bool admitted = capacity >= K;
        const bool completed = observed == outcome::completed;
        if (admitted != completed) {
            std::fprintf(stderr, "family K=%zu capacity=%zu: the check %s, the run %.*s\n", K, capacity,
                         admitted ? "admits" : "refuses", static_cast<int>(outcome_name(observed).size()),
                         outcome_name(observed).data());
            ++failures;
        }
    }
}

}  // namespace test_session_subtype_attack_types

using namespace test_session_subtype_attack_types;

int main() {
    // The runtime half of the capacity family: the check and the runs agree.
    family_agrees_with_runtime<1>();
    family_agrees_with_runtime<2>();
    family_agrees_with_runtime<3>();
    family_agrees_with_runtime<4>();

    // Every pair the synchronous relation holds runs to completion at
    // capacity 1, which is the claim that it needs no anticipation.
    expect(run_against_dual<late<3>, late<3>>(1) == outcome::completed, "a dual pair completes at capacity 1");
    expect(run_against_dual<early<2>, early<2>>(1) == outcome::completed,
           "a dual pair of anticipations completes at capacity 1");

    // The pair that ring<4> admits deadlocks on a channel of capacity 1,
    // and the watchdog ends it.  So the capacity of the check must be the
    // capacity of the channel, which is why the check reads it from the
    // channel type.
    expect(run_against_dual<early<4>, late<4>>(1) == outcome::deadlocked,
           "the pair that needs four slots did not deadlock on one slot, so the harness is wrong");

    // A permuted Select on a word wire: the offerer takes the branch of
    // the label that the picker sent.
    const auto [sent, received] = labels_on_a_word_wire();
    expect(sent == 1 && received == 1, "a permuted keyed Select took the branch of another label");

    // A keyed Send step, on its own thread, reaches the L1 branch of an
    // Offer of two labels, and the value after the label follows it.
    expect(keyed_step_meets_wider_offer() == 9, "a keyed Send step did not reach its branch of a wider Offer");

    // The generated family: every admitted pair runs to completion.
    const generated_tally tally = run_generated();
    expect(tally.unsound_runs == 0, "the asynchronous check admitted a pair that does not complete");
    expect(tally.sound_runs == admitted_pair_count(), "a generated admitted pair did not run");
    std::printf("test_session_subtype_attack: %zu admitted runs completed, %zu unsound, %zu refused cases that "
                "complete at capacity %zu\n",
                tally.sound_runs, tally.unsound_runs, tally.refused_but_completes, checked_capacity);

    // A subtype with a narrower payload puts the same bytes on the wire
    // as the supertype payload, because each axiom keeps the
    // representation.  Checked on the size, the alignment and a value.
    const ::fixy::Refined<::fixy::positive, int> refined = ::fixy::mint_refined<::fixy::positive>(42);
    static_assert(sizeof(refined) == sizeof(int) && alignof(decltype(refined)) == alignof(int));
    expect(refined.value() == 42, "a refined payload does not carry its value as the bare payload");
    const ::fixy::Tagged<int, tags::source::Sanitized> tagged =
        ::fixy::mint_tagged<tags::source::External>(7).retag<tags::source::Sanitized>();
    static_assert(sizeof(tagged) == sizeof(int) && alignof(decltype(tagged)) == alignof(int));
    expect(tagged.value() == 7, "a tagged payload does not carry its value as the bare payload");

    return failures == 0 ? 0 : 1;
}
