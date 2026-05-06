#pragma once

// ── crucible::cipher::ComputationCacheFederation ────────────────────
//
// FOUND-F12.  The bridge between F11's row-aware ComputationCache
// (per-(FnPtr, Row, Args...) atomic-slot in-process cache) and I08's
// federation wire-format (32-byte FederationEntryHeader + opaque
// payload).  When a ComputationCache hit becomes a federation
// candidate (cross-process / cross-org sharing of compiled bodies),
// this header projects the cache's 64-bit `computation_cache_key_in_row`
// into the federation `KernelCacheKey { ContentHash, RowHash }` shape
// and threads it through the I08 serialize/deserialize codec.
//
// ── Key projection contract ─────────────────────────────────────────
//
//   federation_content_hash<FnPtr, Row, Args...> :=
//       ContentHash{ computation_cache_key_in_row<FnPtr, Row, Args...> }
//
//   federation_row_hash<Row> :=
//       RowHash{ row_hash_contribution_v<Row> }
//
//   federation_key<FnPtr, Row, Args...> :=
//       KernelCacheKey{ federation_content_hash, federation_row_hash }
//
// The content axis bundles function-name + function-type +
// row-hash-contribution + args-type-hashes (per F11's
// computation_cache_key_in_row fold).  The row axis ALSO carries
// the row hash separately — the I08 receiver uses the row axis to
// route entries by effect-row independent of the content axis.
//
// This double-counting (row hash appears in BOTH axes) is intentional:
//   * The content axis's row contribution makes ComputationCache slot
//     identity row-aware in the in-process cache (F11 invariant).
//   * The row axis's row hash lets a federation receiver route entries
//     to the correct cache shard / accept-reject by row policy
//     WITHOUT decoding the full content hash.
//
// ── CompiledBody payload opacity ────────────────────────────────────
//
// CompiledBody is forward-declared in ComputationCache.h; its
// concrete representation is owned by the dispatcher.  Federation
// cannot serialize CompiledBody directly (the protocol doesn't know
// what's inside).  Instead the dispatcher provides byte-serialized
// payload AHEAD OF the federation call:
//
//     auto bytes = dispatcher_serialize(my_compiled_body);
//     auto written = serialize_computation_cache_federation_entry<
//         &fn, Row<>, int>(out_buf, bytes);
//
// The federation header pins the (FnPtr, Row, Args...) identity at
// the wire level; the receiver's dispatcher_deserialize() reads the
// payload bytes and reconstructs its own CompiledBody.
//
// ── 8-axiom audit (per the wrapper-policy template) ─────────────────
//
//   InitSafe   — every constexpr returns a fully-specified value;
//                no NSDMI gaps.
//   TypeSafe   — strong-typed ContentHash + RowHash + KernelCacheKey;
//                Row is concept-fenced via IsEffectRow; FnPtr is
//                concept-fenced via IsCacheableFunction.
//   NullSafe   — no pointer fields; std::span at the codec boundary.
//   MemSafe    — no allocations; codec is std::memcpy through caller-
//                owned buffers.
//   BorrowSafe — view returned from deserialize aliases the input
//                buffer (documented lifetime contract from I08).
//   ThreadSafe — N/A (value-semantic codec).
//   LeakSafe   — no resources held by the federation primitives.
//   DetSafe    — same (FnPtr, Row, Args...) → same federation key,
//                bit-stable within one build (inherited from F11
//                computation_cache_key_in_row + I02 row_hash_fold).

#include <crucible/Types.h>                          // KernelCacheKey, ContentHash, RowHash
#include <crucible/cipher/ComputationCache.h>        // computation_cache_key_in_row, IsCacheableFunction, IsEffectRow
#include <crucible/cipher/FederationProtocol.h>      // FederationEntryHeader, codec, FederationError
#include <crucible/safety/diag/RowHashFold.h>        // row_hash_contribution_v
#include <crucible/sessions/FederationProtocol.h>    // typed Sender/Receiver/Coord MPST facade

#include <cstdint>
#include <expected>
#include <span>

namespace crucible::cipher::federation {

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
struct ComputationCacheFederationKeyTag {
    [[maybe_unused]] static constexpr auto fn = FnPtr;
    using row_type = Row;
};

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationSenderProto =
    ::crucible::safety::proto::federation::SenderProto<
        ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationReceiverProto =
    ::crucible::safety::proto::federation::ReceiverProto<
        ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationCoordProto =
    ::crucible::safety::proto::federation::CoordProto<
        ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <typename Payload>
class [[nodiscard]] ContentAddressedFederationPayload {
 public:
    using value_type = Payload;
    using payload_type =
        ::crucible::safety::proto::ContentAddressed<Payload>;

    constexpr ContentAddressedFederationPayload() noexcept = default;
    constexpr explicit ContentAddressedFederationPayload(
        std::span<const std::uint8_t> bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] static constexpr ContentAddressedFederationPayload
    hash_only() noexcept {
        return ContentAddressedFederationPayload{};
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t>
    bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] constexpr bool elides_wire_bytes() const noexcept {
        return bytes_.empty();
    }

 private:
    std::span<const std::uint8_t> bytes_{};
};

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationPayload =
    ::crucible::safety::proto::federation::FederationEntryPayload<
        ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationContentAddressedPayload =
    ContentAddressedFederationPayload<
        ComputationCacheFederationPayload<FnPtr, Row, Args...>>;

// ═════════════════════════════════════════════════════════════════════
// ── Per-axis projections ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// Content axis — F11's 64-bit cache key projected into ContentHash.
// Inherits all F11 invariants:
//   * function-name + function-type + row-hash + args-types fold
//   * order-sensitive in Args
//   * permutation-invariant in Row's effect pack (sort-fold)
//   * non-zero by construction (hash_name seed is non-zero)
//   * distinct from row-blind cache key (slot isolation invariant)
template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] inline constexpr ContentHash
federation_content_hash() noexcept {
    return ContentHash{
        computation_cache_key_in_row<FnPtr, Row, Args...>};
}

// Row axis — I02's row hash contribution lifted into RowHash.
// Inherits all I02 invariants:
//   * sort-fold over Effect underlying values (permutation-invariant)
//   * cardinality-seeded (Row<> ≠ 0; F11 invariant)
//   * monotone in row content (adding atoms changes the hash)
template <typename Row>
    requires IsEffectRow<Row>
[[nodiscard]] inline constexpr RowHash
federation_row_hash() noexcept {
    return RowHash{
        ::crucible::safety::diag::row_hash_contribution_v<Row>};
}

// Composite federation key.  This is the pair the I08 wire format
// uses as the (content, row) header field.
template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] inline constexpr KernelCacheKey
federation_key() noexcept {
    return KernelCacheKey{
        federation_content_hash<FnPtr, Row, Args...>(),
        federation_row_hash<Row>(),
    };
}

// ═════════════════════════════════════════════════════════════════════
// ── Codec wrappers ──────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Forward to the I08 codec with the projected key.  The payload is
// the dispatcher-serialized CompiledBody bytes; the federation layer
// is opaque to its content.

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] inline std::expected<std::size_t, FederationError>
serialize_computation_cache_federation_entry(
    std::span<std::uint8_t> out_buf,
    ComputationCacheFederationContentAddressedPayload<
        FnPtr, Row, Args...> dispatcher_payload) noexcept {
    return serialize_federation_entry(
        out_buf,
        federation_key<FnPtr, Row, Args...>(),
        dispatcher_payload.bytes());
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
[[nodiscard]] inline std::expected<std::size_t, FederationError>
serialize_computation_cache_federation_entry(
    std::span<std::uint8_t> out_buf,
    std::span<const std::uint8_t> dispatcher_payload) noexcept {
    return serialize_computation_cache_federation_entry<FnPtr, Row, Args...>(
        out_buf,
        ComputationCacheFederationContentAddressedPayload<
            FnPtr, Row, Args...>{dispatcher_payload});
}

// Deserialize is delegated to the I08 codec — the (FnPtr, Row,
// Args...) identity is pinned at the WRITE site; the receiver
// reconstructs the entry via header.content_hash + header.row_hash
// and does its own (FnPtr, Row, Args...) lookup against the local
// computation_cache_key_in_row table.
//
// We re-export the I08 deserializer name into this namespace so
// callers don't need to mix `cipher::federation::` and
// `cipher::federation::serialize_computation_cache_*` styles.
using ::crucible::cipher::federation::deserialize_federation_entry;
using ::crucible::cipher::federation::deserialize_federation_header;

// ═════════════════════════════════════════════════════════════════════
// ── Header byte size constant (re-export for codec callers) ────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::cipher::federation::FEDERATION_HEADER_BYTES;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block — invariants asserted at header inclusion ──────
// ═════════════════════════════════════════════════════════════════════

namespace detail::computation_cache_federation_self_test {

// Reuse fixtures from ComputationCache's self-test namespace.  Both
// headers ship in the same TU when this file is included; no symbol
// collision because we declare new in-namespace fixtures.

inline void f12_p_unary(int) noexcept {}
inline void f12_p_binary(int, double) noexcept {}
inline void f12_p_void() noexcept {}

namespace eff_local = ::crucible::effects;
using EmptyR  = eff_local::Row<>;
using BgR     = eff_local::Row<eff_local::Effect::Bg>;
using IOR     = eff_local::Row<eff_local::Effect::IO>;
using BgIOR   = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;

// ── Federation key non-zero ───────────────────────────────────────

static_assert(federation_content_hash<&f12_p_unary, EmptyR, int>().raw() != 0,
    "F12: federation content hash must be non-zero (inherits F11's "
    "non-zero-by-construction invariant from hash_name seed).");
static_assert(federation_row_hash<EmptyR>().raw() != 0,
    "F12: federation row hash for EmptyRow must be non-zero (I02's "
    "cardinality-seeded fold ensures Row<> ≠ 0).");
static_assert(!federation_key<&f12_p_unary, EmptyR, int>().is_zero(),
    "F12: composite federation key must not be the zero-key sentinel.");
static_assert(!federation_key<&f12_p_unary, EmptyR, int>().is_sentinel(),
    "F12: composite federation key must not be the UINT64_MAX-pair "
    "sentinel reserved for cache empty-slot probing.");
static_assert(::crucible::safety::proto::is_well_formed_v<
    ComputationCacheFederationSenderProto<&f12_p_unary, EmptyR, int>>);
static_assert(::crucible::safety::proto::is_well_formed_v<
    ComputationCacheFederationReceiverProto<&f12_p_unary, EmptyR, int>>);
static_assert(::crucible::safety::proto::is_well_formed_v<
    ComputationCacheFederationCoordProto<&f12_p_unary, EmptyR, int>>);
static_assert(::crucible::safety::proto::federation::role_protocol_matches_v<
    ::crucible::safety::proto::federation::SenderRole,
    ComputationCacheFederationSenderProto<&f12_p_unary, EmptyR, int>,
    ComputationCacheFederationKeyTag<&f12_p_unary, EmptyR, int>>);
static_assert(::crucible::safety::proto::is_content_addressed_v<
    typename ComputationCacheFederationContentAddressedPayload<
        &f12_p_unary, EmptyR, int>::payload_type>);
static_assert(sizeof(ComputationCacheFederationContentAddressedPayload<
                  &f12_p_unary, EmptyR, int>)
              == sizeof(std::span<const std::uint8_t>));

// ── Same (FnPtr, Row, Args...) → same key (deterministic) ─────────

static_assert(
    federation_key<&f12_p_unary, EmptyR, int>()
    == federation_key<&f12_p_unary, EmptyR, int>(),
    "F12: federation key MUST be deterministic for same inputs.");

// ── Different Row → different key (row-axis distinguishes) ────────

static_assert(
    federation_key<&f12_p_unary, EmptyR, int>()
    != federation_key<&f12_p_unary, BgR, int>(),
    "F12: federation key MUST differ across rows (the load-bearing "
    "row-axis-distinguishability invariant).");

// Different rows → different ROW axis specifically.
static_assert(
    federation_row_hash<EmptyR>() != federation_row_hash<BgR>(),
    "F12: row hash distinguishes EmptyRow from Row<Bg>.");
static_assert(
    federation_row_hash<BgR>() != federation_row_hash<IOR>(),
    "F12: row hash distinguishes Row<Bg> from Row<IO>.");
static_assert(
    federation_row_hash<BgR>() != federation_row_hash<BgIOR>(),
    "F12: row hash distinguishes Row<Bg> from Row<Bg, IO>.");

// ── Different FnPtr → different key (function-axis distinguishes) ─

static_assert(
    federation_key<&f12_p_unary,  EmptyR, int>()
    != federation_key<&f12_p_void, EmptyR>(),
    "F12: federation key distinguishes different functions.");

// ── Different Args → different key (args-axis distinguishes) ──────

static_assert(
    federation_key<&f12_p_unary, EmptyR, int>()
    != federation_key<&f12_p_binary, EmptyR, int, double>(),
    "F12: federation key distinguishes different argument packs.");

// ── Permutation invariance in Row's effect pack ───────────────────
//
// row_hash_contribution_v is sort-fold over Effect underlying values.
// Re-ordering atoms in Row<Es...> produces the SAME hash.  F12's
// federation_row_hash inherits this; the composite federation_key
// inherits it too because computation_cache_key_in_row's row
// contribution is also sort-fold (F11 + I02 alignment).

using BgIO_perm1 = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;
using BgIO_perm2 = eff_local::Row<eff_local::Effect::IO, eff_local::Effect::Bg>;
static_assert(
    federation_row_hash<BgIO_perm1>() == federation_row_hash<BgIO_perm2>(),
    "F12: row hash is permutation-invariant in the effect pack.");
static_assert(
    federation_key<&f12_p_unary, BgIO_perm1, int>()
    == federation_key<&f12_p_unary, BgIO_perm2, int>(),
    "F12: composite federation key inherits row-permutation "
    "invariance from F11 + I02.");

// ── Concept-fence witnesses ───────────────────────────────────────
//
// The federation primitives reject types that don't satisfy
// IsCacheableFunction (FnPtr) or IsEffectRow (Row).  These witnesses
// are positive — the header doesn't ship neg-compile fixtures
// because the F09/F11 fences already cover the rejection cases for
// computation_cache_key_in_row, and federation_key just forwards.

static_assert(IsCacheableFunction<&f12_p_unary>);
static_assert(IsEffectRow<EmptyR>);
static_assert(IsEffectRow<BgR>);
static_assert(IsEffectRow<BgIOR>);

}  // namespace detail::computation_cache_federation_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Drives the codec at runtime to pin (a) the I08 round-trip works
// end-to-end with F12 keys, (b) the projection produces non-sentinel
// keys that the codec accepts, (c) different rows write distinct
// bytes on the wire.

inline bool computation_cache_federation_smoke_test() noexcept {
    using namespace detail::computation_cache_federation_self_test;

    bool ok = true;

    // Encode an entry with EmptyRow and verify round-trip.
    {
        std::array<std::uint8_t, 64> buf{};
        const std::array<std::uint8_t, 4> body = {0x01, 0x02, 0x03, 0x04};

        auto written = serialize_computation_cache_federation_entry<
            &f12_p_unary, EmptyR, int>(buf, body);
        ok = ok && written.has_value();
        if (!written.has_value()) return false;

        auto view = deserialize_federation_entry(
            std::span<const std::uint8_t>(buf.data(), *written),
            static_cast<std::uint16_t>(
                ::crucible::effects::OsUniverse::cardinality));
        ok = ok && view.has_value();
        if (!view.has_value()) return false;

        const auto expected_key = federation_key<&f12_p_unary, EmptyR, int>();
        ok = ok && (view->header.content_hash == expected_key.content_hash);
        ok = ok && (view->header.row_hash     == expected_key.row_hash);
        ok = ok && (view->payload.size() == body.size());
        for (std::size_t i = 0; i < body.size(); ++i) {
            ok = ok && (view->payload[i] == body[i]);
        }
    }

    // Different rows produce distinct on-wire bytes.
    {
        std::array<std::uint8_t, 32> buf_empty{};
        std::array<std::uint8_t, 32> buf_bg{};
        auto wa = serialize_computation_cache_federation_entry<
            &f12_p_unary, EmptyR, int>(buf_empty,
                                        std::span<const std::uint8_t>{});
        auto wb = serialize_computation_cache_federation_entry<
            &f12_p_unary, BgR, int>(buf_bg,
                                     std::span<const std::uint8_t>{});
        ok = ok && wa.has_value() && wb.has_value();
        if (!wa.has_value() || !wb.has_value()) return false;
        ok = ok && (*wa == *wb);  // same total bytes (header-only)
        // Byte content differs in the row_hash slot (offset 16..23).
        bool any_diff = false;
        for (std::size_t i = 0; i < *wa; ++i) {
            if (buf_empty[i] != buf_bg[i]) { any_diff = true; break; }
        }
        ok = ok && any_diff;
    }

    // Content-addressed payload can announce hash-only: the serialized
    // entry is exactly the 32-byte header and carries zero payload bytes.
    {
        std::array<std::uint8_t, 32> buf{};
        using Payload = ComputationCacheFederationContentAddressedPayload<
            &f12_p_unary, EmptyR, int>;
        auto written = serialize_computation_cache_federation_entry<
            &f12_p_unary, EmptyR, int>(buf, Payload::hash_only());
        ok = ok && written.has_value();
        if (!written.has_value()) return false;
        ok = ok && (*written == FEDERATION_HEADER_BYTES);
    }

    return ok;
}

}  // namespace crucible::cipher::federation
