#pragma once

// Runs two session handles against each other over a queue of words, so
// the session oracle can check the wire end to end: what one endpoint
// writes is what the other endpoint reads and dispatches on.
//
// Each endpoint walks its protocol on its own thread.  At a Select it
// takes branch (seed + n) mod count, where n counts the choices that it
// made before.  A Send of a value writes value_word, a keyed Send and a
// Select write the wire word that the handle gives, and a Recv or an
// Offer reads one word.  An endpoint stops at End, after fuel actions, or
// when its peer stopped and no word is left for it.
//
// One run is one child process of the child door of fixy/os/Process.h, so
// an abort in a handle is an outcome and not the end of the test:
//
//   ok       both endpoints stopped
//   abort    a handle aborted: a word named no branch or no label
//   desync   a Recv of a value read a word that is no value
//   stuck    both endpoints wait for a word, and no word and no close can
//            come: each direction is empty and open
//
// The verdict stuck reads no clock.  The two directions of the wire share
// one lock, and an endpoint that waits for a word counts as waiting under
// that lock.  The last endpoint that starts to wait then sees the state of
// both directions, so a slow host cannot look like a deadlock.  A run that
// never stops and never gets to that state ends at the time limit of the
// test run.
//
// The handles use check::Off, so an endpoint may stop before End.  The test
// process starts no thread, so the child of each run may start the two
// threads of its endpoints.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Args.h>
#include <fixy/os/Process.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/ChildProcess.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace session_oracle::wire {

namespace fs = ::fixy::session;

// The word that a Send of a value writes.  A label word always has its
// top bit set (fixy/session/Fold.h), and a position is small,
// so this word is neither.
inline constexpr std::uint64_t value_word = 0x0076616c7565ULL;
inline constexpr int fuel_per_endpoint = 12;

enum class exit_code : int {
    ok = 0,
    desync = 3,
    stuck = 4
};

struct Wire {};

// The side of an endpoint on the line.  Each side reads the direction that
// carries words to it.
enum class Side : unsigned char {
    left = 0,
    right = 1
};

[[nodiscard]] constexpr Side peer_of(Side const side) noexcept { return side == Side::left ? Side::right : Side::left; }

// The two directions of the wire between the two endpoints, under one lock.
class Line {
public:
    // Put a word into the direction that the side `to` reads.
    void push(Side const to, std::uint64_t word) {
        {
            const std::lock_guard lock{mutex_};
            direction(to).words.push_back(word);
            ++in_flight_;
        }
        ready_.notify_all();
    }

    // Close the direction that the side `to` reads: no word comes after the
    // words in it.
    void close(Side const to) {
        {
            const std::lock_guard lock{mutex_};
            direction(to).is_closed = true;
        }
        ready_.notify_all();
    }

    // The next word for the side `self`, or false when its peer closed the
    // direction and no word is left.  The wait ends the process as stuck
    // when both endpoints wait and each direction is empty and open.
    [[nodiscard]] bool pop(Side const self, std::uint64_t& word) {
        std::unique_lock lock{mutex_};
        Direction& in = direction(self);
        ++waiting_;
        ready_.wait(lock, [&] {
            if (!in.words.empty() || in.is_closed) return true;
            if (is_stuck()) std::_Exit(static_cast<int>(exit_code::stuck));
            return false;
        });
        --waiting_;
        if (in.words.empty()) return false;
        word = in.words.front();
        in.words.pop_front();
        --in_flight_;
        return true;
    }

private:
    struct Direction {
        std::deque<std::uint64_t> words;
        bool is_closed = false;
    };

    [[nodiscard]] Direction& direction(Side const to) noexcept { return to == Side::left ? to_left_ : to_right_; }

    // Only the two endpoints write a direction.  When both wait under this
    // lock on empty, open directions, no word and no close can come.  No
    // word in flight means that each direction is empty.
    [[nodiscard]] bool is_stuck() const noexcept {
        return waiting_ == 2 && in_flight_ == 0 && !to_left_.is_closed && !to_right_.is_closed;
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    Direction to_left_;
    Direction to_right_;
    int waiting_ = 0;
    // The words in the two directions together.
    int in_flight_ = 0;
};

struct Endpoint {
    Line* line = nullptr;
    Side side = Side::left;
    unsigned seed = 0;
    unsigned picks = 0;
    int fuel = fuel_per_endpoint;

    void send(std::uint64_t word) const { line->push(peer_of(side), word); }
    void close_out() const { line->close(peer_of(side)); }
    [[nodiscard]] bool receive(std::uint64_t& word) const { return line->pop(side, word); }
};

template <typename P>
struct head {
    static constexpr int kind = -1;
};
template <>
struct head<fs::End> {
    static constexpr int kind = 0;
};
template <typename T, typename K>
struct head<fs::Send<T, K>> {
    static constexpr int kind = 1;
    using message = T;
};
template <typename T, typename K>
struct head<fs::Recv<T, K>> {
    static constexpr int kind = 2;
    using message = T;
};
template <typename... Bs>
struct head<fs::Select<Bs...>> {
    static constexpr int kind = 3;
};
template <typename... Bs>
struct head<fs::Offer<Bs...>> {
    static constexpr int kind = 4;
};

template <typename H>
void walk(H handle, Endpoint& self);

// The transports of an endpoint, and the step to the next position.  Each
// is one type at every protocol position, so a handle operation
// instantiates its wait helpers once for each transport and not once for
// each position.
struct WordWriter {
    Endpoint* self = nullptr;
    bool operator()(Wire&, std::size_t word) const noexcept {
        self->send(word);
        return true;
    }
};

struct ValueWriter {
    Endpoint* self = nullptr;
    template <typename T>
    bool operator()(Wire&, T&) const noexcept {
        self->send(value_word);
        return true;
    }
};

struct WordReader {
    std::uint64_t word = 0;
    ::fixy::Option<std::size_t> operator()(Wire&) const noexcept {
        return ::fixy::some(static_cast<std::size_t>(word));
    }
};

template <typename T>
struct ValueReader {
    ::fixy::Option<T> operator()(Wire&) const noexcept { return ::fixy::some(T{}); }
};

struct WalkOn {
    Endpoint* self = nullptr;
    template <typename H>
    void operator()(H next) const {
        walk(std::move(next), *self);
    }
};

// Spends one action of fuel.  False when the endpoint has no fuel left,
// and then the endpoint stops and closes its queue.
[[gnu::noinline]] inline bool spend_fuel(Endpoint& self) {
    if (self.fuel-- > 0) return true;
    self.close_out();
    return false;
}

// The next word for the endpoint.  False when its peer stopped and no
// word is left, and then the endpoint stops and closes its queue.
[[gnu::noinline]] inline bool take_word(Endpoint& self, std::uint64_t& word) {
    if (self.receive(word)) return true;
    self.close_out();
    return false;
}

template <std::size_t... I, typename H>
void select_branch(H handle, Endpoint& self, std::size_t pick, std::index_sequence<I...>) {
    (void)((pick == I ? (walk(std::move(handle).template select<I>(WordWriter{&self}), self), true) : false) || ...);
}

template <typename H>
void walk(H handle, Endpoint& self) {
    using P = typename H::protocol;
    constexpr int kind = head<P>::kind;
    static_assert(kind >= 0, "session_oracle wire: the walker has no rule for this protocol head");
    if constexpr (kind == 0) {
        (void)std::move(handle).close();
        self.close_out();
        return;
    } else {
        if (!spend_fuel(self)) return;
        if constexpr (kind == 1) {
            using T = typename head<P>::message;
            if constexpr (H::is_keyed) {
                walk(std::move(handle).send(WordWriter{&self}), self);
            } else {
                walk(std::move(handle).send(T{}, ValueWriter{&self}), self);
            }
        } else if constexpr (kind == 2) {
            using T = typename head<P>::message;
            std::uint64_t word = 0;
            if (!take_word(self, word)) return;
            if constexpr (H::is_keyed) {
                walk(std::move(handle).recv(WordReader{word}), self);
            } else {
                if (word != value_word) std::_Exit(static_cast<int>(exit_code::desync));
                auto [value, next] = std::move(handle).recv(ValueReader<T>{});
                (void)value;
                walk(std::move(next), self);
            }
        } else if constexpr (kind == 3) {
            constexpr std::size_t count = H::branch_count;
            const std::size_t pick = (self.seed + self.picks++) % count;
            select_branch(std::move(handle), self, pick, std::make_index_sequence<count>{});
        } else {
            std::uint64_t word = 0;
            if (!take_word(self, word)) return;
            std::move(handle).branch(WordReader{word}, WalkOn{&self});
        }
    }
}

// The start of one endpoint: it walks its protocol from the first step.
using EndpointStart = void (*)(Endpoint&);

template <typename P>
void walk_from_start(Endpoint& self) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    walk(fs::mint_session<P, fs::check::Off>(ctx, Wire{}), self);
}

// One run of the endpoint that left_start walks against the endpoint that
// right_start walks, in a child process, with the seed of both endpoints.
// Returns the outcome.  This function is not a template, so a translation
// unit compiles the fork, the two threads and the wait one time, and each
// row adds only the start of each endpoint.
[[nodiscard]] inline std::string_view run_endpoints(EndpointStart left_start, EndpointStart right_start,
                                                    unsigned seed) {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    // A body that returns ends the child with the exit code of ok.
    ::fixy::process::Child child = ::fixy::process::spawn_child(test, [left_start, right_start, seed] noexcept {
                                       Line line;
                                       Endpoint a{&line, Side::left, seed};
                                       Endpoint b{&line, Side::right, seed};
                                       std::thread left{[&] { left_start(a); }};
                                       std::thread right{[&] { right_start(b); }};
                                       left.join();
                                       right.join();
                                   }).expect("session_oracle wire: the test starts the child process of a run");
    ::fixy::process::ChildReport const report =
        ::fixy::process::wait(std::move(child)).expect("session_oracle wire: the test waits for the child of a run");
    ::foundation::os::ChildEnd const end = report.end();
    if (end.kind == ::foundation::os::ChildEndKind::signaled) return "abort";
    switch (end.number) {
        case static_cast<int>(exit_code::ok):
            return "ok";
        case static_cast<int>(exit_code::desync):
            return "desync";
        case static_cast<int>(exit_code::stuck):
            return "stuck";
        default:
            return "abort";
    }
}

// One run of the endpoint of protocol A against the endpoint of protocol
// B, in a child process.  Returns the outcome.
template <typename A, typename B, unsigned Seed>
[[nodiscard]] std::string_view run_pair() {
    return run_endpoints(&walk_from_start<A>, &walk_from_start<B>, Seed);
}

struct Row {
    const char* label;
    std::string_view (*run)();
    const char* pinned;
};

// Run one row.  With should_measure, print its outcome and return false.
// Otherwise compare the outcome with the pinned outcome, and print the
// difference and return true when the two differ.
[[nodiscard]] inline bool run_differs(const Row& row, bool should_measure) {
    const std::string_view outcome = row.run();
    if (should_measure) {
        std::printf("%s %.*s\n", row.label, static_cast<int>(outcome.size()), outcome.data());
        return false;
    }
    if (outcome == row.pinned) return false;
    std::fprintf(stderr, "%s the run gives %.*s, and the golden file pins %s\n", row.label,
                 static_cast<int>(outcome.size()), outcome.data(), row.pinned);
    return true;
}

// Run the rows of each shard of a wire test, in order.  With --measure as
// the first argument, print the outcome of each row.  Otherwise compare each
// outcome with its pinned outcome, print each difference, and return 1 when
// one differs.
inline int check_all(std::initializer_list<std::span<const Row>> shards, ::fixy::os::Args const arguments) {
    const bool should_measure = arguments.get(1).match(
        [](::fixy::CText const argument) noexcept { return argument.view() == ::fixy::TextView{"--measure"}; },
        [] noexcept { return false; });
    int failed = 0;
    for (const std::span<const Row> rows : shards) {
        for (const Row& row : rows) {
            if (run_differs(row, should_measure)) failed = 1;
        }
    }
    return failed;
}

}  // namespace session_oracle::wire
