#pragma once

// ── crucible::cipher::federation ────────────────────────────────────
//
// FOUND-I08.  Federation network protocol spec for row-keyed
// KernelCache entries.  Defines the byte-stable WIRE FORMAT (header +
// payload) by which (content_hash, row_hash, compiled-bytes) tuples
// travel between Crucible runs and across organizations — the L16
// Ecosystem layer's federation contract encoded in 32 bytes.
//
// This header is the SPEC, not the transport.  No sockets, no S3, no
// gRPC — just the byte layout, magic, version stamping, and the
// validation rules every transport must apply.  Phase 5 (Cipher cold
// tier + Canopy peer announcements) consumes this spec; until then it
// is the contract that future federation transports MUST honour.
//
// ── Wire format (little-endian, byte-stable) ────────────────────────
//
//  Offset Size  Field                  Notes
//   0      4    magic                  'CFED' LE = 0x44454643
//   4      2    protocol_version       1 (FOUND-I08 v1)
//   6      2    universe_cardinality   OsUniverse::cardinality at write time
//   8      8    content_hash           Family-A persistent hash (Types.h)
//  16      8    row_hash               Family-A persistent hash (FOUND-I02 fold)
//  24      4    payload_size           bytes following the 32-byte header
//  28      4    reserved               must be 0 (future expansion)
//  32+   ...    payload                opaque bytes; integrity by content_hash
//
// Total header = 32 bytes.  Payload follows; its integrity is
// established by the caller-side hash check (payload bytes MUST hash
// to content_hash; that check is the receiver's responsibility, not
// this header's — the spec never sees the payload bytes in raw form).
//
// ── FOUND-I04 append-only Universe extension encoded on the wire ────
//
// `universe_cardinality` is the FOUND-I04 append-only invariant
// (Effect underlying values frozen, new atoms append at next free
// position) lifted from compile-time to wire-format:
//
//   - Sender writes its OsUniverse::cardinality at write time.
//   - Receiver compares the stamp against ITS OsUniverse::cardinality.
//   - Receiver ACCEPTS entries with stamp ≤ its cardinality (older
//     sender, atoms still known: receiver knows every Effect bit the
//     sender used, no silent collision).
//   - Receiver REJECTS entries with stamp > its cardinality (newer
//     sender used Effect atoms the receiver can't interpret — the
//     row_hash bits depend on Effect underlying values the receiver
//     doesn't know about; using such an entry would silently collide
//     with stale row hashes).  Error: UniverseCardinalityTooHigh.
//
// This is the same invariant as the FOUND-I04 cache-invalidation
// witness (test_computation_cache_invalidation.cpp), one layer down:
// instead of a 64-subset compile-time cache key matrix, here the
// 16-bit stamp gates federation acceptance.
//
// ── Sentinel + zero rejection ────────────────────────────────────────
//
// Two KernelCacheKey states must NOT appear in federation traffic:
//
//   - sentinel():  both axes UINT64_MAX — reserved as the open-
//                  addressing empty-slot marker (Types.h:420-426 +
//                  MerkleDag.h::Entry).  Federating a sentinel would
//                  let a peer poison its cache with the sentinel
//                  value, breaking lookup termination.
//   - is_zero():   both axes 0 — typically means "unset" (default-
//                  constructed key); sender forgot to populate.
//                  Federating zero would silently match the most-
//                  vulnerable bucket on the receiver side.
//
// Both are rejected on serialize (refuse to emit) AND on deserialize
// (refuse to accept).  Errors: SentinelKey / ZeroKey.
//
// ── Reserved field discipline ───────────────────────────────────────
//
// The 4-byte `reserved` slot at offset 28 is structural padding +
// future-proofing.  Senders MUST write 0; receivers MUST reject any
// non-zero value.  This is the simple way to gate future protocol
// extensions: when V2 lands and uses these bytes, V1 receivers will
// already reject V2 traffic with a clean error rather than silently
// misinterpret it.  Error: ReservedNonZero.
//
// ── Endianness (CLAUDE.md §XIV) ─────────────────────────────────────
//
// Crucible's platform assumption pins little-endian.  All multi-byte
// fields in this spec are little-endian; on x86-64 / aarch64 + LE the
// implementation is `std::memcpy` directly between the struct and the
// byte buffer.  No byteswap on the supported platforms.  A future big-
// endian port would need explicit `std::byteswap` in the codec; the
// spec layout is the same.
//
// ── Eight-axiom audit (header proper) ───────────────────────────────
//
//   InitSafe — every field has NSDMI; default-constructed header is
//              fully specified (magic = 0, version = 0, payload_size
//              = 0, reserved = 0, hashes default).
//   TypeSafe — strong-typed ContentHash + RowHash; uint32_t / uint16_t
//              widths chosen to match the wire spec exactly.
//   NullSafe — no pointer fields in the header struct.  span<> at the
//              codec API surface (caller-supplied buffers).
//   MemSafe  — header is std::is_standard_layout + trivially copyable;
//              std::memcpy round-trips through the byte buffer
//              losslessly.
//   BorrowSafe — codec functions are pure (no global state); every
//                buffer is caller-owned.
//   ThreadSafe — N/A; the spec is value-semantic.
//   LeakSafe — no resources.
//   DetSafe  — same key + same payload_size → same 32 bytes; the
//              fold is byte-deterministic by construction.

#include <crucible/Types.h>
#include <crucible/effects/OsUniverse.h>

#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>

namespace crucible::cipher::federation {

// ── Magic + version constants ───────────────────────────────────────
//
// 'CFED' as little-endian uint32_t — bytes 0x43 'C', 0x46 'F', 0x45
// 'E', 0x44 'D' in increasing memory address order.  Distinct from
// CDAG_MAGIC (Serialize.h:27) so a federation stream and a Merkle DAG
// snapshot cannot be confused at the magic-check step.
inline constexpr std::uint32_t FEDERATION_MAGIC = 0x44454643u;

// Wire-format protocol version.  V1 ships with FOUND-I08; bumps
// follow when the layout changes.  Receivers reject mismatched
// versions cleanly (UnsupportedVersion).  No silent fallback: a V2
// header with V1 layout would silently misinterpret 4 bytes of the
// reserved field, hence the strict equality check at decode.
inline constexpr std::uint16_t FEDERATION_PROTOCOL_V1 = 1u;

// ── Header struct (32 bytes, byte-stable) ───────────────────────────

struct FederationEntryHeader {
    std::uint32_t magic                = 0;     // FEDERATION_MAGIC
    std::uint16_t protocol_version     = 0;     // FEDERATION_PROTOCOL_V1
    std::uint16_t universe_cardinality = 0;     // OsUniverse::cardinality at write
    ContentHash   content_hash{};               // strong ID (Types.h)
    RowHash       row_hash{};                   // strong ID (Types.h)
    std::uint32_t payload_size         = 0;     // bytes of payload following
    std::uint32_t reserved             = 0;     // must be 0
};

// ── Layout invariants ───────────────────────────────────────────────
//
// The header is a fixed 32-byte struct; the codec writes it as one
// std::memcpy on platforms with the right alignment + endianness.
// Field offsets are pinned so that future codec implementers can
// reach into the bytes by offset without re-parsing the struct
// declaration.
static_assert(sizeof(FederationEntryHeader) == 32,
    "FederationEntryHeader must be exactly 32 bytes — the wire-format "
    "header size.  Adding a field requires bumping FEDERATION_PROTOCOL_V1 "
    "and updating every receiver — see the V2 migration discipline.");
static_assert(alignof(FederationEntryHeader) == 8,
    "FederationEntryHeader must be 8-byte aligned (the natural "
    "alignment of the embedded ContentHash + RowHash).");
static_assert(std::is_standard_layout_v<FederationEntryHeader>,
    "FederationEntryHeader must be standard-layout to permit "
    "offsetof + std::memcpy-based wire codec.");
static_assert(std::is_trivially_copyable_v<FederationEntryHeader>,
    "FederationEntryHeader must be trivially copyable to permit "
    "std::memcpy round-trip through the byte buffer.");

static_assert(offsetof(FederationEntryHeader, magic)                ==  0);
static_assert(offsetof(FederationEntryHeader, protocol_version)     ==  4);
static_assert(offsetof(FederationEntryHeader, universe_cardinality) ==  6);
static_assert(offsetof(FederationEntryHeader, content_hash)         ==  8);
static_assert(offsetof(FederationEntryHeader, row_hash)             == 16);
static_assert(offsetof(FederationEntryHeader, payload_size)         == 24);
static_assert(offsetof(FederationEntryHeader, reserved)             == 28);

// ── Header byte-size constant ───────────────────────────────────────
inline constexpr std::size_t FEDERATION_HEADER_BYTES =
    sizeof(FederationEntryHeader);

// ── Magic byte order witness ────────────────────────────────────────
//
// Pin the byte order of the magic constant so a refactor that swaps
// to big-endian or changes the ASCII spelling fails loud at compile
// time.  The bytes in memory MUST be 'C','F','E','D' in increasing
// address order — that's the order a receiver scanning a wire stream
// sees them.
static_assert((FEDERATION_MAGIC      & 0xFFu) == 'C',
    "FEDERATION_MAGIC byte 0 must be 'C'.");
static_assert(((FEDERATION_MAGIC>> 8) & 0xFFu) == 'F',
    "FEDERATION_MAGIC byte 1 must be 'F'.");
static_assert(((FEDERATION_MAGIC>>16) & 0xFFu) == 'E',
    "FEDERATION_MAGIC byte 2 must be 'E'.");
static_assert(((FEDERATION_MAGIC>>24) & 0xFFu) == 'D',
    "FEDERATION_MAGIC byte 3 must be 'D'.");

// ── Universe cardinality field width witness ────────────────────────
//
// uint16_t has range [0, 65535].  EffectRowLattice's carrier is
// std::uint64_t, so cardinality is structurally bounded by 64 (per
// EffectRowLattice.h:97 static_assert).  64 fits comfortably; the
// 2-byte field gives ~3 orders of magnitude of headroom for future
// universes that compose multiple lattices into a wider catalog.
static_assert(::crucible::effects::OsUniverse::cardinality
              <= std::uint16_t{0xFFFF},
    "OsUniverse::cardinality must fit in the uint16_t wire field.");

// ── Defensive cross-stream magic collision guard ────────────────────
//
// FOUND-I08-AUDIT (Finding G).  Crucible has TWO distinct binary
// stream formats with magic words: this protocol (FEDERATION_MAGIC,
// 'CFED') and the Merkle DAG snapshot (CDAG_MAGIC, 'GDAG' =
// 0x43444147, defined in Serialize.h:27).  A receiver that mis-
// dispatches a federation byte stream to the CDAG codec — or vice
// versa — would silently misinterpret 28 bytes of header before
// hitting a content mismatch.  Pin the magic-distinctness invariant
// HERE (the literal value of CDAG_MAGIC inlined, not pulled via
// Serialize.h to keep this header lightweight); the test side
// includes both headers and asserts the constants disagree.
//
// A future protocol that wants to add a new magic must update both
// (a) this static_assert with the new constant, and (b) the test
// side's cross-magic table.  If CDAG_MAGIC is ever reassigned to a
// value matching FEDERATION_MAGIC, the runtime witness in
// test_federation_protocol.cpp::test_magic_collision_with_cdag fails.
static_assert(FEDERATION_MAGIC != 0x43444147u,
    "FEDERATION_MAGIC must not collide with CDAG_MAGIC ('GDAG' LE) — "
    "a federation stream and a Merkle DAG snapshot must dispatch to "
    "different codecs at the magic-check step.  See Serialize.h:27.");

// ── Payload-size field cap pin ──────────────────────────────────────
//
// FOUND-I08-AUDIT (Finding F).  payload_size is uint32_t; the
// natural cap is std::numeric_limits<std::uint32_t>::max() = 4 GiB - 1.
// A federation transport that wants to ship larger artifacts must
// fragment them into multiple entries (or bump the protocol version
// to V2 with a 64-bit payload_size).  The serialize-side check
// (`payload.size() > UINT32_MAX → reject`) is the runtime guard;
// this static_assert pins the structural cap so the compile-time
// invariant cannot drift if the field width ever changes.
static_assert(sizeof(FederationEntryHeader::payload_size) == 4,
    "payload_size MUST be a 32-bit field — caps the per-entry "
    "payload at 4 GiB.  Larger payloads must fragment into multiple "
    "entries or bump to a V2 protocol with 64-bit payload_size.");
static_assert(sizeof(FederationEntryHeader::universe_cardinality) == 2,
    "universe_cardinality MUST be a 16-bit field — caps the Effect "
    "atom catalog at 65535 entries (well above EffectRowLattice's "
    "structural cap of 64 from the uint64_t carrier).");
static_assert(sizeof(FederationEntryHeader::magic) == 4,
    "magic MUST be a 32-bit field — pinned for byte-stable cross-"
    "platform protocol identification.");
static_assert(sizeof(FederationEntryHeader::protocol_version) == 2,
    "protocol_version MUST be a 16-bit field — supports up to 65536 "
    "wire-format revisions.");
static_assert(sizeof(FederationEntryHeader::reserved) == 4,
    "reserved MUST be a 32-bit field — preserves V1 → V2 layout "
    "compatibility (V2 fields can use this slot).");

// ── Error codes ─────────────────────────────────────────────────────
//
// FederationError is the structured error channel returned by
// serialize / deserialize.  Each error names exactly one rejection
// rule from the spec above.  Single-byte underlying type keeps the
// std::expected<size_t, FederationError> return value compact.

enum class FederationError : std::uint8_t {
    None                       = 0,
    BadMagic                   = 1,  // header magic != FEDERATION_MAGIC
    UnsupportedVersion         = 2,  // protocol_version != V1
    UniverseCardinalityTooHigh = 3,  // sender used atoms receiver lacks
    SentinelKey                = 4,  // KernelCacheKey is sentinel()
    ZeroKey                    = 5,  // KernelCacheKey is_zero()
    ReservedNonZero            = 6,  // reserved field != 0
    TruncatedHeader            = 7,  // out_buf < FEDERATION_HEADER_BYTES
    TruncatedPayload           = 8,  // declared payload_size > remaining bytes
    OutputBufferTooSmall       = 9,  // out_buf < header + payload_size
};

// ── Diagnostic name forwarder (FOUND-E18 row-mismatch surface) ──────
[[nodiscard]] inline constexpr std::string_view
federation_error_name(FederationError e) noexcept {
    switch (e) {
        case FederationError::None:                       return "None";
        case FederationError::BadMagic:                   return "BadMagic";
        case FederationError::UnsupportedVersion:         return "UnsupportedVersion";
        case FederationError::UniverseCardinalityTooHigh: return "UniverseCardinalityTooHigh";
        case FederationError::SentinelKey:                return "SentinelKey";
        case FederationError::ZeroKey:                    return "ZeroKey";
        case FederationError::ReservedNonZero:            return "ReservedNonZero";
        case FederationError::TruncatedHeader:            return "TruncatedHeader";
        case FederationError::TruncatedPayload:           return "TruncatedPayload";
        case FederationError::OutputBufferTooSmall:       return "OutputBufferTooSmall";
        default:                                          return "<unknown FederationError>";
    }
}

// ── Serialize ───────────────────────────────────────────────────────
//
// Encode a (key, payload) pair into out_buf.  Returns the number of
// bytes written (header + payload) on success, or a FederationError
// describing the rejection rule that fired.
//
// Pre-conditions enforced by the body (NOT contract-pre, since errors
// are part of the API surface — the caller routes them through
// std::expected, NOT std::terminate):
//
//   - out_buf must have at least FEDERATION_HEADER_BYTES + payload.size().
//   - key must not be sentinel() and must not be is_zero().
//   - payload.size() must fit in uint32_t (we cap it at UINT32_MAX
//     by the field type; a payload larger than 4 GiB is structurally
//     rejected by truncation, see the field-width discipline below).
//
// The writer pins the universe_cardinality stamp from the local
// OsUniverse; receivers compare against their own cardinality.

[[nodiscard]] inline std::expected<std::size_t, FederationError>
serialize_federation_entry(std::span<std::uint8_t> out_buf,
                           const KernelCacheKey&    key,
                           std::span<const std::uint8_t> payload) noexcept {
    // Sentinel + zero rejection — must not appear in federation traffic.
    if (key.is_sentinel()) {
        return std::unexpected(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return std::unexpected(FederationError::ZeroKey);
    }

    // Field-width discipline: payload_size is uint32_t.  A payload
    // larger than 4 GiB cannot fit; we reject before allocating.
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(FederationError::OutputBufferTooSmall);
    }

    // Total bytes required = 32 (header) + payload.size().
    const std::size_t total_bytes =
        FEDERATION_HEADER_BYTES + payload.size();
    if (out_buf.size() < total_bytes) {
        return std::unexpected(FederationError::OutputBufferTooSmall);
    }

    // Build the header.
    FederationEntryHeader hdr{};
    hdr.magic                = FEDERATION_MAGIC;
    hdr.protocol_version     = FEDERATION_PROTOCOL_V1;
    hdr.universe_cardinality =
        static_cast<std::uint16_t>(::crucible::effects::OsUniverse::cardinality);
    hdr.content_hash         = key.content_hash;
    hdr.row_hash             = key.row_hash;
    hdr.payload_size         = static_cast<std::uint32_t>(payload.size());
    hdr.reserved             = 0;

    // Header → buffer.  std::memcpy is the only legal way under
    // -fno-strict-aliasing-violations for non-byte structs (CLAUDE.md
    // §II MemSafe — std::start_lifetime_as is for arena type-punning,
    // not byte-codec).
    std::memcpy(out_buf.data(), &hdr, FEDERATION_HEADER_BYTES);

    // Payload → buffer (after header).  An empty payload is fine —
    // a zero-byte payload is a valid federation entry that announces
    // (content_hash, row_hash) without bytes (the receiver looks up
    // the artifact via content_hash from its own store, e.g. for
    // dedup confirmation).
    if (!payload.empty()) {
        std::memcpy(out_buf.data() + FEDERATION_HEADER_BYTES,
                    payload.data(),
                    payload.size());
    }

    return total_bytes;
}

// ── Deserialize header ──────────────────────────────────────────────
//
// Decode a 32-byte header from in_buf.  Returns the header on success
// or a FederationError on rejection.  The caller is responsible for
// reading the payload (header.payload_size bytes immediately after
// the header) and validating its hash matches header.content_hash.
//
// receiver_cardinality is the local OsUniverse::cardinality — passed
// explicitly rather than read from a global so the function is
// trivially testable across hypothetical newer/older receivers.
//
// Validation order matters: cheaper checks (truncation, magic,
// version) fire before expensive (key normalization).  Each check is
// short-circuited; the first rejection wins.

[[nodiscard]] inline std::expected<FederationEntryHeader, FederationError>
deserialize_federation_header(std::span<const std::uint8_t> in_buf,
                              std::uint16_t receiver_cardinality) noexcept {
    // Truncation: the buffer must hold at least the 32-byte header.
    if (in_buf.size() < FEDERATION_HEADER_BYTES) {
        return std::unexpected(FederationError::TruncatedHeader);
    }

    // Pull the header out of the buffer.  std::memcpy is the only
    // legal way (see serialize comment).
    FederationEntryHeader hdr{};
    std::memcpy(&hdr, in_buf.data(), FEDERATION_HEADER_BYTES);

    // Magic.
    if (hdr.magic != FEDERATION_MAGIC) {
        return std::unexpected(FederationError::BadMagic);
    }

    // Protocol version — strict equality, no silent fallback.
    if (hdr.protocol_version != FEDERATION_PROTOCOL_V1) {
        return std::unexpected(FederationError::UnsupportedVersion);
    }

    // Reserved field — strict zero, no silent acceptance.
    if (hdr.reserved != 0u) {
        return std::unexpected(FederationError::ReservedNonZero);
    }

    // Universe cardinality — receiver must understand every atom the
    // sender used.  The append-only invariant (FOUND-I04) means
    // sender_cardinality ≤ receiver_cardinality is the safe direction.
    if (hdr.universe_cardinality > receiver_cardinality) {
        return std::unexpected(FederationError::UniverseCardinalityTooHigh);
    }

    // Sentinel + zero key — must not appear in federation traffic.
    const KernelCacheKey key{hdr.content_hash, hdr.row_hash};
    if (key.is_sentinel()) {
        return std::unexpected(FederationError::SentinelKey);
    }
    if (key.is_zero()) {
        return std::unexpected(FederationError::ZeroKey);
    }

    // Truncated payload: declared size > bytes remaining after header.
    const std::size_t bytes_after_header =
        in_buf.size() - FEDERATION_HEADER_BYTES;
    if (static_cast<std::size_t>(hdr.payload_size) > bytes_after_header) {
        return std::unexpected(FederationError::TruncatedPayload);
    }

    return hdr;
}

// ── Deserialize header + payload span ───────────────────────────────
//
// Convenience overload: returns both the header AND a span pointing
// into in_buf for the payload bytes.  The span aliases the input
// buffer; caller must not let in_buf go out of scope before consuming
// the payload.

struct FederationEntryView {
    FederationEntryHeader            header{};
    std::span<const std::uint8_t>    payload{};
};

[[nodiscard]] inline std::expected<FederationEntryView, FederationError>
deserialize_federation_entry(std::span<const std::uint8_t> in_buf,
                             std::uint16_t receiver_cardinality) noexcept {
    auto hdr_or_err =
        deserialize_federation_header(in_buf, receiver_cardinality);
    if (!hdr_or_err) {
        return std::unexpected(hdr_or_err.error());
    }

    const std::size_t payload_offset = FEDERATION_HEADER_BYTES;
    const std::size_t payload_size   =
        static_cast<std::size_t>(hdr_or_err->payload_size);
    return FederationEntryView{
        .header  = *hdr_or_err,
        .payload = in_buf.subspan(payload_offset, payload_size),
    };
}

// ── Federation-acceptance predicate (helper) ────────────────────────
//
// True iff the sender's universe_cardinality is ≤ the receiver's.
// Pure consteval-friendly predicate for compile-time validation in
// scenarios where the cardinality is statically known on both sides
// (typical for fleet-wide same-binary federation).
[[nodiscard]] inline constexpr bool
federation_accepts_cardinality(std::uint16_t sender_cardinality,
                               std::uint16_t receiver_cardinality) noexcept {
    return sender_cardinality <= receiver_cardinality;
}

}  // namespace crucible::cipher::federation
