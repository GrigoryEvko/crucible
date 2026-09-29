// The value of a keyed message travels with its label word.
//
// A global type projects onto two roles, and each role runs its local
// type on its own thread.  The two threads share a channel: a queue of
// words for each direction, which a lock guards.  A PeerMsg is keyed, so
// its step sends the label word, and the handle then stands at the value
// step, which sends the value.  The receiving side checks the value that
// it reads.  Without the value step, a handle built from a projection
// would put the label word on the wire and drop the value.

#include <fixy/session/Global.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

#include <foundation/Pinned.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace s = ::fixy::session;
namespace g = ::fixy::session::global;

// The roles and the labels feed a stable id, so each has external
// linkage.
namespace test_session_projected_value_labels {
struct P {};
struct Q {};
struct Hello {};
struct Accept {};
struct Refuse {};
}  // namespace test_session_projected_value_labels

namespace {

using test_session_projected_value_labels::Accept;
using test_session_projected_value_labels::Hello;
using test_session_projected_value_labels::P;
using test_session_projected_value_labels::Q;
using test_session_projected_value_labels::Refuse;

// P sends Hello with an int to Q.  Q answers with Accept or Refuse, and
// each answer holds an int too.
using G = g::Msg<P, Q, Hello, int, g::Comm<Q, P, g::Branch<Accept, int, g::End>, g::Branch<Refuse, int, g::End>>>;

using LocalP = typename s::project_t<G, P>::local;
using LocalQ = typename s::project_t<G, Q>::local;

static_assert(std::is_same_v<LocalP, s::Send<s::PeerMsg<Q, Hello, int>,
                                             s::Offer<s::Sender<Q>, s::Recv<s::PeerMsg<Q, Accept, int>, s::End>,
                                                      s::Recv<s::PeerMsg<Q, Refuse, int>, s::End>>>>);
static_assert(
    std::is_same_v<LocalQ, s::Recv<s::PeerMsg<P, Hello, int>, s::Select<s::Send<s::PeerMsg<P, Accept, int>, s::End>,
                                                                        s::Send<s::PeerMsg<P, Refuse, int>, s::End>>>>);

// The step after a label word is the value step of the message.
static_assert(std::is_same_v<s::keyed_landing_t<s::Send<s::PeerMsg<Q, Hello, int>, s::End>>, s::Send<int, s::End>>);
static_assert(std::is_same_v<s::keyed_landing_t<s::Recv<s::PeerMsg<P, Hello, int>, s::End>>, s::Recv<int, s::End>>);
static_assert(std::is_same_v<s::keyed_landing_t<s::Send<s::PeerMsg<Q, Hello, void>, s::End>>, s::End>,
              "a payload of void has no value step");

// One direction of the channel.  A lock guards the queue, because the
// writer and the reader run on two threads.
struct Lane : ::foundation::Pinned<Lane> {
    std::mutex lock;
    std::deque<std::uint64_t> words;
};

// The Resource of one end: the lane it reads and the lane it writes.  The
// member makes it move-only, so it is an owned SessionResource.
struct Port {
    Lane* in = nullptr;
    Lane* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// A write tries, and the queue has no bound, so each try takes the word.
// A read polls: the next word when one is queued, and no value otherwise.
constexpr auto write_word = [](Port& port, std::size_t word) noexcept {
    const std::lock_guard guard{port.out->lock};
    port.out->words.push_back(word);
    return true;
};
constexpr auto write_int = [](Port& port, int& value) noexcept {
    const std::lock_guard guard{port.out->lock};
    port.out->words.push_back(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)));
    return true;
};
constexpr auto read_word = [](Port& port) noexcept -> std::optional<std::size_t> {
    const std::lock_guard guard{port.in->lock};
    if (port.in->words.empty()) return std::nullopt;
    const std::uint64_t word = port.in->words.front();
    port.in->words.pop_front();
    return static_cast<std::size_t>(word);
};
constexpr auto read_int = [](Port& port) noexcept -> std::optional<int> {
    return read_word(port).transform(
        [](std::size_t word) noexcept { return static_cast<int>(static_cast<std::uint32_t>(word)); });
};

struct Outcome {
    int hello_on_q = 0;
    int answer_on_p = 0;
};

void run_p(Port port, Outcome& outcome) {
    auto hello_value = s::mint_session_handle<LocalP>(std::move(port)).send(write_word);
    static_assert(std::is_same_v<typename decltype(hello_value)::protocol,
                                 s::Send<int, s::Offer<s::Sender<Q>, s::Recv<s::PeerMsg<Q, Accept, int>, s::End>,
                                                       s::Recv<s::PeerMsg<Q, Refuse, int>, s::End>>>>,
                  "the keyed step of Hello stands at its value step");
    auto answer = std::move(hello_value).send(42, write_int);
    std::move(answer).branch(read_word, [&outcome](auto answer_value) {
        auto [value, at_end] = std::move(answer_value).recv(read_int);
        outcome.answer_on_p = value;
        (void)std::move(at_end).close();
    });
}

void run_q(Port port, Outcome& outcome) {
    auto hello_value = s::mint_session_handle<LocalQ>(std::move(port)).recv(read_word);
    auto [hello, choosing] = std::move(hello_value).recv(read_int);
    outcome.hello_on_q = hello;
    auto accept_value = std::move(choosing).select<0>(write_word);
    static_assert(std::is_same_v<typename decltype(accept_value)::protocol, s::Send<int, s::End>>,
                  "the select of Accept stands at the value step of Accept");
    (void)std::move(accept_value).send(hello + 1, write_int).close();
}

}  // namespace

int main() {
    Lane p_to_q;
    Lane q_to_p;
    Outcome outcome;
    {
        std::jthread q_thread{[&] { run_q(Port{&p_to_q, &q_to_p}, outcome); }};
        std::jthread p_thread{[&] { run_p(Port{&q_to_p, &p_to_q}, outcome); }};
    }
    if (outcome.hello_on_q != 42) {
        std::fprintf(stderr, "test_session_projected_value: Q read %d as the value of Hello, want 42\n",
                     outcome.hello_on_q);
        return 1;
    }
    if (outcome.answer_on_p != 43) {
        std::fprintf(stderr, "test_session_projected_value: P read %d as the value of the answer, want 43\n",
                     outcome.answer_on_p);
        return 1;
    }
    if (!p_to_q.words.empty() || !q_to_p.words.empty()) {
        std::fprintf(stderr, "test_session_projected_value: a word stayed on the channel\n");
        return 1;
    }
    std::fprintf(stderr, "test_session_projected_value: OK\n");
    return 0;
}
