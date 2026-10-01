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
#include <fixy/CanonicalOrder.h>
#include <fixy/Federation.h>
#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Protocol.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
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
using ComputationCacheFederationSenderProto = SenderProto<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationReceiverProto = ReceiverProto<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row>
using ComputationCacheFederationCoordProto = CoordProto<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

template <typename Payload>
class [[nodiscard]] ContentAddressedFederationPayload {
public:
    using value_type = Payload;
    using payload_type = ::fixy::session::ContentAddressed<Payload>;

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
using ComputationCacheFederationPayload = FederationEntryPayload<ComputationCacheFederationKeyTag<FnPtr, Row, Args...>>;

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
concept ArgsCanonicallyOrdered = (::fixy::canonical_order::CanonicallyOrdered<Args> && ...);

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline constexpr ContentHash federation_content_hash() noexcept {
    return ContentHash{computation_cache_key_in_row<FnPtr, Row, Args...>};
}

template <typename Row>
    requires IsEffectRow<Row>
[[nodiscard]] inline constexpr RowHash federation_row_hash() noexcept {
    return RowHash{::foundation::diag::row_hash_contribution_v<Row>};
}

template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline constexpr KernelCacheKey federation_key() noexcept {
    return KernelCacheKey{
        federation_content_hash<FnPtr, Row, Args...>(),
        federation_row_hash<Row>(),
    };
}

// The local cipher permission is the proof that this process may write
// into the federation.  The call reads it and does not consume it, and
// its brand is deduced, so a caller passes the token it holds.
template <typename Brand>
using LocalCipherPermission = ::foundation::permissions::Permission<::fixy::federation::LocalCipherTag, Brand>;

template <auto FnPtr, typename Row, typename... Args, typename Brand>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline std::expected<std::size_t, FederationError> serialize_computation_cache_federation_entry(
    const LocalCipherPermission<Brand>& local_permission, std::span<std::uint8_t> out_buf,
    ComputationCacheFederationContentAddressedPayload<FnPtr, Row, Args...> dispatcher_payload) noexcept {
    (void)local_permission;

    return serialize_federation_entry(out_buf, federation_key<FnPtr, Row, Args...>(), dispatcher_payload.bytes());
}

template <auto FnPtr, typename Row, typename... Args, typename Brand>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline std::expected<std::size_t, FederationError>
serialize_computation_cache_federation_entry(const LocalCipherPermission<Brand>& local_permission,
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

namespace detail {

// The probe function and the two rows of
// computation_cache_federation_smoke_test, which the check file of this
// header also keys.
inline void probe_unary(int) noexcept {}
using EmptyR = ::foundation::effects::Row<>;
using BgR = ::foundation::effects::Row<::foundation::effects::Effect::Bg>;

}  // namespace detail

// The caller passes the local cipher permission it holds.  A root of
// that permission is minted once per program under a context that
// admits its IO row, and a library header does not mint one.
template <typename Brand>
[[nodiscard]] inline bool
computation_cache_federation_smoke_test(const LocalCipherPermission<Brand>& local_permission) noexcept {
    using detail::BgR;
    using detail::EmptyR;
    using detail::probe_unary;

    bool ok = true;

    {
        std::array<std::uint8_t, 64> buf{};
        const std::array<std::uint8_t, 4> body = {0x01, 0x02, 0x03, 0x04};

        auto written =
            serialize_computation_cache_federation_entry<&probe_unary, EmptyR, int>(local_permission, buf, body);
        ok = ok && written.has_value();
        if (!written.has_value()) return false;

        auto view =
            deserialize_untrusted_federation_entry(std::span<const std::uint8_t>(buf.data(), *written),
                                                   static_cast<std::uint16_t>(::foundation::effects::effect_count));
        ok = ok && view.has_value();
        if (!view.has_value()) return false;

        const auto expected_key = federation_key<&probe_unary, EmptyR, int>();
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
        auto wa = serialize_computation_cache_federation_entry<&probe_unary, EmptyR, int>(
            local_permission, buf_empty, std::span<const std::uint8_t>{});
        auto wb = serialize_computation_cache_federation_entry<&probe_unary, BgR, int>(local_permission, buf_bg,
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
        using Payload = ComputationCacheFederationContentAddressedPayload<&probe_unary, EmptyR, int>;
        auto written = serialize_computation_cache_federation_entry<&probe_unary, EmptyR, int>(local_permission, buf,
                                                                                               Payload::hash_only());
        ok = ok && written.has_value();
        if (!written.has_value()) return false;
        ok = ok && (*written == FEDERATION_HEADER_BYTES);
    }

    return ok;
}

}  // namespace crucible::cipher::federation
