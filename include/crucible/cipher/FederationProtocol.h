#pragma once

// This file is the wire format, not a transport. It fixes the byte
// layout, the magic, the version stamp, and the rules every transport
// applies before it trusts an entry.
//
// Little-endian throughout, a 32-byte header, payload immediately
// after it:
//
//  Offset Size  Field
//   0      4    magic                 'CFED' as a little-endian word
//   4      2    protocol_version
//   6      2    universe_cardinality  the sender's effect-atom count
//   8      8    content_hash
//  16      8    row_hash
//  24      4    payload_size          bytes following the header
//  28      4    reserved              zero
//  32+   ...    payload               opaque bytes
//
// The integrity of the payload is the receiver's business. This layer
// never sees the payload in raw form, so a receiver hashes the bytes
// itself and compares the result against the content hash.
//
// The cardinality stamp carries the append-only rule for effect atoms
// onto the wire. An atom's value never moves and a new atom takes the
// next free position. So a stamp at or below the receiver's own count
// means the receiver knows every atom the sender used. A stamp above
// it means the sender used atoms this receiver cannot interpret, the
// row hash then depends on values the receiver does not know, and the
// entry is refused rather than allowed to collide with a stale row
// hash.
//
// Two key values never travel. A sentinel key is the empty-slot
// marker of an open-addressed table, and accepting one lets a peer
// poison its own lookup termination. A zero key means the sender left
// it unset, and accepting one aims traffic at the most vulnerable
// bucket on the far side. Both are refused on write and on read.
//
// The reserved word is written as zero and any other value is
// refused. That is what makes a later revision safe. A reader of this
// version rejects newer traffic outright instead of reading four
// bytes as something they are not.
//
// Every multi-byte field is little-endian, which matches every
// supported platform, so the codec copies bytes straight in and out
// with no swapping. A big-endian port needs explicit swaps. The
// layout itself does not change.
//
// The session protocol at the end of this header fixes the legal order
// of the messages around one entry.

#include <crucible/Types.h>
#include <crucible/effects/_OsUniverse.h>
#include <crucible/safety/_Decide.h>
#include <crucible/safety/_Pre.h>
#include <fixy/Federation.h>
#include <fixy/Tagged.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Global.h>
#include <fixy/session/Network.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>

namespace crucible::cipher::federation {

// The bytes in memory read 'C', 'F', 'E', 'D' in increasing address
// order, which is the order a receiver scanning a stream meets them.
inline constexpr std::uint32_t FEDERATION_MAGIC = 0x44454643u;

// A version is compared for equality and never fallen back from. A
// later header read under this layout would take four bytes of the
// reserved word for something else.
inline constexpr std::uint16_t FEDERATION_PROTOCOL_V1 = 1u;

struct FederationEntryHeader {
    std::uint32_t magic = 0;
    std::uint16_t protocol_version = 0;
    std::uint16_t universe_cardinality = 0;
    ContentHash content_hash{};
    RowHash row_hash{};
    std::uint32_t payload_size = 0;
    std::uint32_t reserved = 0;
};

// The offsets below are pinned because a codec written elsewhere
// reaches into the bytes by offset rather than through this struct.
static_assert(sizeof(FederationEntryHeader) == 32, "FederationEntryHeader must be exactly 32 bytes — the wire-format "
                                                   "header size.  Adding a field requires a new protocol version and "
                                                   "an update to every receiver.");
static_assert(alignof(FederationEntryHeader) == 8, "FederationEntryHeader must be 8-byte aligned (the natural "
                                                   "alignment of the embedded ContentHash + RowHash).");
static_assert(std::is_standard_layout_v<FederationEntryHeader>,
              "FederationEntryHeader must be standard-layout to permit "
              "offsetof + std::memcpy-based wire codec.");
static_assert(std::is_trivially_copyable_v<FederationEntryHeader>,
              "FederationEntryHeader must be trivially copyable to permit "
              "std::memcpy round-trip through the byte buffer.");

static_assert(offsetof(FederationEntryHeader, magic) == 0);
static_assert(offsetof(FederationEntryHeader, protocol_version) == 4);
static_assert(offsetof(FederationEntryHeader, universe_cardinality) == 6);
static_assert(offsetof(FederationEntryHeader, content_hash) == 8);
static_assert(offsetof(FederationEntryHeader, row_hash) == 16);
static_assert(offsetof(FederationEntryHeader, payload_size) == 24);
static_assert(offsetof(FederationEntryHeader, reserved) == 28);

inline constexpr std::size_t FEDERATION_HEADER_BYTES = sizeof(FederationEntryHeader);

struct ColdBlobRegion {
    std::size_t offset_bytes = 0;
    std::size_t nbytes = 0;
};

template <std::size_t MaxRegions>
[[nodiscard]] constexpr bool cold_blob_regions_pairwise_disjoint(std::span<const ColdBlobRegion> regions) noexcept {
    if (regions.size() > MaxRegions) return false;

    std::array<decide::Interval<std::size_t>, MaxRegions> intervals{};
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const ColdBlobRegion region = regions[i];
        if (!decide::no_overflow_sum(region.offset_bytes, region.nbytes)) {
            return false;
        }
        intervals[i] = {
            .lo = region.offset_bytes,
            .hi = region.offset_bytes + region.nbytes,
        };
    }
    return decide::intervals_pairwise_disjoint(
        std::span<const decide::Interval<std::size_t>>{intervals.data(), regions.size()});
}

[[nodiscard]] constexpr bool federation_entry_blob_layout_disjoint(std::size_t payload_bytes) noexcept {
    if (!decide::no_overflow_sum(FEDERATION_HEADER_BYTES, payload_bytes)) {
        return false;
    }
    const std::size_t payload_end = FEDERATION_HEADER_BYTES + payload_bytes;
    const std::array<ColdBlobRegion, 2> regions{{
        {.offset_bytes = 0, .nbytes = FEDERATION_HEADER_BYTES},
        {.offset_bytes = FEDERATION_HEADER_BYTES, .nbytes = payload_bytes},
    }};
    return payload_end >= FEDERATION_HEADER_BYTES
        && cold_blob_regions_pairwise_disjoint<regions.size()>(std::span<const ColdBlobRegion>{regions});
}

static_assert((FEDERATION_MAGIC & 0xFFu) == 'C', "FEDERATION_MAGIC byte 0 must be 'C'.");
static_assert(((FEDERATION_MAGIC >> 8) & 0xFFu) == 'F', "FEDERATION_MAGIC byte 1 must be 'F'.");
static_assert(((FEDERATION_MAGIC >> 16) & 0xFFu) == 'E', "FEDERATION_MAGIC byte 2 must be 'E'.");
static_assert(((FEDERATION_MAGIC >> 24) & 0xFFu) == 'D', "FEDERATION_MAGIC byte 3 must be 'D'.");

static_assert(::crucible::effects::OsUniverse::cardinality <= std::uint16_t{0xFFFF},
              "OsUniverse::cardinality must fit in the uint16_t wire field.");

// The other binary stream format in this runtime is the graph
// snapshot, whose magic word is spelled out here as a literal rather
// than included, to keep this header light. A stream dispatched to
// the wrong codec would misread twenty-eight bytes of header before
// anything noticed, so the two words must stay distinct. A third
// format has to extend this assertion.
static_assert(FEDERATION_MAGIC != 0x43444147u, "FEDERATION_MAGIC must not collide with the graph-snapshot magic — "
                                               "a federation stream and a graph snapshot must dispatch to "
                                               "different codecs at the magic-check step.");

static_assert(sizeof(FederationEntryHeader::payload_size) == 4,
              "payload_size MUST be a 32-bit field — caps the per-entry "
              "payload at 4 GiB.  A larger artifact fragments into several "
              "entries, or waits for a protocol version with a wider field.");
static_assert(sizeof(FederationEntryHeader::universe_cardinality) == 2,
              "universe_cardinality MUST be a 16-bit field — caps the effect "
              "atom catalog at 65535 entries.");
static_assert(sizeof(FederationEntryHeader::magic) == 4, "magic MUST be a 32-bit field — pinned for byte-stable cross-"
                                                         "platform protocol identification.");
static_assert(sizeof(FederationEntryHeader::protocol_version) == 2,
              "protocol_version MUST be a 16-bit field — supports up to 65536 "
              "wire-format revisions.");
static_assert(sizeof(FederationEntryHeader::reserved) == 4,
              "reserved MUST be a 32-bit field — a later layout claims this "
              "slot, and the width has to be there waiting for it.");

enum class FederationError : std::uint8_t {
    None = 0,
    BadMagic = 1,
    UnsupportedVersion = 2,
    UniverseCardinalityTooHigh = 3,
    SentinelKey = 4,
    ZeroKey = 5,
    ReservedNonZero = 6,
    TruncatedHeader = 7,
    TruncatedPayload = 8,
    OutputBufferTooSmall = 9,
};

[[nodiscard]] inline constexpr std::string_view federation_error_name(FederationError e) noexcept {
    switch (e) {
        case FederationError::None:
            return "None";
        case FederationError::BadMagic:
            return "BadMagic";
        case FederationError::UnsupportedVersion:
            return "UnsupportedVersion";
        case FederationError::UniverseCardinalityTooHigh:
            return "UniverseCardinalityTooHigh";
        case FederationError::SentinelKey:
            return "SentinelKey";
        case FederationError::ZeroKey:
            return "ZeroKey";
        case FederationError::ReservedNonZero:
            return "ReservedNonZero";
        case FederationError::TruncatedHeader:
            return "TruncatedHeader";
        case FederationError::TruncatedPayload:
            return "TruncatedPayload";
        case FederationError::OutputBufferTooSmall:
            return "OutputBufferTooSmall";
        default:
            return "<unknown FederationError>";
    }
}

// The rejection rules are checked in the body and reported, rather
// than asserted as preconditions. A caller that hands over a buffer
// too small, or a key it forgot to fill in, deserves an error it can
// route, not a terminated process.

[[nodiscard]] inline std::expected<std::size_t, FederationError>
serialize_federation_entry(std::span<std::uint8_t> out_buf, const KernelCacheKey& key,
                           std::span<const std::uint8_t> payload) noexcept {
    if (key.is_sentinel()) {
        return std::unexpected(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return std::unexpected(FederationError::ZeroKey);
    }

    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(FederationError::OutputBufferTooSmall);
    }
    CRUCIBLE_PRE(federation_entry_blob_layout_disjoint(payload.size()));

    const std::size_t total_bytes = FEDERATION_HEADER_BYTES + payload.size();
    if (out_buf.size() < total_bytes) {
        return std::unexpected(FederationError::OutputBufferTooSmall);
    }

    FederationEntryHeader hdr{};
    hdr.magic = FEDERATION_MAGIC;
    hdr.protocol_version = FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality = static_cast<std::uint16_t>(::crucible::effects::OsUniverse::cardinality);
    hdr.content_hash = key.content_hash;
    hdr.row_hash = key.row_hash;
    hdr.payload_size = static_cast<std::uint32_t>(payload.size());
    hdr.reserved = 0;

    // A byte codec copies. Starting a lifetime at the address is the
    // tool for arena type-punning and would be wrong here.
    std::memcpy(out_buf.data(), &hdr, FEDERATION_HEADER_BYTES);

    // An entry with no payload bytes is valid and meaningful. It
    // announces the pair of hashes, and the receiver looks the
    // artifact up in its own store from the content hash.
    if (!payload.empty()) {
        std::memcpy(out_buf.data() + FEDERATION_HEADER_BYTES, payload.data(), payload.size());
    }

    return total_bytes;
}

// The receiver's own atom count arrives as an argument instead of
// being read from a global, so a test can stand in as a newer or an
// older peer.
//
// The caller reads the payload itself and checks that it hashes to
// the content hash in the returned header.

[[nodiscard]] inline std::expected<FederationEntryHeader, FederationError>
deserialize_federation_header(std::span<const std::uint8_t> in_buf, std::uint16_t receiver_cardinality) noexcept {
    if (in_buf.size() < FEDERATION_HEADER_BYTES) {
        return std::unexpected(FederationError::TruncatedHeader);
    }

    FederationEntryHeader hdr{};
    std::memcpy(&hdr, in_buf.data(), FEDERATION_HEADER_BYTES);

    if (hdr.magic != FEDERATION_MAGIC) {
        return std::unexpected(FederationError::BadMagic);
    }

    if (hdr.protocol_version != FEDERATION_PROTOCOL_V1) {
        return std::unexpected(FederationError::UnsupportedVersion);
    }

    if (hdr.reserved != 0u) {
        return std::unexpected(FederationError::ReservedNonZero);
    }

    // A sender at or below this count used only atoms this receiver
    // already knows, which is the safe direction of the append-only
    // rule.
    if (hdr.universe_cardinality > receiver_cardinality) {
        return std::unexpected(FederationError::UniverseCardinalityTooHigh);
    }

    const KernelCacheKey key{hdr.content_hash, hdr.row_hash};
    if (key.is_sentinel()) {
        return std::unexpected(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return std::unexpected(FederationError::ZeroKey);
    }

    const std::size_t bytes_after_header = in_buf.size() - FEDERATION_HEADER_BYTES;
    CRUCIBLE_PRE(federation_entry_blob_layout_disjoint(hdr.payload_size));
    if (static_cast<std::size_t>(hdr.payload_size) > bytes_after_header) {
        return std::unexpected(FederationError::TruncatedPayload);
    }

    return hdr;
}

// The payload span aliases the input buffer. It stays valid only
// while that buffer does.

struct FederationEntryView {
    FederationEntryHeader header{};
    std::span<const std::uint8_t> payload{};
};

[[nodiscard]] inline std::expected<FederationEntryView, FederationError>
deserialize_untrusted_federation_entry(std::span<const std::uint8_t> in_buf,
                                       std::uint16_t receiver_cardinality) noexcept {
    auto hdr_or_err = deserialize_federation_header(in_buf, receiver_cardinality);
    if (!hdr_or_err) {
        return std::unexpected(hdr_or_err.error());
    }

    const std::size_t payload_offset = FEDERATION_HEADER_BYTES;
    const std::size_t payload_size = static_cast<std::size_t>(hdr_or_err->payload_size);
    return FederationEntryView{
        .header = *hdr_or_err,
        .payload = in_buf.subspan(payload_offset, payload_size),
    };
}

// The peer token comes only from the federation door of
// fixy/Federation.h, so a caller that holds one admitted a peer of Org.
// The tag of the view is the tag of that token.  It records where the
// view came from and is no check: fixy::Tagged lets any caller mint a
// value under this tag.
template <typename Org, typename Brand>
[[nodiscard]] inline std::expected<
    ::fixy::Tagged<FederationEntryView, ::foundation::permissions::tag::FederatedPeer<Org>>, FederationError>
deserialize_federation_entry(
    const ::foundation::permissions::Permission<::foundation::permissions::tag::FederatedPeer<Org>, Brand>&
        peer_permission,
    std::span<const std::uint8_t> in_buf, std::uint16_t receiver_cardinality) noexcept {
    (void)peer_permission;

    auto view = deserialize_untrusted_federation_entry(in_buf, receiver_cardinality);
    if (!view) {
        return std::unexpected(view.error());
    }
    return ::fixy::mint_tagged<::foundation::permissions::tag::FederatedPeer<Org>>(*view);
}

[[nodiscard]] inline constexpr bool federation_accepts_cardinality(std::uint16_t sender_cardinality,
                                                                   std::uint16_t receiver_cardinality) noexcept {
    return sender_cardinality <= receiver_cardinality;
}

// ── The session protocol ─────────────────────────────────────────────
//
// The protocol has three roles.  The sender sends the header of an
// entry to the coordinator, and the coordinator sends an Ack back.  The
// coordinator then sends a pull request to the receiver, and the
// receiver sends the body.  The coordinator is a role of the global
// type, so the protocol is one description of three parties and not two
// binary descriptions with no relation.
//
// The global type is written on fixy/session/Global.h, and each local
// type is its projection (fixy/session/Projection.h).  A carrier states
// its network model (fixy/session/NetworkModel.h).  The protocol has no
// choice, and the coordinator receives from two senders.  So it is
// implementable on per-pair FIFO and on a bag, but not on a mailbox
// (fixy/session/Network.h).  A mint that gives the handle of a role
// must first ask ::fixy::session::CarrierImplements with the global
// type and the carrier.
//
// No production file makes the handle of a role, so this header carries
// the types and no mint.
//
// Old spelling: include/crucible/sessions/_FederationProtocol.h, on the
// old session layer, with no label on a message and with a mint for
// each role.

struct SenderRole {};
struct ReceiverRole {};
struct CoordRole {};

// The label of each message of the global type.
struct HeaderLabel {};
struct AckLabel {};
struct PullLabel {};
struct BodyLabel {};

struct AnyFederationKey {};

template <typename KeyTag = AnyFederationKey>
struct Ack {
    using key_tag = KeyTag;
    ::crucible::KernelCacheKey key{};
};

template <typename KeyTag = AnyFederationKey>
struct PullRequest {
    using key_tag = KeyTag;
    ::crucible::KernelCacheKey key{};
};

template <typename KeyTag = AnyFederationKey>
struct FederationEntryPayload {
    using key_tag = KeyTag;
    std::span<const std::uint8_t> bytes{};
};

template <typename KeyTag = AnyFederationKey>
using HeaderPayload = ::fixy::session::ContentAddressed<FederationEntryHeader>;

template <typename KeyTag = AnyFederationKey>
using BodyPayload = ::fixy::session::ContentAddressed<FederationEntryPayload<KeyTag>>;

template <typename KeyTag = AnyFederationKey>
using FederationGlobal = ::fixy::session::global::Rec<::fixy::session::global::Msg<
    SenderRole, CoordRole, HeaderLabel, HeaderPayload<KeyTag>,
    ::fixy::session::global::Msg<
        CoordRole, SenderRole, AckLabel, Ack<KeyTag>,
        ::fixy::session::global::Msg<
            CoordRole, ReceiverRole, PullLabel, PullRequest<KeyTag>,
            ::fixy::session::global::Msg<ReceiverRole, CoordRole, BodyLabel, BodyPayload<KeyTag>,
                                         ::fixy::session::global::Var>>>>>;

// A projection gives a queue and a local type.  At the start of the
// protocol each queue is empty, so a role protocol is the local type.
template <typename KeyTag = AnyFederationKey>
using SenderProto = typename ::fixy::session::project_t<FederationGlobal<KeyTag>, SenderRole>::local;

template <typename KeyTag = AnyFederationKey>
using ReceiverProto = typename ::fixy::session::project_t<FederationGlobal<KeyTag>, ReceiverRole>::local;

template <typename KeyTag = AnyFederationKey>
using CoordProto = typename ::fixy::session::project_t<FederationGlobal<KeyTag>, CoordRole>::local;

template <typename KeyTag = AnyFederationKey>
using ExpectedSenderProto = ::fixy::session::Loop<
    ::fixy::session::Send<::fixy::session::PeerMsg<CoordRole, HeaderLabel, HeaderPayload<KeyTag>>,
                          ::fixy::session::Recv<::fixy::session::PeerMsg<CoordRole, AckLabel, Ack<KeyTag>>,
                                                ::fixy::session::Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedReceiverProto = ::fixy::session::Loop<
    ::fixy::session::Recv<::fixy::session::PeerMsg<CoordRole, PullLabel, PullRequest<KeyTag>>,
                          ::fixy::session::Send<::fixy::session::PeerMsg<CoordRole, BodyLabel, BodyPayload<KeyTag>>,
                                                ::fixy::session::Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedCoordProto = ::fixy::session::Loop<::fixy::session::Recv<
    ::fixy::session::PeerMsg<SenderRole, HeaderLabel, HeaderPayload<KeyTag>>,
    ::fixy::session::Send<
        ::fixy::session::PeerMsg<SenderRole, AckLabel, Ack<KeyTag>>,
        ::fixy::session::Send<::fixy::session::PeerMsg<ReceiverRole, PullLabel, PullRequest<KeyTag>>,
                              ::fixy::session::Recv<::fixy::session::PeerMsg<ReceiverRole, BodyLabel, BodyPayload<KeyTag>>,
                                                    ::fixy::session::Continue>>>>>;

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
struct role_protocol_matches : std::false_type {};

template <typename KeyTag>
struct role_protocol_matches<SenderRole, SenderProto<KeyTag>, KeyTag> : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<ReceiverRole, ReceiverProto<KeyTag>, KeyTag> : std::true_type {};

template <typename KeyTag>
struct role_protocol_matches<CoordRole, CoordProto<KeyTag>, KeyTag> : std::true_type {};

template <typename Role, typename Proto, typename KeyTag = AnyFederationKey>
inline constexpr bool role_protocol_matches_v = role_protocol_matches<Role, Proto, KeyTag>::value;

static_assert(::fixy::session::global::is_global_well_formed_v<FederationGlobal<>>);
static_assert(::fixy::session::project_t<FederationGlobal<>, SenderRole>::queue::size == 0);
static_assert(::fixy::session::project_t<FederationGlobal<>, ReceiverRole>::queue::size == 0);
static_assert(::fixy::session::project_t<FederationGlobal<>, CoordRole>::queue::size == 0);
static_assert(std::is_same_v<SenderProto<>, ExpectedSenderProto<>>);
static_assert(std::is_same_v<ReceiverProto<>, ExpectedReceiverProto<>>);
static_assert(std::is_same_v<CoordProto<>, ExpectedCoordProto<>>);
static_assert(role_protocol_matches_v<SenderRole, SenderProto<>>);
static_assert(role_protocol_matches_v<ReceiverRole, ReceiverProto<>>);
static_assert(role_protocol_matches_v<CoordRole, CoordProto<>>);
static_assert(!role_protocol_matches_v<CoordRole, SenderProto<>>);

static_assert(::fixy::session::implementable_on_v<FederationGlobal<>, ::fixy::session::Network::PerPairFifo>);
static_assert(::fixy::session::implementable_on_v<FederationGlobal<>, ::fixy::session::Network::Bag>);
static_assert(::fixy::session::network_refusal_v<FederationGlobal<>, ::fixy::session::Network::Mailbox>
              == ::fixy::session::NetworkRefusal::ReceiverHasTwoSenders);

}  // namespace crucible::cipher::federation
