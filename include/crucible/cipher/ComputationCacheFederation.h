#pragma once

// The wire key of a computation is the federation key of its stable
// identity (crucible/cipher/FederationProtocol.h): the keyed hash of the
// stable names of the function, of the canonical row and of each argument
// type (foundation/core/StableIdentity.h).  It hashes the names and not
// their 64-bit ids, so two computations whose ids collide still get two
// keys.  The row takes part in its canonical form, so two spellings of one
// row meet in one slot.
//
// The names are rendered by the compiler and are implementation-specific,
// so two peers built with different compilers can compute different keys
// for one computation.  A key is a safe join only among peers built on
// one toolchain.  A miss between two toolchains is safe, and two names
// never give one image, so two toolchains cannot land two computations
// on one slot.
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
#include <foundation/core/StableIdentity.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

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
    constexpr explicit ContentAddressedFederationPayload(::fixy::View<std::byte const> bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] static constexpr ContentAddressedFederationPayload hash_only() noexcept {
        return ContentAddressedFederationPayload{};
    }

    [[nodiscard]] constexpr ::fixy::View<std::byte const> bytes() const noexcept { return bytes_; }

    [[nodiscard]] constexpr bool elides_wire_bytes() const noexcept { return bytes_.size() == 0; }

private:
    ::fixy::View<std::byte const> bytes_{};
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
// The row is canonical already. It is fenced to an effect row, and the
// key reads its canonical form, which sorts and deduplicates, so two
// spellings of one row meet in a single slot.
//
// The argument list is the open door. A stack of safety wrappers is
// not canonicalized for free. Two stacks that nest the same wrappers in
// opposite order print two names, so an out-of-order stack lands in a
// different slot from a peer's identical one. Every key projection below
// therefore requires each argument type to be in canonical nesting order.
// A bare payload type and a flat row are vacuously canonical, so only a
// genuinely inverted stack is refused, and it is refused here, naming the
// argument, rather than fragmenting the cache later.

template <typename... Args>
concept ArgsCanonicallyOrdered = (::fixy::canonical_order::CanonicallyOrdered<Args> && ...);

// The mark of the content of a computation key.  Its name is the third
// field of the identity, so the identity of a computation is no image of
// a different kind of content under the same key of the federation.
struct ComputationCacheIdentity {};

// The stable identity of a computation: the image of the function, of the
// mark, of the canonical row and of each argument type, with static
// storage.
template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] constexpr ::fixy::View<std::byte const> computation_identity() noexcept {
    return ::fixy::View<std::byte const>{
        ::foundation::core::stable_identity_image<FnPtr, ComputationCacheIdentity,
                                                  ::foundation::effects::canonical_row_t<Row>, Args...>()};
}

// The federation key of a computation under the key of the federation.
// Complexity: one compression for each 64 bytes of the identity.
template <auto FnPtr, typename Row, typename... Args>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] constexpr FederationKey federation_key(FederationHashKey const& hash_key) noexcept {
    return federation_key_of_bytes(hash_key, computation_identity<FnPtr, Row, Args...>());
}

// The local cipher permission is the proof that this process may write
// into the federation.  The call reads it and does not consume it, and
// its brand is deduced, so a caller passes the token it holds.
template <typename Brand>
using LocalCipherPermission = ::foundation::permissions::Permission<::fixy::federation::LocalCipherTag, Brand>;

// The entry goes into the run of out_buf, the ViewMut of a frame.
template <auto FnPtr, typename Row, typename... Args, typename Brand, class OutBrand, std::size_t OutExtent>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline ::fixy::Result<std::size_t, FederationError> serialize_computation_cache_federation_entry(
    const LocalCipherPermission<Brand>& local_permission, FederationHashKey const& hash_key,
    ::fixy::ViewMut<std::byte, OutBrand, OutExtent> out_buf,
    ComputationCacheFederationContentAddressedPayload<FnPtr, Row, Args...> dispatcher_payload) noexcept {
    (void)local_permission;

    return serialize_federation_entry(std::move(out_buf), federation_key<FnPtr, Row, Args...>(hash_key),
                                      dispatcher_payload.bytes());
}

template <auto FnPtr, typename Row, typename... Args, typename Brand, class OutBrand, std::size_t OutExtent>
    requires IsCacheableFunction<FnPtr> && IsEffectRow<Row> && ArgsCanonicallyOrdered<Args...>
[[nodiscard]] inline ::fixy::Result<std::size_t, FederationError>
serialize_computation_cache_federation_entry(const LocalCipherPermission<Brand>& local_permission,
                                             FederationHashKey const& hash_key,
                                             ::fixy::ViewMut<std::byte, OutBrand, OutExtent> out_buf,
                                             ::fixy::View<std::byte const> dispatcher_payload) noexcept {
    return serialize_computation_cache_federation_entry<FnPtr, Row, Args...>(
        local_permission, hash_key, std::move(out_buf),
        ComputationCacheFederationContentAddressedPayload<FnPtr, Row, Args...>{dispatcher_payload});
}

// The identity is pinned at the write site. A receiver never sees the
// function or the argument types. It reads the key out of the header
// and runs its own lookup against it.
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

// The caller passes the local cipher permission it holds and the key of
// its federation.  A root of that permission is minted once per program
// under a context that admits its IO row, and a library header does not
// mint one.
template <typename Brand>
[[nodiscard]] inline bool computation_cache_federation_smoke_test(const LocalCipherPermission<Brand>& local_permission,
                                                                  FederationHashKey const& hash_key) noexcept {
    using detail::BgR;
    using detail::EmptyR;
    using detail::probe_unary;

    {
        const ::fixy::FixedArray<std::byte, 4> body{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};

        // The body borrows the permission through a Ref, so no closure holds
        // the linear permission by reference.
        auto [written, buf] = ::fixy::build_fixed_array<std::byte, FEDERATION_HEADER_BYTES + 4>(
            [permission = ::fixy::to(local_permission), &hash_key, &body](auto run) noexcept {
                return serialize_computation_cache_federation_entry<&probe_unary, EmptyR, int>(
                    *permission, hash_key, std::move(run), body.view());
            });
        // The entry is the part of the buffer that the write filled, and it
        // decodes to the key and the body that the write took.
        const bool is_round_trip = written.match(
            [&buf, &hash_key, &body](std::size_t written_bytes) noexcept {
                const ::fixy::View<std::byte const> entry =
                    ::fixy::View<std::byte const>{buf.view()}.prefix_up_to(written_bytes);
                const auto decoded = deserialize_untrusted_federation_entry(entry, FEDERATION_UNIVERSE_CARDINALITY);
                return decoded.match(
                    [&hash_key, &body](FederationEntryView const& view) noexcept {
                        return view.header.key == federation_key<&probe_unary, EmptyR, int>(hash_key)
                            && ::fixy::equal(view.payload, body.view());
                    },
                    [](FederationError) noexcept { return false; });
            },
            [](FederationError) noexcept { return false; });
        if (!is_round_trip) return false;
    }

    {
        auto [wa, buf_empty] = ::fixy::build_fixed_array<std::byte, FEDERATION_HEADER_BYTES>(
            [permission = ::fixy::to(local_permission), &hash_key](auto run) noexcept {
                return serialize_computation_cache_federation_entry<&probe_unary, EmptyR, int>(
                    *permission, hash_key, std::move(run), ::fixy::View<std::byte const>{});
            });
        auto [wb, buf_bg] = ::fixy::build_fixed_array<std::byte, FEDERATION_HEADER_BYTES>(
            [permission = ::fixy::to(local_permission), &hash_key](auto run) noexcept {
                return serialize_computation_cache_federation_entry<&probe_unary, BgR, int>(
                    *permission, hash_key, std::move(run), ::fixy::View<std::byte const>{});
            });
        // The two entries fill their buffers, so the two buffers differ
        // where the two rows differ.
        const bool is_same_size = wa.match(
            [&wb](std::size_t written_empty) noexcept {
                return wb.match(
                    [written_empty](std::size_t written_bg) noexcept { return written_empty == written_bg; },
                    [](FederationError) noexcept { return false; });
            },
            [](FederationError) noexcept { return false; });
        if (!is_same_size) return false;
        if (::fixy::equal(buf_empty.view(), buf_bg.view())) return false;
    }

    {
        using Payload = ComputationCacheFederationContentAddressedPayload<&probe_unary, EmptyR, int>;
        auto [written, buf] = ::fixy::build_fixed_array<std::byte, FEDERATION_HEADER_BYTES>(
            [permission = ::fixy::to(local_permission), &hash_key](auto run) noexcept {
                return serialize_computation_cache_federation_entry<&probe_unary, EmptyR, int>(
                    *permission, hash_key, std::move(run), Payload::hash_only());
            });
        const bool is_header_only =
            written.match([](std::size_t written_bytes) noexcept { return written_bytes == FEDERATION_HEADER_BYTES; },
                          [](FederationError) noexcept { return false; });
        if (!is_header_only) return false;
    }

    return true;
}

}  // namespace crucible::cipher::federation
