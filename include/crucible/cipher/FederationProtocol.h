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

#include <crucible/Types.h>
#include <crucible/effects/OsUniverse.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Tagged.h>

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

template <typename Org>
[[nodiscard]] inline std::expected<
    ::crucible::safety::Tagged<FederationEntryView, ::crucible::safety::source::FederatedPeer<Org>>, FederationError>
deserialize_federation_entry(const ::crucible::permissions::FederatedPeerPermission<Org>& peer_permission,
                             std::span<const std::uint8_t> in_buf, std::uint16_t receiver_cardinality) noexcept {
    (void)peer_permission;

    auto view = deserialize_untrusted_federation_entry(in_buf, receiver_cardinality);
    if (!view) {
        return std::unexpected(view.error());
    }
    return ::crucible::safety::Tagged<FederationEntryView, ::crucible::safety::source::FederatedPeer<Org>>{*view};
}

[[nodiscard]] inline constexpr bool federation_accepts_cardinality(std::uint16_t sender_cardinality,
                                                                   std::uint16_t receiver_cardinality) noexcept {
    return sender_cardinality <= receiver_cardinality;
}

}  // namespace crucible::cipher::federation
