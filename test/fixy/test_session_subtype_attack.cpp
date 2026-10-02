// Attacks on the protocol algebra and on subtyping, through correct use
// of the public surface only: no cast, no reopened namespace outside a
// documented registry, no undefined behaviour.  Each attack either
// fails, and the test pins the failure, or it succeeds, and the test
// pins the success on the known-limitation ledger at the end.  The
// ledger only shrinks: an entry whose attack no longer works fails its
// pin, and the entry goes.
//
// The runtime attacks run two threads over two bounded queues.  A
// watchdog reads the counts of the two queues, and it ends a run and
// records a deadlock when the counts show that no side can make a step.
// So no attack can hang the test, and no verdict reads a clock.
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

    // The number of pushes and the number of pops so far.  Each count only
    // grows, and one side writes each count.
    [[nodiscard]] std::size_t pushed() const { return head_.load(std::memory_order_acquire); }
    [[nodiscard]] std::size_t popped() const { return tail_.load(std::memory_order_acquire); }

private:
    std::array<int, max_capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t capacity_;
};

// The four counts of a run.  The left side writes the pushes of
// left_to_right and the pops of right_to_left, and the right side writes
// the other two counts.  Each step of a side is one push or one pop, so
// the counts give the position of each side in its script and the number
// of messages in each queue.
struct run_counts {
    std::size_t left_sent = 0;
    std::size_t right_received = 0;
    std::size_t right_sent = 0;
    std::size_t left_received = 0;
    friend bool operator==(run_counts const&, run_counts const&) = default;
};

// Reads the four counts until two reads in sequence agree.  Each count only
// grows, so two equal reads show that no count changed between them.  Each
// read is an acquire load, so the second read of a count sees each write
// that happened before a write that the first reads saw.  The counts are
// then a state of the run that holds each step before a step it holds.
// The loop ends, because the scripts are finite and each step changes a
// count once.
inline run_counts read_counts(bounded_queue const& left_to_right, bounded_queue const& right_to_left) {
    const auto read_once = [&] {
        return run_counts{.left_sent = left_to_right.pushed(),
                          .right_received = left_to_right.popped(),
                          .right_sent = right_to_left.pushed(),
                          .left_received = right_to_left.popped()};
    };
    run_counts earlier = read_once();
    while (true) {
        const run_counts later = read_once();
        if (later == earlier) return later;
        earlier = later;
    }
}

enum class side_state : std::uint8_t {
    can_step,
    waits,
    finished
};

// The state of a side at `position` in its script.  A send waits while its
// queue holds `capacity` messages, and a receive waits while its queue is
// empty.
inline side_state state_of(std::span<const step> script, std::size_t position, std::size_t outgoing,
                           std::size_t incoming, std::size_t capacity) {
    if (position >= script.size()) return side_state::finished;
    const bool is_blocked = script[position].is_send ? outgoing >= capacity : incoming == 0;
    return is_blocked ? side_state::waits : side_state::can_step;
}

enum class run_state : std::uint8_t {
    running,
    finished,
    deadlocked
};

// The state of a run from its counts.  A run is a deadlock when no side can
// step and a side has not finished.  Only a step changes a count, so that
// state is final, and the verdict reads no clock: a side that is off the
// processor for any time while its queue lets it step keeps the run alive.
//
// A step of one side never takes a step away from the other side: a push
// can only fill the queue that the other side pops, and a pop can only free
// the queue that the other side pushes.  So each schedule of a pair ends in
// the same state, and a pair deadlocks in each schedule or in none.
inline run_state state_of_run(run_counts const& counts, std::span<const step> left, std::span<const step> right,
                              std::size_t capacity) {
    const std::size_t left_to_right = counts.left_sent - counts.right_received;
    const std::size_t right_to_left = counts.right_sent - counts.left_received;
    const side_state left_state =
        state_of(left, counts.left_sent + counts.left_received, left_to_right, right_to_left, capacity);
    const side_state right_state =
        state_of(right, counts.right_sent + counts.right_received, right_to_left, left_to_right, capacity);
    if (left_state == side_state::finished && right_state == side_state::finished) return run_state::finished;
    if (left_state == side_state::can_step || right_state == side_state::can_step) return run_state::running;
    return run_state::deadlocked;
}

// One wait of a side for its queue.  The first waits of a step spin with
// a pause, the next waits give the processor to another thread, and the
// remaining waits sleep.  A side that waits through a deadlock then does
// not make a system call in a tight loop until the watchdog stops it.
// Each step starts its count at zero.
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

// Runs the two scripts against each other.  The watchdog reads the counts
// of the run between two polls.  When they show a deadlock, it raises the
// stop flag, each thread leaves its wait, and both joins return.  The
// poll interval sets only how soon the watchdog sees a final state, never
// the verdict.
outcome run_pair(std::span<const step> left, std::span<const step> right, std::size_t capacity) {
    bounded_queue left_to_right{capacity};
    bounded_queue right_to_left{capacity};
    std::atomic<bool> stop{false};
    std::atomic<bool> wrong{false};
    const auto play = [&](std::span<const step> script, bounded_queue& out, bounded_queue& in) {
        for (const step& action : script) {
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
        }
    };
    bool is_deadlocked = false;
    {
        std::jthread first{[&] { play(left, left_to_right, right_to_left); }};
        std::jthread second{[&] { play(right, right_to_left, left_to_right); }};
        constexpr auto poll = std::chrono::microseconds{500};
        while (true) {
            const run_state state = state_of_run(read_counts(left_to_right, right_to_left), left, right, capacity);
            if (state == run_state::finished) break;
            if (state == run_state::deadlocked) {
                is_deadlocked = true;
                stop.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::sleep_for(poll);
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

// The verdict of the watchdog on planted counts.  A side that waits while
// the other side can step is no deadlock, however long the other side is
// off the processor.  A run with a time window called such a state a
// deadlock when the other side stayed off the processor for the window.
inline void watchdog_verdicts_read_the_counts() {
    constexpr step send_a{.is_send = true, .message = 1};
    constexpr step send_b{.is_send = true, .message = 2};
    constexpr step recv_a{.is_send = false, .message = 1};
    constexpr step recv_b{.is_send = false, .message = 2};

    constexpr std::array ping{send_a, recv_b};
    constexpr std::array pong{recv_a, send_b};
    expect(state_of_run(run_counts{}, ping, pong, 1) == run_state::running, "a run that can start is running");
    expect(state_of_run(run_counts{.left_sent = 1}, ping, pong, 1) == run_state::running,
           "a side that waits for a message in the queue of the other side is no deadlock");
    constexpr run_counts at_the_end{.left_sent = 1, .right_received = 1, .right_sent = 1, .left_received = 1};
    expect(state_of_run(at_the_end, ping, pong, 1) == run_state::finished,
           "a run whose two sides are at the end of their scripts is finished");

    constexpr std::array waits_for_b{recv_b};
    constexpr std::array waits_for_a{recv_a};
    expect(state_of_run(run_counts{}, waits_for_b, waits_for_a, 1) == run_state::deadlocked,
           "two sides that each wait for a message of the other side are a deadlock");

    constexpr std::array sends_a{send_a, send_a};
    constexpr std::array sends_b{send_b, send_b};
    expect(state_of_run(run_counts{.left_sent = 1, .right_sent = 1}, sends_a, sends_b, 1) == run_state::deadlocked,
           "two sides that each send into a full queue are a deadlock");

    constexpr std::array one_send{send_a};
    constexpr std::array two_receives{recv_a, recv_a};
    expect(state_of_run(run_counts{.left_sent = 1, .right_received = 1}, one_send, two_receives, 1)
               == run_state::deadlocked,
           "a side that waits for a side that finished is a deadlock");
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
    watchdog_verdicts_read_the_counts();

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
