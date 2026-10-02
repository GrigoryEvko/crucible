// What fixy/session/EventLog.h and fixy/session/Recording.h claim,
// checked.
//
// The event half encodes one event of each kind, decodes it, and gets
// the same event back.  Then it corrupts one byte at a time, in each way
// the decoder must refuse, and checks the reason it gives.  A stored log
// that holds a crash graded "no throw" must decode with the cause Unknown.
//
// The recorder half records a plain session, Example 3.2 of LMCS 2025 with
// the receiver crashed, a checkpoint exchange and the hand-off of an
// endpoint.  It checks each recorded event, and then that the log survives
// a round trip through bytes.
//
// The test is two source files of one executable, so that no translation
// unit compiles every recorded session:
//
//   session_recording.h                    the shared part
//   this file                              the events, the log, a plain
//                                          session, a hand-off, a keyed
//                                          choice and main
//   test_session_recording_decorators.cpp  the recorder outside a crash
//                                          transport and outside a
//                                          checkpoint session

#include "session_recording.h"

#include <foundation/permissions/PermSet.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace test_session_recording_types {

// ── One event of each kind survives the bytes ───────────────────────

constexpr bool round_trips(const s::SessionEvent& event) {
    const auto bytes = event.encode();
    const auto decoded = s::decode_session_event(bytes);
    return decoded.has_value() && decoded->encode() == bytes;
}

static_assert(round_trips(s::SessionEvent::send(kSelf, kPeer, s::default_schema_hash<int>)));
static_assert(round_trips(s::SessionEvent::send(kSelf, kPeer, s::default_schema_hash<int>, {},
                                                s::DeliveryFate::LostToCrashedPeer)));
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
static_assert(round_trips(s::SessionEvent::cipher_event(s::SessionOp::TierPromote, s::StepId{4}, {8}, 1000,
                                                        s::CipherTierTag::Warm, s::CipherTierTag::Hot)));

static_assert(s::session_op_name(s::SessionOp::CheckpointRoll) == "CheckpointRoll");

// The operations that each cipher question admits, over every enumerator
// of SessionOp.  A kind that commits the head of a tier is a persistence
// kind, and a pending store or a load commits nothing.
consteval std::uint64_t ops_admitted_by(bool (*admits)(s::SessionOp) noexcept) {
    std::uint64_t admitted = 0;
    ::foundation::reflect::for_each_enumerator<s::SessionOp>(
        [&admitted, admits](s::SessionOp op, std::string_view) noexcept {
            if (admits(op)) admitted |= std::uint64_t{1} << std::to_underlying(op);
        });
    return admitted;
}
consteval std::uint64_t op_bits(std::same_as<s::SessionOp> auto... ops) {
    return ((std::uint64_t{1} << std::to_underlying(ops)) | ...);
}
static_assert(ops_admitted_by(s::session_op_is_cipher)
              == op_bits(s::SessionOp::StorePending, s::SessionOp::StoreCommitted, s::SessionOp::LoadFromTier,
                         s::SessionOp::TierPromote, s::SessionOp::TierDemote, s::SessionOp::TierRestore));
static_assert(ops_admitted_by(s::session_op_commits_cipher_head)
              == op_bits(s::SessionOp::StoreCommitted, s::SessionOp::TierPromote, s::SessionOp::TierDemote,
                         s::SessionOp::TierRestore));

// Each identifier is its own type, so no call site swaps two of them.  The
// payload lane holds a payload hash, a saved state or a permission set.
template <typename Id, typename... Others>
inline constexpr bool converts_to_no_other =
    ((std::is_same_v<Id, Others> || !std::is_convertible_v<Id, Others>) && ...);
template <typename... Ids>
inline constexpr bool are_distinct_ids = (converts_to_no_other<Ids, Ids...> && ...);
static_assert(are_distinct_ids<s::SessionTagId, s::RoleTagId, s::SchemaHash, s::PayloadHash, s::RecoveryPathHash,
                               s::StateHash, s::InnerPermSetHash, s::LabelWord, s::StepId>);

// A writer that hashes its payload gives the hash to the factory, and the
// event keeps it.
static_assert(s::SessionEvent::send(kSelf, kPeer, s::default_schema_hash<int>, s::PayloadHash{7}).payload_hash()
              == s::PayloadHash{7});
static_assert(s::SessionEvent::recv(kSelf, kPeer, s::default_schema_hash<int>, s::PayloadHash{8}).payload_hash()
              == s::PayloadHash{8});

// The public step key reads the step of an event and orders two steps.
static_assert(s::StepIdKeyFn{}(s::SessionEvent::cipher_event(s::SessionOp::StoreCommitted, s::StepId{9}, {1}, 0)).value
              == 9);
static_assert(s::StepIdLess{}(s::StepId{1}, s::StepId{2}));
static_assert(!s::StepIdLess{}(s::StepId{2}, s::StepId{2}));
static_assert(!s::StepIdLess{}(s::StepId{3}, s::StepId{2}));

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

// An epoched hand-off decodes with its thresholds, and it writes the same
// bytes back, although no factory writes one.
constexpr auto kEpochedBytes = with_byte(with_byte(s::SessionEvent::delegate_handoff(kSelf, kPeer, {5}, {6}).encode(),
                                                   kOpOffset, std::to_underlying(s::SessionOp::EpochedDelegate)),
                                         kEpochOffset, 7);
static_assert(s::decode_session_event(kEpochedBytes).has_value());
static_assert(s::decode_session_event(kEpochedBytes)->op() == s::SessionOp::EpochedDelegate);
static_assert(s::decode_session_event(kEpochedBytes)->min_epoch() == 7);
static_assert(s::decode_session_event(kEpochedBytes)->encode() == kEpochedBytes);

// A stored log can hold 3 in the crash lane for a crash graded "no throw".
static_assert(s::decode_session_event(with_byte(kStopBytes, kCrashOffset, 3))->crash_cause() == s::CrashCause::Unknown);

// A short span is truncated.
static_assert(!s::decode_session_event(std::span<const std::byte>{kSendBytes.data(), 71}).has_value());
static_assert(s::decode_session_event(std::span<const std::byte>{kSendBytes.data(), 71}).error()
              == s::EventDecodeError::Truncated);

namespace {

int check_log_decode() {
    std::vector<std::byte> block;
    for (const auto& bytes : {kSendBytes, kCloseBytes, kStopBytes})
        block.insert(block.end(), bytes.begin(), bytes.end());
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

}  // namespace

// ── A plain session ─────────────────────────────────────────────────

namespace {

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
    // The recorder hashes no payload, so it writes the zero of "not hashed".
    if (log[0].op() != s::SessionOp::Send || log[0].delivery_fate() != s::DeliveryFate::Delivered
        || log[0].payload_schema() != s::default_schema_hash<int> || log[0].from_role() != kSelf
        || log[0].payload_hash() != s::PayloadHash{})
        return fail("the send event is wrong");
    if (log[1].op() != s::SessionOp::Offer || log[1].branch_index() != 0) return fail("the offer event is wrong");
    if (log[2].op() != s::SessionOp::Recv || log[2].to_role() != kSelf) return fail("the recv event is wrong");
    if (log[3].op() != s::SessionOp::Close) return fail("the close event is wrong");
    return 0;
}

}  // namespace

// ── The hand-off of an endpoint ─────────────────────────────────────
//
// A payload that carries an endpoint is recorded as a hand-off and never
// as a plain message.  The event holds the hash of the carried protocol and
// the hash of the permission set that goes with it.
//
// The carried handle keeps the default policy.  So the Release build of
// this test compiles the send of a checked parcel through the recorder.
// -Werror stops that build on each uninitialized read that GCC reports.

using Carried = s::Send<int, s::End>;
using NoPermissions = ::foundation::permissions::EmptyPermSet;
using Parcel = s::DelegatedSession<Carried, Port, s::DefaultAbandonmentPolicy, NoPermissions>;

// The outer channel holds one parcel.
struct ParcelPort {
    std::optional<Parcel>* held = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

namespace {

int check_handoff_recording() {
    Mailbox to_self;
    Mailbox to_peer;
    std::optional<Parcel> held;
    s::SessionEventLog log;
    auto sender =
        s::mint_recorded_session(s::mint_session_handle<s::Send<Parcel, s::End>>(ParcelPort{&held}), log, kSelf, kPeer);
    auto receiver =
        s::mint_recorded_session(s::mint_session_handle<s::Recv<Parcel, s::End>>(ParcelPort{&held}), log, kPeer, kSelf);

    auto parcel = s::mint_delegated_session(s::mint_session_handle<Carried>(Port{&to_self, &to_peer}));
    (void)std::move(sender)
        .send(std::move(parcel),
              [](ParcelPort& port, Parcel& value) noexcept {
                  port.held->emplace(std::move(value));
                  return true;
              })
        .close();
    auto [received, at_end] = std::move(receiver).recv([](ParcelPort& port) noexcept {
        std::optional<Parcel> taken{std::move(*port.held)};
        port.held->reset();
        return taken;
    });
    (void)std::move(at_end).close();
    (void)std::move(received).accept().send(3, push_int).close();
    if (to_peer.slots.size() != 1) return fail("the accepted endpoint did not step its own session");

    constexpr s::StateHash carried_hash = s::default_proto_hash<Carried>;
    constexpr s::InnerPermSetHash no_permissions_hash{::foundation::reflect::stable_type_id<NoPermissions>};
    static_assert(carried_hash != s::default_proto_hash<s::End>);
    if (log.size() != 4) return fail("the hand-off did not record four events");
    if (log[0].op() != s::SessionOp::Delegate || log[0].from_role() != kSelf || log[0].to_role() != kPeer
        || log[0].delegated_proto() != carried_hash || log[0].inner_perm_set() != no_permissions_hash)
        return fail("the send of a parcel was not recorded as the hand-off of the carried protocol");
    if (log[2].op() != s::SessionOp::Accept || log[2].from_role() != kSelf || log[2].to_role() != kPeer
        || log[2].delegated_proto() != carried_hash || log[2].inner_perm_set() != no_permissions_hash)
        return fail("the receive of a parcel was not recorded as the accept of the carried protocol");
    return 0;
}

}  // namespace

// ── A keyed choice, and the replay of a choice ──────────────────────
//
// The two sides hold the labels in another order.  Each records the
// index of the branch in its own protocol and the label word of the
// branch, so the two logs agree on the label.

using KeyedAsk = s::Select<s::Send<s::PeerMsg<Carol, Yes, int>, s::End>, s::Send<s::PeerMsg<Carol, No, int>, s::End>>;
using KeyedHear = s::Offer<s::Recv<s::PeerMsg<Carol, No, int>, s::End>, s::Recv<s::PeerMsg<Carol, Yes, int>, s::End>>;
using PlainAsk = s::Select<s::Send<int, s::End>, s::Send<char, s::End>>;

namespace {

int check_keyed_recording() {
    Mailbox to_left;
    Mailbox to_right;
    s::SessionEventLog log_left;
    s::SessionEventLog log_right;
    auto left =
        s::mint_recorded_session(s::mint_session_handle<KeyedAsk>(Port{&to_left, &to_right}), log_left, kSelf, kPeer);
    auto right =
        s::mint_recorded_session(s::mint_session_handle<KeyedHear>(Port{&to_right, &to_left}), log_right, kPeer, kSelf);
    // A keyed message is its label word and then its value, so each side
    // stands at the value step of its branch.
    auto left_value = std::move(left).select<1>(push_label);
    static_assert(std::is_same_v<typename decltype(left_value)::protocol, s::Send<int, s::End>>);
    (void)std::move(left_value).send(8, push_int).close();
    if (to_right.slots.size() != 2)
        return fail("the keyed select did not put its label word and its value on the wire");
    int heard = 0;
    std::move(right).branch(pop_label, [&](auto right_value) {
        auto [value, right_end] = std::move(right_value).recv(pop_int);
        heard = value;
        (void)std::move(right_end).close();
    });
    if (heard != 8) return fail("the value of the keyed message did not reach the receiver");

    constexpr s::LabelWord no_word{s::branch_wire_word_v<KeyedAsk, 1>};
    if (log_left[0].op() != s::SessionOp::Select || log_left[0].branch_index() != 1
        || log_left[0].label_word() != no_word)
        return fail("the keyed select did not record its index and its label word");
    if (log_right[0].op() != s::SessionOp::Offer || log_right[0].branch_index() != 0
        || log_right[0].label_word() != no_word)
        return fail("the keyed offer did not record its own index and the label word");
    if (s::replayed_branch<KeyedAsk>(log_left[0]) != 1 || s::replayed_branch<KeyedHear>(log_right[0]) != 0)
        return fail("a recorded keyed event did not replay against its own protocol");

    // An event with no label word does not replay against a keyed choice.  Nor does an event whose word names
    // another branch, nor an Offer against a Select.
    if (s::replayed_branch<KeyedAsk>(s::SessionEvent::select(kSelf, kPeer, 1)))
        return fail("an event with no label word replayed against a keyed choice");
    constexpr s::LabelWord yes_word{s::branch_wire_word_v<KeyedAsk, 0>};
    if (s::replayed_branch<KeyedAsk>(s::SessionEvent::select(kSelf, kPeer, 1, s::DeliveryFate::Delivered, yes_word)))
        return fail("an event whose word names another branch replayed");
    if (s::replayed_branch<KeyedAsk>(log_right[0])) return fail("an Offer event replayed against a Select");
    if (s::replayed_branch<KeyedAsk>(s::SessionEvent::select(kSelf, kPeer, 2, s::DeliveryFate::Delivered, no_word)))
        return fail("an index past the last branch replayed");

    // A keyed Send step records a send of its label word, and its value step
    // records a send of the value.
    Mailbox step_out;
    Mailbox step_in;
    s::SessionEventLog log_step;
    using SayNo = s::Send<s::PeerMsg<Carol, No, int>, s::End>;
    auto stepper =
        s::mint_recorded_session(s::mint_session_handle<SayNo>(Port{&step_in, &step_out}), log_step, kSelf, kPeer);
    (void)std::move(stepper).send(push_label).send(9, push_int).close();
    if (step_out.slots.size() != 2 || step_out.slots.front() != s::step_wire_word_v<SayNo>
        || step_out.slots.front() != s::branch_wire_word_v<KeyedAsk, 1> || step_out.slots.back() != 9)
        return fail("the keyed step did not send the word of its label, which the wider Select sends for No, and "
                    "then its value");
    if (log_step.size() != 3 || log_step[0].op() != s::SessionOp::Send || log_step[1].op() != s::SessionOp::Send
        || log_step[1].payload_schema() != s::default_schema_hash<int>)
        return fail("the keyed step did not record a send of the label and a send of the value");

    // A positional choice replays an event with no word, and refuses one
    // with a word.
    if (s::replayed_branch<PlainAsk>(s::SessionEvent::select(kSelf, kPeer, 1)) != 1)
        return fail("a positional event did not replay");
    if (s::replayed_branch<PlainAsk>(s::SessionEvent::select(kSelf, kPeer, 1, s::DeliveryFate::Delivered, no_word)))
        return fail("a positional choice replayed an event with a label word");
    return 0;
}

}  // namespace

}  // namespace test_session_recording_types

using namespace test_session_recording_types;

int main() {
    if (const int rc = check_log_decode(); rc != 0) return rc;
    if (const int rc = check_log_steps(); rc != 0) return rc;
    if (const int rc = check_plain_recording(); rc != 0) return rc;
    if (const int rc = check_crash_recording(); rc != 0) return rc;
    if (const int rc = check_checkpoint_recording(); rc != 0) return rc;
    if (const int rc = check_handoff_recording(); rc != 0) return rc;
    if (const int rc = check_keyed_recording(); rc != 0) return rc;
    if (const int rc = check_keyed_checkpoint(); rc != 0) return rc;
    return 0;
}
