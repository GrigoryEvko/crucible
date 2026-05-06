// GAPS-041 — CipherTier promotion/demotion mint sites and HotPromote
// delegation protocol.

#include <crucible/cipher/CipherTierPromotion.h>

#include <cassert>
#include <expected>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using crucible::ContentHash;
using crucible::cipher::HotPromote;
using crucible::cipher::HotPromoteAccept;
using crucible::cipher::HotPromoteDelegate;
using crucible::cipher::RestoreError;
using crucible::cipher::mint_demote;
using crucible::cipher::mint_promote;
using crucible::cipher::mint_restore;
using crucible::safety::CipherTier;
using crucible::safety::CipherTierTag_v;
namespace ct = crucible::cipher;
namespace tier = crucible::safety::cipher_tier;
namespace proto = crucible::safety::proto;

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept CanMintPromote = requires(CipherTier<From, T> source) {
    { mint_promote<From, To>(std::move(source)) } ->
        std::same_as<CipherTier<To, T>>;
};

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept CanMintDemote = requires(CipherTier<From, T> source) {
    { mint_demote<From, To>(std::move(source)) } ->
        std::same_as<CipherTier<To, T>>;
};

struct MoveOnlyHash {
    ContentHash hash;

    explicit constexpr MoveOnlyHash(ContentHash h) noexcept : hash{h} {}
    MoveOnlyHash(const MoveOnlyHash&) = delete;
    MoveOnlyHash& operator=(const MoveOnlyHash&) = delete;
    constexpr MoveOnlyHash(MoveOnlyHash&&) noexcept = default;
    constexpr MoveOnlyHash& operator=(MoveOnlyHash&&) noexcept = default;
};

static_assert(CanMintPromote<CipherTierTag_v::Cold,
                             CipherTierTag_v::Warm,
                             ContentHash>);
static_assert(CanMintPromote<CipherTierTag_v::Cold,
                             CipherTierTag_v::Hot,
                             MoveOnlyHash>);
static_assert(CanMintPromote<CipherTierTag_v::Warm,
                             CipherTierTag_v::Hot,
                             ContentHash>);
static_assert(!CanMintPromote<CipherTierTag_v::Hot,
                              CipherTierTag_v::Cold,
                              ContentHash>);

static_assert(CanMintDemote<CipherTierTag_v::Hot,
                            CipherTierTag_v::Cold,
                            MoveOnlyHash>);
static_assert(CanMintDemote<CipherTierTag_v::Hot,
                            CipherTierTag_v::Warm,
                            ContentHash>);
static_assert(CanMintDemote<CipherTierTag_v::Warm,
                            CipherTierTag_v::Cold,
                            ContentHash>);
static_assert(!CanMintDemote<CipherTierTag_v::Cold,
                             CipherTierTag_v::Hot,
                             ContentHash>);

static_assert(std::is_same_v<
    HotPromote<ContentHash>,
    proto::Send<tier::Hot<ContentHash>, proto::End>>);
static_assert(proto::is_well_formed_v<HotPromote<ContentHash>>);
static_assert(proto::DelegatesTo<
    HotPromoteDelegate<ContentHash>,
    HotPromote<ContentHash>>);
static_assert(proto::AcceptsFrom<
    HotPromoteAccept<ContentHash>,
    HotPromote<ContentHash>>);
static_assert(std::is_same_v<
    proto::dual_of_t<HotPromoteDelegate<ContentHash>>,
    HotPromoteAccept<ContentHash>>);

void test_promote_and_demote_preserve_value() {
    constexpr ContentHash cold_hash{0xA001'0000'0000'0001ULL};
    tier::Cold<MoveOnlyHash> cold{MoveOnlyHash{cold_hash}};
    auto hot = mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Hot>(
        std::move(cold));
    static_assert(std::is_same_v<decltype(hot), tier::Hot<MoveOnlyHash>>);
    MoveOnlyHash moved_hot = std::move(hot).consume();
    assert(moved_hot.hash == cold_hash);

    constexpr ContentHash hot_hash{0xB002'0000'0000'0002ULL};
    tier::Hot<MoveOnlyHash> hot_source{MoveOnlyHash{hot_hash}};
    auto cold_again = mint_demote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(
        std::move(hot_source));
    static_assert(std::is_same_v<decltype(cold_again),
                                 tier::Cold<MoveOnlyHash>>);
    MoveOnlyHash moved_cold = std::move(cold_again).consume();
    assert(moved_cold.hash == hot_hash);
}

void test_restore_returns_expected_warm_handle() {
    constexpr ContentHash hash{0xC003'0000'0000'0003ULL};
    auto restored = mint_restore<ContentHash>(tier::Cold<ContentHash>{hash},
                                              hash);
    assert(restored.has_value());
    static_assert(std::is_same_v<decltype(restored),
        std::expected<tier::Warm<ContentHash>, RestoreError>>);
    ContentHash restored_hash = std::move(restored.value()).consume();
    assert(restored_hash == hash);
}

void test_restore_error_surface() {
    constexpr ContentHash hash{0xD004'0000'0000'0004ULL};
    auto empty_key = mint_restore<ContentHash>(
        tier::Cold<ContentHash>{hash}, ContentHash{});
    assert(!empty_key.has_value());
    assert(empty_key.error() == RestoreError::EmptyContentHash);

    auto empty_handle = mint_restore<ContentHash>(
        tier::Cold<ContentHash>{ContentHash{}}, hash);
    assert(!empty_handle.has_value());
    assert(empty_handle.error() == RestoreError::EmptyColdHandle);

    auto mismatch = mint_restore<ContentHash>(
        tier::Cold<ContentHash>{ContentHash{0xE005ULL}}, hash);
    assert(!mismatch.has_value());
    assert(mismatch.error() == RestoreError::ContentHashMismatch);
}

struct CarrierWire {
    int delegated_endpoints = 0;
    ContentHash transferred_marker{};
};

struct HotWire {
    ContentHash marker{};
    ContentHash sent{};
};

void transport_delegate_hot(CarrierWire& carrier, HotWire&& endpoint) noexcept {
    ++carrier.delegated_endpoints;
    carrier.transferred_marker = endpoint.marker;
}

HotWire transport_accept_hot(CarrierWire&) noexcept {
    return HotWire{.marker = ContentHash{0xABCD'0000'0000'0001ULL}};
}

void send_hot_payload(HotWire& wire, tier::Hot<ContentHash>&& payload) noexcept {
    wire.sent = std::move(payload).consume();
}

void test_hot_promote_delegate_protocol() {
    constexpr ContentHash marker{0xABCD'0000'0000'0001ULL};
    constexpr ContentHash payload{0xABCD'0000'0000'0002ULL};

    auto delegator =
        proto::mint_session_handle<HotPromoteDelegate<ContentHash>>(
            CarrierWire{});
    auto delegated_endpoint =
        proto::mint_session_handle<HotPromote<ContentHash>>(
            HotWire{.marker = marker});

    auto delegator_end = std::move(delegator).delegate(
        std::move(delegated_endpoint), transport_delegate_hot);
    CarrierWire carrier_after_delegate = std::move(delegator_end).close();
    assert(carrier_after_delegate.delegated_endpoints == 1);
    assert(carrier_after_delegate.transferred_marker == marker);

    auto acceptor =
        proto::mint_session_handle<HotPromoteAccept<ContentHash>>(
            CarrierWire{});
    auto [hot_endpoint, acceptor_end] =
        std::move(acceptor).accept(transport_accept_hot);
    (void)std::move(acceptor_end).close();

    auto hot_end = std::move(hot_endpoint).send(
        tier::Hot<ContentHash>{payload}, send_hot_payload);
    HotWire hot_wire = std::move(hot_end).close();
    assert(hot_wire.marker == marker);
    assert(hot_wire.sent == payload);
}

}  // namespace

int main() {
    test_promote_and_demote_preserve_value();
    test_restore_returns_expected_warm_handle();
    test_restore_error_surface();
    test_hot_promote_delegate_protocol();
    std::puts("cipher_tier_promotion: ok");
    return 0;
}
