// Crash reports that race a crash-watched session on another thread.
//
// A crash report is two atomic words in the cell of the peer
// (fixy/session/CrashTransport.h), and the session reads them on its own
// thread.  Four schedules run here, each kRuns times, and the tsan preset
// runs them under ThreadSanitizer:
//
//   - Two failure detectors report two peers while one thread sends to
//     each of them.  Each payload either reaches its queue or comes back,
//     and each session then takes its crash branch.  A session that starts
//     after a report sends nothing.
//   - A detector reports the peer while the transport of a send runs.  The
//     send checked the peer before the transport, so the payload stays
//     delivered, and the next reception takes the crash branch.
//   - The peer crashes while the poll of a branch runs.  The message that
//     the peer finished before its crash arrives, and the next reception
//     takes the crash branch.
//   - The peer sends its messages and crashes on its own thread, and the
//     survivor reads them only after it sees the crash.  The report is the
//     one edge that orders the writes of the peer before the reads of the
//     survivor, so a report that does not release is a data race, and tsan
//     stops the test.

#include <fixy/Ctx.h>
#include <fixy/session/CrashTransport.h>

#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
#include <thread>
#include <utility>

namespace s = ::fixy::session;
namespace eff = ::foundation::effects;

namespace {

using BgCtx = ::fixy::BgDrainCtx;
[[nodiscard]] BgCtx bg_ctx() noexcept { return BgCtx{eff::testing::bg()}; }

struct P {};  // the survivor
struct Q {};  // a peer
struct R {};  // a second peer

constexpr int kRuns = 50;
constexpr int kMessages = 64;

// A queue of wire words with no atomic in it.  Each schedule gives it to
// one thread at a time, or orders the two threads through the crash
// report, so tsan sees each access that no edge orders.
struct Mailbox {
    std::deque<std::uint64_t> slots;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

constexpr auto push_label = [](Port& port, std::size_t label) noexcept {
    port.out->slots.push_back(label);
    return true;
};
constexpr auto push_int = [](Port& port, int& value) noexcept {
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
    return true;
};

[[nodiscard]] std::optional<std::uint64_t> pop(Port& port) noexcept {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t word = port.in->slots.front();
    port.in->slots.pop_front();
    return word;
}

constexpr auto poll_word = [](Port& port) noexcept -> std::optional<std::size_t> {
    const auto word = pop(port);
    if (!word) return std::nullopt;
    return static_cast<std::size_t>(*word);
};
constexpr auto read_int = [](Port& port) noexcept -> std::optional<int> {
    const auto word = pop(port);
    if (!word) return std::nullopt;
    return static_cast<int>(*word);
};

// p sends one value to the peer and waits for a reply or for the crash.
template <typename Peer>
using Asks = s::Select<s::Send<int, s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Peer>, s::End>>>>;

// q streams values to p.  p drains them until q crashes.
using Streams = s::Loop<s::Select<s::Send<int, s::Continue>>>;
using Drains = s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<s::Crash<Q>, s::End>>>;

static_assert(s::CrashSessionAdmissible<Asks<Q>, P, Q, s::NoReliableRoles>);
static_assert(s::CrashSessionAdmissible<Streams, Q, P, s::NoReliableRoles>);
static_assert(s::CrashSessionAdmissible<Drains, P, Q, s::NoReliableRoles>);

using DrainHandle = decltype(s::mint_crash_session<Drains, P, Q>(
    std::declval<const BgCtx&>(), Port{}, std::declval<const s::PeerCrashCell&>(), std::declval<s::CrashWriter>()));
using StreamHandle = decltype(s::mint_crash_session<Streams, Q, P>(
    std::declval<const BgCtx&>(), Port{}, std::declval<const s::PeerCrashCell&>(), std::declval<s::CrashWriter>()));

int fail(const char* what) {
    std::fprintf(stderr, "test_session_crash_race: %s\n", what);
    return 1;
}

void wait_for(const std::atomic<bool>& flag) noexcept {
    while (!flag.load(std::memory_order_acquire))
        CRUCIBLE_SPIN_PAUSE;
}

// Two flags that pin a crash inside a transport call: the transport says
// that it runs, the other thread crashes the peer, and then it lets the
// transport return.
struct TransportBarrier {
    alignas(64) std::atomic<bool> is_running{false};
    alignas(64) std::atomic<bool> is_released{false};
};

// Sends the label and the value of Asks.  The peer can crash before,
// between or after the two writes.
template <typename Handle>
[[nodiscard]] auto send_value(Handle handle, int value) {
    auto chosen = std::move(handle).template select<0>(push_label);
    return std::move(chosen).send(value, push_int);
}

// A send to a crashed peer writes nothing and gives the payload back.
// So the queue holds the label and the value exactly when the payload
// did not come back, and the label counts as the one message sent.
template <typename Sent>
[[nodiscard]] int check_send_outcome(const Sent& sent, const Mailbox& queue, int value) {
    const std::size_t queued = queue.slots.size();
    if (sent.undelivered.has_value()) {
        if (*sent.undelivered != value) return fail("the payload that came back is not the one sent");
        if (queued > 1) return fail("a payload came back and also reached the queue");
    } else if (queued != 2 || queue.slots[1] != static_cast<std::uint64_t>(value)) {
        return fail("a payload that did not come back is missing from the queue");
    }
    const std::uint64_t labels = queued >= 1 ? 1 : 0;
    if (sent.next.messages_sent() != labels) return fail("the count of messages sent disagrees with the queue");
    return 0;
}

// The peer of Asks crashed and sent nothing, so the branch must take the
// crash branch.  Returns the cause of the crash record.
template <typename Handle>
[[nodiscard]] std::optional<s::CrashCause> take_crash_branch(Handle waiting) {
    std::optional<s::CrashCause> cause;
    std::move(waiting).branch(poll_word, [&](auto branch) {
        using Head = typename decltype(branch)::protocol;
        if constexpr (s::is_crash_branch_v<Head>) {
            auto [record, end] = std::move(branch).recv();
            cause = record.cause;
            (void)std::move(end).close();
        } else {
            std::fprintf(stderr, "test_session_crash_race: a reply came from a peer that sent nothing\n");
            std::abort();
        }
    });
    return cause;
}

// Two detectors report q and r while p sends to each of them.  Then a
// session that starts after the reports sends to q.
int run_reports_race_sends() {
    for (int run = 0; run < kRuns; ++run) {
        Mailbox from_q, to_q, from_r, to_r, from_late, to_late;
        s::PeerCrashCell cell_q, cell_r;
        s::PeerCrashCell cell_p_to_q, cell_p_to_r, cell_p_late;
        std::atomic<bool> is_started{false};
        bool is_first_report_q = false;
        bool is_first_report_r = false;

        auto to_peer_q = s::mint_crash_session<Asks<Q>, P, Q>(bg_ctx(), Port{&from_q, &to_q}, cell_q,
                                                              s::mint_crash_writer(cell_p_to_q));
        auto to_peer_r = s::mint_crash_session<Asks<R>, P, R>(bg_ctx(), Port{&from_r, &to_r}, cell_r,
                                                              s::mint_crash_writer(cell_p_to_r));

        std::jthread detector_q([&] {
            wait_for(is_started);
            is_first_report_q = s::mint_crash_reporter(cell_q).report(s::CrashCause::Abort);
        });
        std::jthread detector_r([&] {
            wait_for(is_started);
            is_first_report_r = s::mint_crash_reporter(cell_r).report(s::CrashCause::Throw);
        });

        is_started.store(true, std::memory_order_release);
        auto sent_q = send_value(std::move(to_peer_q), 100 + run);
        auto sent_r = send_value(std::move(to_peer_r), 200 + run);
        detector_q.join();
        detector_r.join();

        if (!is_first_report_q || !is_first_report_r) return fail("the one report of a cell was refused");
        if (const int rc = check_send_outcome(sent_q, to_q, 100 + run); rc != 0) return rc;
        if (const int rc = check_send_outcome(sent_r, to_r, 200 + run); rc != 0) return rc;
        if (take_crash_branch(std::move(sent_q.next)) != s::CrashCause::Abort)
            return fail("the session to q did not take the crash branch with the cause of its report");
        if (take_crash_branch(std::move(sent_r.next)) != s::CrashCause::Throw)
            return fail("the session to r did not take the crash branch with the cause of its report");

        // The join made the report of q visible, so this session writes nothing.
        auto late = s::mint_crash_session<Asks<Q>, P, Q>(bg_ctx(), Port{&from_late, &to_late}, cell_q,
                                                         s::mint_crash_writer(cell_p_late));
        auto sent_late = send_value(std::move(late), 300 + run);
        if (!sent_late.undelivered.has_value() || *sent_late.undelivered != 300 + run)
            return fail("a send to a peer that crashed before it did not give the payload back");
        if (!to_late.slots.empty()) return fail("a send to a crashed peer reached its queue");
        if (sent_late.next.messages_sent() != 0) return fail("a label that was not written was counted");
        if (take_crash_branch(std::move(sent_late.next)) != s::CrashCause::Abort)
            return fail("the late session did not take the crash branch");
    }
    return 0;
}

// The detector reports q while the transport of the value runs.
int run_crash_inside_send_transport() {
    for (int run = 0; run < kRuns; ++run) {
        Mailbox from_q, to_q;
        s::PeerCrashCell cell_q, cell_p;
        TransportBarrier barrier;

        auto to_peer =
            s::mint_crash_session<Asks<Q>, P, Q>(bg_ctx(), Port{&from_q, &to_q}, cell_q, s::mint_crash_writer(cell_p));
        std::jthread detector([&] {
            wait_for(barrier.is_running);
            (void)s::mint_crash_reporter(cell_q).report(s::CrashCause::Abort);
            barrier.is_released.store(true, std::memory_order_release);
        });

        auto chosen = std::move(to_peer).select<0>(push_label);
        auto sent = std::move(chosen).send(400 + run, [&barrier](Port& port, int& value) noexcept {
            port.out->slots.push_back(static_cast<std::uint64_t>(value));
            barrier.is_running.store(true, std::memory_order_release);
            wait_for(barrier.is_released);
            return true;
        });
        detector.join();

        if (sent.undelivered.has_value()) return fail("a payload that the transport took came back");
        if (to_q.slots.size() != 2) return fail("the label and the value did not both reach the queue");
        if (!sent.next.peer_has_crashed()) return fail("the report did not reach the session");
        if (take_crash_branch(std::move(sent.next)) != s::CrashCause::Abort)
            return fail("the reception after the send did not take the crash branch");
    }
    return 0;
}

// q crashes while the poll of p's branch runs.  The message that q
// finished before its crash is already queued.
int run_crash_inside_branch_poll() {
    for (int run = 0; run < kRuns; ++run) {
        Mailbox to_p, to_q;
        s::PeerCrashCell cell_p, cell_q;
        TransportBarrier barrier;

        auto streaming =
            s::mint_crash_session<Streams, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q));
        auto chosen = std::move(streaming).select<0>(push_label);
        auto q_sent = std::move(chosen).send(500 + run, push_int);
        if (q_sent.undelivered.has_value()) return fail("a payload to a live peer came back");

        auto draining =
            s::mint_crash_session<Drains, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
        std::jthread peer([&barrier, q = std::move(q_sent.next)]() mutable {
            wait_for(barrier.is_running);
            (void)std::move(q).crash(s::CrashCause::Abort);
            barrier.is_released.store(true, std::memory_order_release);
        });

        int value = -1;
        std::optional<DrainHandle> after;
        std::move(draining).branch(
            [&barrier](Port& port) noexcept -> std::optional<std::size_t> {
                barrier.is_running.store(true, std::memory_order_release);
                wait_for(barrier.is_released);
                return poll_word(port);
            },
            [&](auto branch) {
                using Head = typename decltype(branch)::protocol;
                if constexpr (s::is_crash_branch_v<Head>) {
                    std::fprintf(stderr, "test_session_crash_race: the crash branch won over a queued message\n");
                    std::abort();
                } else {
                    auto [received, next] = std::move(branch).recv(read_int);
                    value = received;
                    after.emplace(std::move(next));
                }
            });
        peer.join();

        if (value != 500 + run) return fail("the message that q finished before its crash did not arrive");
        if (after->messages_received() != 1) return fail("the message was not counted");
        bool took_crash_branch = false;
        std::move(*after).branch(poll_word, [&](auto branch) {
            using Head = typename decltype(branch)::protocol;
            if constexpr (s::is_crash_branch_v<Head>) {
                auto [record, end] = std::move(branch).recv();
                took_crash_branch = record.cause == s::CrashCause::Abort;
                (void)std::move(end).close();
            } else {
                std::fprintf(stderr, "test_session_crash_race: a message came after the count of the report\n");
                std::abort();
            }
        });
        if (!took_crash_branch) return fail("the reception after the message did not take the crash branch");
    }
    return 0;
}

// q streams its messages and crashes on its own thread.  p reads the
// queue only after it sees the crash, so the report orders every write
// of q before every read of p.
int run_report_publishes_earlier_messages() {
    for (int run = 0; run < kRuns; ++run) {
        Mailbox to_p, to_q;
        s::PeerCrashCell cell_p, cell_q;
        bool was_payload_returned = false;

        auto draining =
            s::mint_crash_session<Drains, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p));
        std::jthread peer([&] {
            StreamHandle q = s::mint_crash_session<Streams, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p,
                                                                  s::mint_crash_writer(cell_q));
            for (int message = 0; message < kMessages; ++message) {
                auto chosen = std::move(q).select<0>(push_label);
                auto sent = std::move(chosen).send(message, push_int);
                was_payload_returned = was_payload_returned || sent.undelivered.has_value();
                q = std::move(sent.next);
            }
            (void)std::move(q).crash(s::CrashCause::Abort);
        });

        while (!draining.peer_has_crashed())
            CRUCIBLE_SPIN_PAUSE;

        int received = 0;
        bool is_in_order = true;
        bool took_crash_branch = false;
        std::optional<DrainHandle> current{std::move(draining)};
        bool is_done = false;
        while (!is_done) {
            DrainHandle step = std::move(*current);
            current.reset();
            std::move(step).branch(poll_word, [&](auto branch) {
                using Head = typename decltype(branch)::protocol;
                if constexpr (s::is_crash_branch_v<Head>) {
                    auto [record, end] = std::move(branch).recv();
                    took_crash_branch = record.cause == s::CrashCause::Abort;
                    (void)std::move(end).close();
                    is_done = true;
                } else {
                    auto [value, next] = std::move(branch).recv(read_int);
                    is_in_order = is_in_order && value == received;
                    ++received;
                    current.emplace(std::move(next));
                }
            });
        }
        peer.join();

        if (was_payload_returned) return fail("a payload to a live peer came back");
        if (received != kMessages) return fail("p lost a message that q finished before its crash");
        if (!is_in_order) return fail("p received the messages out of order");
        if (!took_crash_branch) return fail("p did not take the crash branch with the cause of the report");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = run_reports_race_sends(); rc != 0) return rc;
    if (const int rc = run_crash_inside_send_transport(); rc != 0) return rc;
    if (const int rc = run_crash_inside_branch_poll(); rc != 0) return rc;
    if (const int rc = run_report_publishes_earlier_messages(); rc != 0) return rc;
    std::puts("test_session_crash_race: every schedule passes");
    return 0;
}
