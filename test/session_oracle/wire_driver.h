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
// One run is one child process, so an abort in a handle is an outcome and
// not the end of the test:
//
//   ok       both endpoints stopped
//   abort    a handle aborted: a word named no branch or no label
//   desync   a Recv of a value read a word that is no value
//   stuck    an endpoint waited two seconds for a word that never came
//
// The handles use check::Off, so an endpoint may stop before End.

#include <fixy/session/Handle.h>

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

namespace session_oracle::wire {

namespace fs = ::fixy::session;

// The word that a Send of a value writes.  A label word always has its
// top bit set (foundation/algebra/Transition.h), and a position is small,
// so this word is neither.
inline constexpr std::uint64_t value_word = 0x0076'616c'7565ULL;
inline constexpr int fuel_per_endpoint = 12;

enum class exit_code : int { ok = 0, desync = 3, stuck = 4 };

struct Wire {};

class Queue {
public:
    void push(std::uint64_t word) {
        {
            const std::lock_guard lock{mutex_};
            words_.push_back(word);
        }
        ready_.notify_all();
    }

    void close() {
        {
            const std::lock_guard lock{mutex_};
            closed_ = true;
        }
        ready_.notify_all();
    }

    // The next word, or false when the writer closed and no word is left.
    // A wait longer than two seconds ends the process as stuck.
    [[nodiscard]] bool pop(std::uint64_t& word) {
        std::unique_lock lock{mutex_};
        const bool woke = ready_.wait_for(lock, std::chrono::seconds{2}, [&] { return !words_.empty() || closed_; });
        if (!woke) std::_Exit(static_cast<int>(exit_code::stuck));
        if (words_.empty()) return false;
        word = words_.front();
        words_.pop_front();
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::uint64_t> words_;
    bool closed_ = false;
};

struct Endpoint {
    Queue* out = nullptr;
    Queue* in = nullptr;
    unsigned seed = 0;
    unsigned picks = 0;
    int fuel = fuel_per_endpoint;
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

template <std::size_t... I, typename H>
void select_branch(H handle, Endpoint& self, std::size_t pick, std::index_sequence<I...>) {
    const auto write = [&](Wire&, std::size_t word) noexcept {
        self.out->push(word);
        return true;
    };
    (void)((pick == I ? (walk(std::move(handle).template select<I>(write), self), true) : false) || ...);
}

template <typename H>
void walk(H handle, Endpoint& self) {
    using P = typename H::protocol;
    constexpr int kind = head<P>::kind;
    static_assert(kind >= 0, "session_oracle wire: the walker has no rule for this protocol head");
    if constexpr (kind == 0) {
        (void)std::move(handle).close();
        self.out->close();
        return;
    } else {
        if (self.fuel-- <= 0) {
            self.out->close();
            return;
        }
        if constexpr (kind == 1) {
            using T = typename head<P>::message;
            if constexpr (H::is_keyed) {
                walk(std::move(handle).send([&](Wire&, std::size_t word) noexcept {
                    self.out->push(word);
                    return true;
                }),
                     self);
            } else {
                walk(std::move(handle).send(T{}, [&](Wire&, T&) noexcept {
                    self.out->push(value_word);
                    return true;
                }),
                     self);
            }
        } else if constexpr (kind == 2) {
            using T = typename head<P>::message;
            std::uint64_t word = 0;
            if (!self.in->pop(word)) {
                self.out->close();
                return;
            }
            if constexpr (H::is_keyed) {
                walk(std::move(handle).recv([&](Wire&) noexcept -> std::optional<std::size_t> {
                    return static_cast<std::size_t>(word);
                }),
                     self);
            } else {
                if (word != value_word) std::_Exit(static_cast<int>(exit_code::desync));
                auto [value, next] = std::move(handle).recv([](Wire&) noexcept -> std::optional<T> { return T{}; });
                (void)value;
                walk(std::move(next), self);
            }
        } else if constexpr (kind == 3) {
            constexpr std::size_t count = H::branch_count;
            const std::size_t pick = (self.seed + self.picks++) % count;
            select_branch(std::move(handle), self, pick, std::make_index_sequence<count>{});
        } else {
            std::uint64_t word = 0;
            if (!self.in->pop(word)) {
                self.out->close();
                return;
            }
            std::move(handle).branch(
                [&](Wire&) noexcept -> std::optional<std::size_t> { return static_cast<std::size_t>(word); },
                [&](auto next) { walk(std::move(next), self); });
        }
    }
}

// One run of the endpoint of protocol A against the endpoint of protocol
// B, in a child process.  Returns the outcome.
template <typename A, typename B, unsigned Seed>
[[nodiscard]] std::string_view run_pair() {
    std::fflush(nullptr);
    const pid_t child = ::fork();
    if (child < 0) {
        std::perror("session_oracle wire: fork");
        std::abort();
    }
    if (child == 0) {
        ::alarm(10);
        Queue a_to_b;
        Queue b_to_a;
        Endpoint a{&a_to_b, &b_to_a, Seed};
        Endpoint b{&b_to_a, &a_to_b, Seed};
        std::thread left{[&] { walk(fs::mint_session_handle<A, Wire, fs::check::Off>(Wire{}), a); }};
        std::thread right{[&] { walk(fs::mint_session_handle<B, Wire, fs::check::Off>(Wire{}), b); }};
        left.join();
        right.join();
        std::_Exit(static_cast<int>(exit_code::ok));
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
    }
    if (WIFSIGNALED(status)) return WTERMSIG(status) == SIGALRM ? "stuck" : "abort";
    switch (WEXITSTATUS(status)) {
        case static_cast<int>(exit_code::ok): return "ok";
        case static_cast<int>(exit_code::desync): return "desync";
        case static_cast<int>(exit_code::stuck): return "stuck";
        default: return "abort";
    }
}

struct Row {
    const char* label;
    std::string_view (*run)();
    const char* pinned;
};

// With --measure, print the outcome of each row.  Otherwise compare each
// outcome with its pinned outcome, print each difference, and return 1
// when one differs.
inline int check_all(std::span<const Row> rows, int argc, char** argv) {
    const bool measure = argc > 1 && std::strcmp(argv[1], "--measure") == 0;
    int failed = 0;
    for (const Row& row : rows) {
        const std::string_view outcome = row.run();
        if (measure) {
            std::printf("%s %.*s\n", row.label, static_cast<int>(outcome.size()), outcome.data());
        } else if (outcome != row.pinned) {
            std::fprintf(stderr, "%s the run gives %.*s, and the golden file pins %s\n", row.label,
                         static_cast<int>(outcome.size()), outcome.data(), row.pinned);
            failed = 1;
        }
    }
    return failed;
}

}  // namespace session_oracle::wire
