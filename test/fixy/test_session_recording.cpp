// What fixy/session/EventLog.h and fixy/session/Recording.h claim,
// checked.
//
// The event half encodes one event of each kind, decodes it, and gets
// the same event back.  Then it corrupts one byte at a time, in each way
// the decoder must refuse, and checks the reason it gives.  A log that
// the old tree wrote for a crash graded "no throw" must decode with the
// cause Unknown.
//
// The recorder half records three sessions: a plain one, Example 3.2 of
// LMCS 2025 with the receiver crashed, and a checkpoint exchange.  It
// checks each recorded event, and then that the log survives a round
// trip through bytes.

#include <fixy/session/Recording.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace s = fixy::session;

namespace {

int fail(const char* what) {
    std::fprintf(stderr, "test_session_recording: %s\n", what);
    return 1;
}

constexpr s::RoleTagId kSelf{11};
constexpr s::RoleTagId kPeer{22};

// ── One event of each kind survives the bytes ───────────────────────

constexpr bool round_trips(const s::SessionEvent& event) {
    const auto bytes = event.encode();
    const auto decoded = s::decode_session_event(bytes);
    return decoded.has_value() && decoded->encode() == bytes;
}

static_assert(round_trips(s::SessionEvent::send(kSelf, kPeer, s::default_schema_hash<int>)));
static_assert(round_trips(
    s::SessionEvent::send(kSelf, kPeer, s::default_schema_hash<int>, {}, s::DeliveryFate::LostToCrashedPeer)));
static_assert(round_trips(s::SessionEvent::recv(kSelf, kPeer, s::default_schema_hash<int>)));
static_assert(round_trips(s::SessionEvent::select(kSelf, kPeer, 3)));
static_assert(round_trips(s::SessionEvent::offer(kSelf, kPeer, 250)));
static_assert(round_trips(s::SessionEvent::close(kSelf, kPeer)));
static_assert(round_trips(s::SessionEvent::detach(kSelf, kPeer, s::DetachReasonKind::AsyncCancellation)));
static_assert(round_trips(s::SessionEvent::stop(kSelf, kPeer, kPeer, s::StopReasonKind::PeerCrashed,
                                                s::CrashCause::ErrorReturn, s::RecoveryPathHash{9})));
static_assert(round_trips(s::SessionEvent::checkpoint_commit(kSelf, kPeer, s::CheckpointRole::Passive, {7})));
static_assert(round_trips(s::SessionEvent::checkpoint_roll(kSelf, kPeer, s::CheckpointRole::Active)));
static_assert(round_trips(s::SessionEvent::checkpoint_abort(kSelf, kPeer, s::CheckpointRole::Active)));
static_assert(round_trips(s::SessionEvent::delegate_handoff(kSelf, kPeer, {5}, {6})));
static_assert(round_trips(s::SessionEvent::accept_handoff(kSelf, kPeer, {5}, {6})));
static_assert(round_trips(s::SessionEvent::epoched_delegate_handoff(kSelf, kPeer, {5}, 7, 3, {6})));
static_assert(round_trips(s::SessionEvent::epoched_accept_handoff(kSelf, kPeer, {5}, 7, 3, {6})));
static_assert(round_trips(s::SessionEvent::cipher_event(s::SessionOp::TierPromote, s::StepId{4}, {8}, 1000,
                                                        s::CipherTierTag::Warm, s::CipherTierTag::Hot)));

static_assert(s::session_op_name(s::SessionOp::CheckpointRoll) == "CheckpointRoll");

// ── Corrupt bytes are refused, each for its own reason ──────────────

// Byte offsets of the control bytes in the 72-byte record.
constexpr std::size_t kOpOffset = 64;
constexpr std::size_t kBranchOffset = 65;
constexpr std::size_t kReasonOffset = 66;
constexpr std::size_t kCrashOffset = 67;
constexpr std::size_t kEpochOffset = 48;

constexpr std::array<std::byte, 72> with_byte(std::array<std::byte, 72> bytes, std::size_t offset, std::uint8_t value) {
    bytes[offset] = static_cast<std::byte>(value);
    return bytes;
}

constexpr s::EventDecodeError error_of(const std::array<std::byte, 72>& bytes) {
    const auto decoded = s::decode_session_event(bytes);
    return decoded ? s::EventDecodeError::Truncated : decoded.error();
}

constexpr auto kStopBytes =
    s::SessionEvent::stop(kSelf, kPeer, kPeer, s::StopReasonKind::PeerCrashed, s::CrashCause::Throw).encode();
constexpr auto kSendBytes = s::SessionEvent::send(kSelf, kPeer, s::default_schema_hash<int>).encode();
constexpr auto kCloseBytes = s::SessionEvent::close(kSelf, kPeer).encode();

static_assert(error_of(with_byte(kSendBytes, kOpOffset, 0)) == s::EventDecodeError::UnknownOperation);
static_assert(error_of(with_byte(kSendBytes, kOpOffset, 23)) == s::EventDecodeError::UnknownOperation);
static_assert(error_of(with_byte(kSendBytes, kOpOffset, 255)) == s::EventDecodeError::UnknownOperation);
static_assert(error_of(with_byte(kSendBytes, kReasonOffset, 2)) == s::EventDecodeError::ControlByteOutOfRange);
static_assert(error_of(with_byte(kStopBytes, kReasonOffset, 4)) == s::EventDecodeError::ControlByteOutOfRange);
static_assert(error_of(with_byte(kCloseBytes, kReasonOffset, 1)) == s::EventDecodeError::ControlByteOutOfRange);
static_assert(error_of(with_byte(kCloseBytes, kBranchOffset, 1)) == s::EventDecodeError::BranchByteOutOfRange);
static_assert(error_of(with_byte(kStopBytes, kCrashOffset, 4)) == s::EventDecodeError::CrashCauseOutOfRange);
static_assert(error_of(with_byte(kSendBytes, kCrashOffset, 1)) == s::EventDecodeError::NonZeroPadding);
static_assert(error_of(with_byte(kSendBytes, kCrashOffset + 3, 1)) == s::EventDecodeError::NonZeroPadding);
static_assert(error_of(with_byte(kSendBytes, kEpochOffset, 1)) == s::EventDecodeError::ThresholdOnPlainKind);

// A legacy checkpoint kind decodes only with its own choice byte.
constexpr auto kLegacyBase = with_byte(with_byte(kCloseBytes, kOpOffset, 8), kReasonOffset, 1);
static_assert(s::decode_session_event(kLegacyBase).has_value());
static_assert(s::decode_session_event(kLegacyBase)->legacy_checkpoint_choice() == s::CheckpointChoice::Base);
static_assert(error_of(with_byte(kLegacyBase, kReasonOffset, 2)) == s::EventDecodeError::ControlByteOutOfRange);

// The old tree wrote 3 in the crash lane for a crash graded "no throw".
static_assert(s::decode_session_event(with_byte(kStopBytes, kCrashOffset, 3))->crash_cause() == s::CrashCause::Unknown);

// A short span is truncated.
static_assert(!s::decode_session_event(std::span<const std::byte>{kSendBytes.data(), 71}).has_value());
static_assert(s::decode_session_event(std::span<const std::byte>{kSendBytes.data(), 71}).error()
              == s::EventDecodeError::Truncated);

int check_log_decode() {
    std::vector<std::byte> block;
    for (const auto& bytes : {kSendBytes, kCloseBytes, kStopBytes}) block.insert(block.end(), bytes.begin(), bytes.end());
    const auto whole = s::decode_session_log(block);
    if (!whole || whole->size() != 3) return fail("a valid block of three records did not decode");

    std::vector<std::byte> truncated(block.begin(), block.end() - 1);
    const auto short_block = s::decode_session_log(truncated);
    if (short_block || short_block.error().index != 2) return fail("a truncated block did not name its last record");

    block[72 + kOpOffset] = std::byte{0};
    const auto corrupt = s::decode_session_log(block);
    if (corrupt || corrupt.error().index != 1 || corrupt.error().error != s::EventDecodeError::UnknownOperation)
        return fail("a corrupt middle record was not named");
    return 0;
}

// ── The log ─────────────────────────────────────────────────────────

int check_log_steps() {
    s::SessionEventLog log{s::SessionTagId{77}};
    log.record_now(s::SessionEvent::close(kSelf, kPeer));
    log.record_now(s::SessionEvent::close(kSelf, kPeer));
    if (log.size() != 2) return fail("the log lost an event");
    if (log[0].step_id().value != 1 || log[1].step_id().value != 2) return fail("the steps are not 1 and 2");
    if (log[1].session().value != 77) return fail("record_now did not stamp the session");
    const auto drained = std::move(log).drain();
    return drained.size() == 2 ? 0 : fail("the drain lost an event");
}

// ── The wire ────────────────────────────────────────────────────────

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
};

constexpr auto push_label = [](Port& port, std::size_t label) noexcept { port.out->slots.push_back(label); };
constexpr auto push_int = [](Port& port, int&& value) noexcept {
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
};
constexpr auto pop_int = [](Port& port) noexcept {
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return static_cast<int>(slot);
};
constexpr auto pop_label = [](Port& port) noexcept -> std::size_t {
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return slot;
};
constexpr auto poll_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    return pop_label(port);
};

// ── A plain session ─────────────────────────────────────────────────

int check_plain_recording() {
    using Proto = s::Send<int, s::Offer<s::Recv<int, s::End>, s::End>>;
    Mailbox to_self;
    Mailbox to_peer;
    s::SessionEventLog log;
    auto handle = s::mint_recorded_session(s::mint_session_handle<Proto>(Port{&to_self, &to_peer}), log, kSelf, kPeer);
    auto waiting = std::move(handle).send(5, push_int);
    to_self.slots.push_back(0);
    to_self.slots.push_back(9);
    int got = 0;
    std::move(waiting).branch(pop_label, [&](auto branch) {
        if constexpr (std::is_same_v<typename decltype(branch)::protocol, s::Recv<int, s::End>>) {
            auto [value, at_end] = std::move(branch).recv(pop_int);
            got = value;
            (void)std::move(at_end).close();
        } else {
            (void)std::move(branch).close();
        }
    });
    if (got != 9) return fail("the plain session read the wrong value");
    if (log.size() != 4) return fail("the plain session did not record four events");
    if (log[0].op() != s::SessionOp::Send || log[0].delivery_fate() != s::DeliveryFate::Delivered
        || log[0].payload_schema() != s::default_schema_hash<int> || log[0].from_role() != kSelf)
        return fail("the send event is wrong");
    if (log[1].op() != s::SessionOp::Offer || log[1].branch_index() != 0) return fail("the offer event is wrong");
    if (log[2].op() != s::SessionOp::Recv || log[2].to_role() != kSelf) return fail("the recv event is wrong");
    if (log[3].op() != s::SessionOp::Close) return fail("the close event is wrong");
    return 0;
}

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
    auto p = s::mint_recorded_session(s::mint_crash_session<ProtoP, P, Q>(Port{&to_p, &to_q}, cell_q), log_p, kSelf,
                                      kPeer);
    auto q = s::mint_recorded_session(s::mint_crash_session<ProtoQ, Q, P>(Port{&to_q, &to_p}, cell_p), log_q, kPeer,
                                      kSelf);

    (void)std::move(q).crash(s::CrashCause::Throw, cell_q);
    if (log_q.size() != 1 || log_q[0].op() != s::SessionOp::Stop || log_q[0].stop_reason() != s::StopReasonKind::LocalAbort
        || log_q[0].crash_cause() != s::CrashCause::Throw || log_q[0].stopped_role() != kPeer)
        return fail("the local crash was not recorded as a LocalAbort stop");

    auto p_sent = std::move(p).select<0>(push_label);
    auto [p_wait, lost] = std::move(p_sent).send(3, push_int);
    if (!lost || *lost != 3) return fail("the lost payload did not come back through the recorder");
    bool took_crash = false;
    std::move(p_wait).branch(poll_label, [&](auto branch) {
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
    auto left = s::mint_recorded_session(s::mint_checkpoint_session<Decide, Follow>(Port{&to_left, &to_right}),
                                         log_left, kSelf, kPeer);
    auto right = s::mint_recorded_session(s::mint_checkpoint_session<Follow, Decide>(Port{&to_right, &to_left}),
                                          log_right, kPeer, kSelf);
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

}  // namespace

int main() {
    if (const int rc = check_log_decode(); rc != 0) return rc;
    if (const int rc = check_log_steps(); rc != 0) return rc;
    if (const int rc = check_plain_recording(); rc != 0) return rc;
    if (const int rc = check_crash_recording(); rc != 0) return rc;
    if (const int rc = check_checkpoint_recording(); rc != 0) return rc;
    return 0;
}
