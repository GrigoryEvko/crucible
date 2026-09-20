#pragma once

// The wire key carries two axes, and the row hash appears in both.
// That is deliberate. The content axis folds the row in, which is what
// makes an in-process slot row-aware. The row axis carries the same
// hash on its own, so a receiver can route an entry, or refuse it by
// policy, without decoding the content axis.
//
// The row axis folds enumerator values alone and comes out the same
// on any toolchain. The content axis folds rendered names, which are
// implementation-specific, so two peers built with different
// compilers can compute different content hashes for one computation,
// or land two computations on one slot. The bare key is a safe join
// only among peers built on one toolchain. Peers that may differ have
// to fold a toolchain discriminator into the key, which makes the two
// sides disjoint by construction.
//
// The payload stays opaque to this layer. A compiled body is
// serialized by the dispatcher before the call and rebuilt by the
// receiving dispatcher afterwards, and the protocol never learns what
// is inside. The header pins only the identity the two sides must
// agree on.

#include <crucible/Types.h>
#include <crucible/cipher/ComputationCache.h>
#include <crucible/cipher/FederationProtocol.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/safety/diag/CanonicalOrder.h>
#include <crucible/safety/diag/_RowHashFold.h>
#include <crucible/sessions/FederationProtocol.h>

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
    ::crucible::safety::proto::federation::SenderProto<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationReceiverProto =
    ::crucible::safety::proto::federation::ReceiverProto<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationCoordProto =
    ::crucible::safety::proto::federation::CoordProto<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <typename Payload>
class [[nodiscard]] ContentAddressedFederationPayload {
public:
    using value_type = Payload;
    using payload_type = ::crucible::safety::proto::ContentAddressed<Payload>;

    constexpr ContentAddressedFederationPayload() noexcept = default;
    constexpr explicit ContentAddressedFederationPayload(std::span<const std::uint8_t> bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] static constexpr ContentAddressedFederationPayload hash_only() noexcept {
        return ContentAddressedFederationPayload{};
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

    [[nodiscard]] constexpr bool elides_wire_bytes() const noexcept { return bytes_.empty(); }

private:
    std::span<const std::uint8_t> bytes_{};
};

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationPayload = ::crucible::safety::proto::federation::FederationEntryPayload<
    ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationContentAddressedPayload =
    ContentAddressedFederationPayload<ComputationCacheFederationPayload<FnPtr, Row, Args...>>;

// The key is the cache-slot identity shared between parties. Two
// parties that compute the same thing must project it to the same
// key, or each publishes into a slot only it can find and the cache
// stops being shared.
//
// The row axis is canonical already. It is fenced to an effect row,
// and the fold over a row sorts and deduplicates, so two spellings of
// one row meet in a single slot.
//
// The argument axis is the open door. A stack of safety wrappers is
// not canonicalized for free. The hash combiner is order-sensitive,
// so two stacks that nest the same wrappers in opposite order fold to
// different values, and the name-based fold that the content axis
// uses for arguments splits them by spelling as well. Either way an
// out-of-order stack lands in a different slot from a peer's
// identical one. Every key projection below therefore requires each
// argument type to be in canonical nesting order. A bare payload type
// and a flat row are vacuously canonical, so only a genuinely
// inverted stack is refused, and it is refused here, naming the
// argument, rather than fragmenting the cache later.

template <typename... Args>
concept ArgsCanonicallyOrdered = (::crucible::safety::diag::canonical_order::CanonicallyOrdered<Args> && ...);

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline constexpr ContentHash federation_content_hash() noexcept {
    return ContentHash{computation_cache_key_in_row<FnPtr, Row, Args...>};
}

template <typename Row>
    requires IsEffectRow<Row>
[[nodiscard]] inline constexpr RowHash federation_row_hash() noexcept {
    return RowHash{::crucible::safety::diag::row_hash_contribution_v<Row>};
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline constexpr KernelCacheKey federation_key() noexcept {
    return KernelCacheKey{
        federation_content_hash<FnPtr, Row, Args...>(),
        federation_row_hash<Row>(),
    };
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline std::expected<std::size_t, FederationError> serialize_computation_cache_federation_entry(
    const ::crucible::permissions::LocalCipherPermission& local_permission, std::span<std::uint8_t> out_buf,
    ComputationCacheFederationContentAddressedPayload<FnPtr, Row, Args...> dispatcher_payload) noexcept {
    (void)local_permission;

    return serialize_federation_entry(out_buf, federation_key<FnPtr, Row, Args...>(), dispatcher_payload.bytes());
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline std::expected<std::size_t, FederationError>
serialize_computation_cache_federation_entry(const ::crucible::permissions::LocalCipherPermission& local_permission,
                                             std::span<std::uint8_t> out_buf,
                                             std::span<const std::uint8_t> dispatcher_payload) noexcept {
    return serialize_computation_cache_federation_entry<FnPtr, Row, Args...>(
        local_permission, out_buf,
        ComputationCacheFederationContentAddressedPayload<FnPtr, Row, Args...>{dispatcher_payload});
}

// The identity is pinned at the write site. A receiver never sees the
// function or the argument types. It reads the two hashes out of the
// header and runs its own lookup against them.
using ::crucible::cipher::federation::deserialize_federation_entry;
using ::crucible::cipher::federation::deserialize_untrusted_federation_entry;
using ::crucible::cipher::federation::deserialize_federation_header;

using ::crucible::cipher::federation::FEDERATION_HEADER_BYTES;

namespace detail::computation_cache_federation_self_test {

inline void f12_p_unary(int) noexcept {}
inline void f12_p_binary(int, double) noexcept {}
inline void f12_p_void() noexcept {}

namespace eff_local = ::crucible::effects;
using EmptyR = eff_local::Row<>;
using BgR = eff_local::Row<eff_local::Effect::Bg>;
using IOR = eff_local::Row<eff_local::Effect::IO>;
using BgIOR = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;

static_assert(federation_content_hash<&f12_p_unary, EmptyR, int>().raw() != 0,
              "the federation content hash must be non-zero, which the "
              "non-zero name seed guarantees.");
static_assert(federation_row_hash<EmptyR>().raw() != 0, "the federation row hash for the empty row must be non-zero, "
                                                        "which the cardinality-seeded fold guarantees.");
static_assert(!federation_key<&f12_p_unary, EmptyR, int>().is_zero(),
              "the composite federation key must not be the zero-key sentinel.");
static_assert(!federation_key<&f12_p_unary, EmptyR, int>().is_sentinel(),
              "the composite federation key must not be the all-ones pair "
              "that marks an empty cache slot.");
static_assert(
    ::crucible::safety::proto::is_well_formed_v<ComputationCacheFederationSenderProto<&f12_p_unary, EmptyR, int>>);
static_assert(
    ::crucible::safety::proto::is_well_formed_v<ComputationCacheFederationReceiverProto<&f12_p_unary, EmptyR, int>>);
static_assert(
    ::crucible::safety::proto::is_well_formed_v<ComputationCacheFederationCoordProto<&f12_p_unary, EmptyR, int>>);
static_assert(::crucible::safety::proto::federation::role_protocol_matches_v<
              ::crucible::safety::proto::federation::SenderRole,
              ComputationCacheFederationSenderProto<&f12_p_unary, EmptyR, int>,
              ComputationCacheFederationKeyTag<&f12_p_unary, EmptyR, int>>);
static_assert(::crucible::safety::proto::is_content_addressed_v<
              typename ComputationCacheFederationContentAddressedPayload<&f12_p_unary, EmptyR, int>::payload_type>);
static_assert(sizeof(ComputationCacheFederationContentAddressedPayload<&f12_p_unary, EmptyR, int>)
              == sizeof(std::span<const std::uint8_t>));

static_assert(federation_key<&f12_p_unary, EmptyR, int>() == federation_key<&f12_p_unary, EmptyR, int>(),
              "the federation key must be deterministic for the same inputs.");

static_assert(federation_key<&f12_p_unary, EmptyR, int>() != federation_key<&f12_p_unary, BgR, int>(),
              "the federation key must differ across rows, which is what "
              "makes the row axis able to tell entries apart.");

static_assert(federation_row_hash<EmptyR>() != federation_row_hash<BgR>(),
              "the row hash distinguishes the empty row from Row<Bg>.");
static_assert(federation_row_hash<BgR>() != federation_row_hash<IOR>(),
              "the row hash distinguishes Row<Bg> from Row<IO>.");
static_assert(federation_row_hash<BgR>() != federation_row_hash<BgIOR>(),
              "the row hash distinguishes Row<Bg> from Row<Bg, IO>.");

static_assert(federation_key<&f12_p_unary, EmptyR, int>() != federation_key<&f12_p_void, EmptyR>(),
              "the federation key distinguishes different functions.");

static_assert(federation_key<&f12_p_unary, EmptyR, int>() != federation_key<&f12_p_binary, EmptyR, int, double>(),
              "the federation key distinguishes different argument packs.");

using BgIO_perm1 = eff_local::Row<eff_local::Effect::Bg, eff_local::Effect::IO>;
using BgIO_perm2 = eff_local::Row<eff_local::Effect::IO, eff_local::Effect::Bg>;
static_assert(federation_row_hash<BgIO_perm1>() == federation_row_hash<BgIO_perm2>(),
              "the row hash does not change when the effect pack is reordered.");
static_assert(federation_key<&f12_p_unary, BgIO_perm1, int>() == federation_key<&f12_p_unary, BgIO_perm2, int>(),
              "the composite federation key does not change when the effect "
              "pack is reordered.");

static_assert(IsCacheableFunction<&f12_p_unary>);
static_assert(IsEffectRow<EmptyR>);
static_assert(IsEffectRow<BgR>);
static_assert(IsEffectRow<BgIOR>);

static_assert(ArgsCanonicallyOrdered<>, "an empty argument pack is vacuously canonical.");
static_assert(ArgsCanonicallyOrdered<int>, "a bare payload type is vacuously canonical.");
static_assert(ArgsCanonicallyOrdered<int, double>, "a pack of bare payload types is vacuously canonical.");

static_assert(ArgsCanonicallyOrdered<
                  ::crucible::safety::Stale<::crucible::safety::Tagged<int, ::crucible::safety::source::FromUser>>>,
              "Stale outside Tagged is the canonical nesting order and must be "
              "accepted as an argument.");

static_assert(!ArgsCanonicallyOrdered<
                  ::crucible::safety::Tagged<::crucible::safety::Stale<int>, ::crucible::safety::source::FromUser>>,
              "Tagged outside Stale inverts the canonical nesting order and "
              "must be refused at the publish boundary.");

static_assert(
    !federation_key<&f12_p_unary, EmptyR,
                    ::crucible::safety::Stale<::crucible::safety::Tagged<int, ::crucible::safety::source::FromUser>>>()
         .is_zero(),
    "a canonically nested argument projects to a well-formed federation "
    "key.");

}  // namespace detail::computation_cache_federation_self_test

inline bool computation_cache_federation_smoke_test() noexcept {
    using namespace detail::computation_cache_federation_self_test;

    bool ok = true;
    auto local_permission = ::crucible::safety::mint_permission_root<::crucible::permissions::tag::LocalCipherTag>();

    {
        std::array<std::uint8_t, 64> buf{};
        const std::array<std::uint8_t, 4> body = {0x01, 0x02, 0x03, 0x04};

        auto written =
            serialize_computation_cache_federation_entry<&f12_p_unary, EmptyR, int>(local_permission, buf, body);
        ok = ok && written.has_value();
        if (!written.has_value()) return false;

        auto view = deserialize_untrusted_federation_entry(
            std::span<const std::uint8_t>(buf.data(), *written),
            static_cast<std::uint16_t>(::crucible::effects::OsUniverse::cardinality));
        ok = ok && view.has_value();
        if (!view.has_value()) return false;

        const auto expected_key = federation_key<&f12_p_unary, EmptyR, int>();
        ok = ok && (view->header.content_hash == expected_key.content_hash);
        ok = ok && (view->header.row_hash == expected_key.row_hash);
        ok = ok && (view->payload.size() == body.size());
        for (std::size_t i = 0; i < body.size(); ++i) {
            ok = ok && (view->payload[i] == body[i]);
        }
    }

    {
        std::array<std::uint8_t, 32> buf_empty{};
        std::array<std::uint8_t, 32> buf_bg{};
        auto wa = serialize_computation_cache_federation_entry<&f12_p_unary, EmptyR, int>(
            local_permission, buf_empty, std::span<const std::uint8_t>{});
        auto wb = serialize_computation_cache_federation_entry<&f12_p_unary, BgR, int>(local_permission, buf_bg,
                                                                                       std::span<const std::uint8_t>{});
        ok = ok && wa.has_value() && wb.has_value();
        if (!wa.has_value() || !wb.has_value()) return false;
        ok = ok && (*wa == *wb);
        bool any_diff = false;
        for (std::size_t i = 0; i < *wa; ++i) {
            if (buf_empty[i] != buf_bg[i]) {
                any_diff = true;
                break;
            }
        }
        ok = ok && any_diff;
    }

    {
        std::array<std::uint8_t, 32> buf{};
        using Payload = ComputationCacheFederationContentAddressedPayload<&f12_p_unary, EmptyR, int>;
        auto written = serialize_computation_cache_federation_entry<&f12_p_unary, EmptyR, int>(local_permission, buf,
                                                                                               Payload::hash_only());
        ok = ok && written.has_value();
        if (!written.has_value()) return false;
        ok = ok && (*written == FEDERATION_HEADER_BYTES);
    }

    return ok;
}

}  // namespace crucible::cipher::federation
