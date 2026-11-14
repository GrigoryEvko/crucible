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
#include <fixy/Core.h>
#include <fixy/Federation.h>
#include <fixy/Tagged.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Global.h>
#include <fixy/session/Network.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::cipher::federation {

// The bytes in memory read 'C', 'F', 'E', 'D' in increasing address
// order, which is the order a receiver scanning a stream meets them.
inline constexpr std::uint32_t FEDERATION_MAGIC = 0x44454643u;

// A version is compared for equality and never fallen back from. A
// later header read under this layout would take four bytes of the
// reserved word for something else.
inline constexpr std::uint16_t FEDERATION_PROTOCOL_V1 = 1u;

// The count of effect atoms of this build: the stamp that a sender writes,
// and the count that a receiver of this build states.  The check file of
// this header states that the count fits the 16-bit field.
inline constexpr std::uint16_t FEDERATION_UNIVERSE_CARDINALITY =
    static_cast<std::uint16_t>(::foundation::effects::effect_count);

struct FederationEntryHeader {
    std::uint32_t magic = 0;
    std::uint16_t protocol_version = 0;
    std::uint16_t universe_cardinality = 0;
    ContentHash content_hash{};
    RowHash row_hash{};
    std::uint32_t payload_size = 0;
    std::uint32_t reserved = 0;
};

inline constexpr std::size_t FEDERATION_HEADER_BYTES = sizeof(FederationEntryHeader);

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

// The name is the enumerator's identifier, read by reflection, so a new
// error cannot be added without a name.  A value that no enumerator
// holds prints "<unknown FederationError>".
[[nodiscard]] inline constexpr ::fixy::TextView federation_error_name(FederationError e) noexcept {
    return ::foundation::core::enum_name_text(e);
}

// The rejection rules are checked in the body and reported, rather
// than asserted as preconditions. A caller that hands over a buffer
// too small, or a key it forgot to fill in, deserves an error it can
// route, not a terminated process.

[[nodiscard]] inline ::fixy::Result<std::size_t, FederationError>
serialize_federation_entry(::fixy::View<std::byte> out_buf, const KernelCacheKey& key,
                           ::fixy::View<std::byte const> payload) noexcept {
    if (key.is_sentinel()) {
        return ::fixy::err(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return ::fixy::err(FederationError::ZeroKey);
    }

    // The payload size is a 32-bit field of the header.
    CRUCIBLE_TRY(payload_size,
                 ::fixy::narrow<std::uint32_t>(payload.size()).ok_or(FederationError::OutputBufferTooSmall));

    const std::size_t total_bytes = FEDERATION_HEADER_BYTES + payload.size();
    if (out_buf.size() < total_bytes) {
        return ::fixy::err(FederationError::OutputBufferTooSmall);
    }

    const FederationEntryHeader hdr{
        .magic = FEDERATION_MAGIC,
        .protocol_version = FEDERATION_PROTOCOL_V1,
        .universe_cardinality = FEDERATION_UNIVERSE_CARDINALITY,
        .content_hash = key.content_hash,
        .row_hash = key.row_hash,
        .payload_size = payload_size,
        .reserved = 0,
    };

    // A byte codec copies the bytes of the header. Starting a lifetime
    // at the address is the tool for arena type-punning and would be
    // wrong here.
    ::fixy::copy(out_buf.window<FEDERATION_HEADER_BYTES>(0).expect("the size check puts the header inside the buffer"),
                 ::fixy::bytes(hdr));

    // An entry with no payload bytes is valid and meaningful. It
    // announces the pair of hashes, and the receiver looks the
    // artifact up in its own store from the content hash.
    if (payload.size() != 0) {
        ::fixy::copy(out_buf.window(FEDERATION_HEADER_BYTES, payload.size())
                         .expect("the size check puts the payload inside the buffer"),
                     payload)
            .expect("the window holds the count of the payload");
    }

    return total_bytes;
}

// The receiver's own atom count arrives as an argument instead of
// being read from a global, so a test can stand in as a newer or an
// older peer.
//
// The caller reads the payload itself and checks that it hashes to
// the content hash in the returned header.

[[nodiscard]] inline ::fixy::Result<FederationEntryHeader, FederationError>
deserialize_federation_header(::fixy::View<std::byte const> in_buf, std::uint16_t receiver_cardinality) noexcept {
    CRUCIBLE_TRY(header_bytes, in_buf.window<FEDERATION_HEADER_BYTES>(0).ok_or(FederationError::TruncatedHeader));

    // Each bit pattern of the header is a value, so the copy of its bytes
    // cannot fail.  The checks below read the fields.
    ::foundation::core::FixedArray<std::byte, FEDERATION_HEADER_BYTES> image{};
    ::fixy::copy(image.view(), header_bytes);
    const FederationEntryHeader hdr = ::fixy::bit_cast<FederationEntryHeader>(image);

    if (hdr.magic != FEDERATION_MAGIC) {
        return ::fixy::err(FederationError::BadMagic);
    }

    if (hdr.protocol_version != FEDERATION_PROTOCOL_V1) {
        return ::fixy::err(FederationError::UnsupportedVersion);
    }

    if (hdr.reserved != 0u) {
        return ::fixy::err(FederationError::ReservedNonZero);
    }

    // A sender at or below this count used only atoms this receiver
    // already knows, which is the safe direction of the append-only
    // rule.
    if (hdr.universe_cardinality > receiver_cardinality) {
        return ::fixy::err(FederationError::UniverseCardinalityTooHigh);
    }

    const KernelCacheKey key{hdr.content_hash, hdr.row_hash};
    if (key.is_sentinel()) {
        return ::fixy::err(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return ::fixy::err(FederationError::ZeroKey);
    }

    const std::size_t bytes_after_header = in_buf.size() - FEDERATION_HEADER_BYTES;
    if (hdr.payload_size > bytes_after_header) {
        return ::fixy::err(FederationError::TruncatedPayload);
    }

    return hdr;
}

// The payload View borrows the input buffer. It stays valid only
// while that buffer does.

struct FederationEntryView {
    FederationEntryHeader header{};
    ::fixy::View<std::byte const> payload{};
};

[[nodiscard]] inline ::fixy::Result<FederationEntryView, FederationError>
deserialize_untrusted_federation_entry(::fixy::View<std::byte const> in_buf,
                                       std::uint16_t receiver_cardinality) noexcept {
    CRUCIBLE_TRY(header, deserialize_federation_header(in_buf, receiver_cardinality));

    return FederationEntryView{
        .header = header,
        .payload = in_buf.window(FEDERATION_HEADER_BYTES, header.payload_size)
                       .expect("the header check puts the payload inside the buffer"),
    };
}

// The peer token comes only from the federation door of
// fixy/Federation.h, so a caller that holds one admitted a peer of Org.
// The tag of the view is the tag of that token.  It records where the
// view came from and is no check: fixy::Tagged lets any caller mint a
// value under this tag.
template <typename Org, typename Brand>
[[nodiscard]] inline ::fixy::Result<
    ::fixy::Tagged<FederationEntryView, ::foundation::permissions::tag::FederatedPeer<Org>>, FederationError>
deserialize_federation_entry(const ::foundation::permissions::Permission<
                                 ::foundation::permissions::tag::FederatedPeer<Org>, Brand>& peer_permission,
                             ::fixy::View<std::byte const> in_buf, std::uint16_t receiver_cardinality) noexcept {
    (void)peer_permission;

    CRUCIBLE_TRY(view, deserialize_untrusted_federation_entry(in_buf, receiver_cardinality));
    return ::fixy::mint_tagged<::foundation::permissions::tag::FederatedPeer<Org>>(view);
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
    ::fixy::View<std::byte const> bytes{};
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
        ::fixy::session::global::Msg<CoordRole, ReceiverRole, PullLabel, PullRequest<KeyTag>,
                                     ::fixy::session::global::Msg<ReceiverRole, CoordRole, BodyLabel,
                                                                  BodyPayload<KeyTag>,
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
using ExpectedSenderProto = ::fixy::session::Loop<::fixy::session::Send<
    ::fixy::session::PeerMsg<CoordRole, HeaderLabel, HeaderPayload<KeyTag>>,
    ::fixy::session::Recv<::fixy::session::PeerMsg<CoordRole, AckLabel, Ack<KeyTag>>, ::fixy::session::Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedReceiverProto = ::fixy::session::Loop<
    ::fixy::session::Recv<::fixy::session::PeerMsg<CoordRole, PullLabel, PullRequest<KeyTag>>,
                          ::fixy::session::Send<::fixy::session::PeerMsg<CoordRole, BodyLabel, BodyPayload<KeyTag>>,
                                                ::fixy::session::Continue>>>;

template <typename KeyTag = AnyFederationKey>
using ExpectedCoordProto = ::fixy::session::Loop<::fixy::session::Recv<
    ::fixy::session::PeerMsg<SenderRole, HeaderLabel, HeaderPayload<KeyTag>>,
    ::fixy::session::Send<::fixy::session::PeerMsg<SenderRole, AckLabel, Ack<KeyTag>>,
                          ::fixy::session::Send<::fixy::session::PeerMsg<ReceiverRole, PullLabel, PullRequest<KeyTag>>,
                                                ::fixy::session::Recv<::fixy::session::PeerMsg<ReceiverRole, BodyLabel,
                                                                                               BodyPayload<KeyTag>>,
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

}  // namespace crucible::cipher::federation
