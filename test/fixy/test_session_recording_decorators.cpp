// The recorder outside a crash transport and outside a checkpoint session,
// for test_session_recording.  Example 3.2 of LMCS 2025 runs with the
// receiver crashed, and a checkpoint exchange commits, with a plain and
// with a keyed choice.

#include "session_recording.h"

#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

namespace test_session_recording_types {

// ── Example 3.2 with the receiver crashed ───────────────────────────

struct P {};
struct Q {};
using ProtoP = s::Select<s::Send<int, s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Q>, s::End>>>>;
using ProtoQ = s::Offer<s::Recv<int, s::Select<s::Send<int, s::End>>>, s::Recv<s::Crash<P>, s::End>>;

int check_crash_recording() {
    Mailbox to_p;
    Mailbox to_q;
    s::PeerCrashCell cell_p;
    s::PeerCrashCell cell_q;
    s::SessionEventLog log_p;
    s::SessionEventLog log_q;
    auto p = s::mint_recorded_session(
        s::mint_crash_session<ProtoP, P, Q>(bg_ctx(), Port{&to_p, &to_q}, cell_q, s::mint_crash_writer(cell_p)), log_p,
        kSelf, kPeer);
    auto q = s::mint_recorded_session(
        s::mint_crash_session<ProtoQ, Q, P>(bg_ctx(), Port{&to_q, &to_p}, cell_p, s::mint_crash_writer(cell_q)), log_q,
        kPeer, kSelf);

    (void)std::move(q).crash(s::CrashCause::Throw);
    if (log_q.size() != 1 || log_q[0].op() != s::SessionOp::Stop
        || log_q[0].stop_reason() != s::StopReasonKind::LocalAbort || log_q[0].crash_cause() != s::CrashCause::Throw
        || log_q[0].stopped_role() != kPeer)
        return fail("the local crash was not recorded as a LocalAbort stop");

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, lost] = std::move(p_sent).send(3, push_int);
    if (!lost || *lost != 3) return fail("the lost payload did not come back through the recorder");
    bool took_crash = false;
    std::move(p_wait).branch(pop_label, [&](auto branch) {
        if constexpr (std::is_same_v<typename decltype(branch)::protocol, s::Recv<s::Crash<Q>, s::End>>) {
            auto [record, at_end] = std::move(branch).recv();
            took_crash = record.cause == s::CrashCause::Throw;
            (void)std::move(at_end).close();
        } else {
            std::abort();
        }
    });
    if (!took_crash) return fail("p did not take the crash branch");
    if (log_p.size() != 5) return fail("p did not record five events");
    if (log_p[0].op() != s::SessionOp::Select || log_p[0].delivery_fate() != s::DeliveryFate::LostToCrashedPeer)
        return fail("the select to a crashed peer was not recorded as lost");
    if (log_p[1].op() != s::SessionOp::Send || log_p[1].delivery_fate() != s::DeliveryFate::LostToCrashedPeer)
        return fail("the send to a crashed peer was not recorded as lost");
    if (log_p[2].op() != s::SessionOp::Offer || log_p[2].branch_index() != 1)
        return fail("the crash branch was not recorded as branch 1");
    if (log_p[3].op() != s::SessionOp::Stop || log_p[3].stop_reason() != s::StopReasonKind::PeerCrashed
        || log_p[3].crash_cause() != s::CrashCause::Throw || log_p[3].stopped_role() != kPeer)
        return fail("the detected crash was not recorded as a PeerCrashed stop with its cause");
    if (log_p[4].op() != s::SessionOp::Close) return fail("the close was not recorded");

    // Replay reads the same events back from the bytes.
    std::vector<std::byte> block;
    for (const s::SessionEvent& event : log_p) {
        const auto bytes = event.encode();
        block.insert(block.end(), bytes.begin(), bytes.end());
    }
    const auto replayed = s::decode_session_log(block);
    if (!replayed || replayed->size() != log_p.size()) return fail("the recorded log did not decode");
    for (std::size_t index = 0; index < replayed->size(); ++index) {
        if ((*replayed)[index].encode() != log_p[index].encode()) return fail("a replayed event differs");
    }
    return 0;
}

// ── A checkpoint exchange ───────────────────────────────────────────

int check_checkpoint_recording() {
    using Decide = s::Select<s::Commit<s::Send<int, s::End>>, s::Roll>;
    using Follow = s::Offer<s::Commit<s::Recv<int, s::End>>, s::Roll>;
    Mailbox to_left;
    Mailbox to_right;
    s::SessionEventLog log_left;
    s::SessionEventLog log_right;
    auto left = s::mint_recorded_session(
        s::mint_checkpoint_session<Decide, Follow>(bg_ctx(), Port{&to_left, &to_right}), log_left, kSelf, kPeer);
    auto right = s::mint_recorded_session(
        s::mint_checkpoint_session<Follow, Decide>(bg_ctx(), Port{&to_right, &to_left}), log_right, kPeer, kSelf);
    auto left_saved = std::move(left).select<0>(push_label);
    int got = 0;
    std::move(right).branch(pop_label, [&](auto branch) {
        if constexpr (std::is_same_v<typename decltype(branch)::protocol, s::Recv<int, s::End>>) {
            auto left_end = std::move(left_saved).send(4, push_int);
            auto [value, right_end] = std::move(branch).recv(pop_int);
            got = value;
            (void)std::move(right_end).close();
            (void)std::move(left_end).close();
        } else {
            std::abort();
        }
    });
    if (got != 4) return fail("the checkpoint exchange read the wrong value");
    if (log_left.size() != 4 || log_left[1].op() != s::SessionOp::CheckpointCommit
        || log_left[1].checkpoint_role() != s::CheckpointRole::Active)
        return fail("the decider did not record an active commit");
    if (log_right.size() != 4 || log_right[0].op() != s::SessionOp::Offer
        || log_right[1].op() != s::SessionOp::CheckpointCommit
        || log_right[1].checkpoint_role() != s::CheckpointRole::Passive)
        return fail("the follower did not record a passive commit");
    return 0;
}

// A keyed choice inside a checkpoint session.  The checkpoint handle
// follows its inner handle past the label word of the keyed branch, to the
// value step.
int check_keyed_checkpoint() {
    using Decide = s::Select<s::Commit<s::Select<s::Send<s::PeerMsg<Carol, Yes, int>, s::End>,
                                                 s::Send<s::PeerMsg<Carol, No, int>, s::Send<int, s::End>>>>,
                             s::Roll>;
    using Hear = s::Offer<s::Recv<s::PeerMsg<Carol, Yes, int>, s::End>,
                          s::Recv<s::PeerMsg<Carol, No, int>, s::Recv<int, s::End>>>;
    using Follow = s::Offer<s::Commit<Hear>, s::Roll>;
    Mailbox to_left;
    Mailbox to_right;
    auto left = s::mint_checkpoint_session<Decide, Follow>(bg_ctx(), Port{&to_left, &to_right});
    auto right = s::mint_checkpoint_session<Follow, Decide>(bg_ctx(), Port{&to_right, &to_left});
    auto left_value = std::move(left).select<0>(push_label).template select<1>(push_label);
    static_assert(std::is_same_v<typename decltype(left_value)::protocol, s::Send<int, s::Send<int, s::End>>>);
    (void)std::move(left_value).send(6, push_int).send(5, push_int).close();

    int label_value = 0;
    int got = 0;
    std::move(right).branch(pop_label, [&](auto committed) {
        if constexpr (std::is_same_v<typename decltype(committed)::protocol, Hear>) {
            std::move(committed).branch(pop_label, [&](auto taken) {
                using Taken = typename decltype(taken)::protocol;
                if constexpr (std::is_same_v<Taken, s::Recv<int, s::Recv<int, s::End>>>) {
                    auto [first, then] = std::move(taken).recv(pop_int);
                    auto [second, right_end] = std::move(then).recv(pop_int);
                    label_value = first;
                    got = second;
                    (void)std::move(right_end).close();
                } else if constexpr (std::is_same_v<Taken, s::Recv<int, s::End>>) {
                    auto [value, right_end] = std::move(taken).recv(pop_int);
                    static_cast<void>(value);
                    (void)std::move(right_end).close();
                } else {
                    std::abort();
                }
            });
        } else {
            std::abort();
        }
    });
    if (label_value != 6) return fail("the value of the keyed label of a checkpoint session did not arrive");
    return got == 5 ? 0 : fail("the keyed branch of a checkpoint session did not carry its value");
}

}  // namespace test_session_recording_types
