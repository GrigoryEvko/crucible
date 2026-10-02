#include <crucible/cipher/CipherTierPromotion.h>

#include <fixy/Ctx.h>
#include <fixy/session/Delegate.h>
#include <foundation/Pinned.h>
#include <foundation/reflect/EnumName.h>

#include "test_assert.h"
#include <expected>
#include <cstdio>
#include <optional>
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
using ::fixy::CipherTier;
using ::fixy::CipherTierTag_v;
namespace tier = ::fixy::cipher_tier;
namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept CanMintPromote = requires(CipherTier<From, T> source) {
    { mint_promote<From, To>(std::move(source)) } -> std::same_as<CipherTier<To, T>>;
};

template <CipherTierTag_v From, CipherTierTag_v To, typename T>
concept CanMintDemote = requires(CipherTier<From, T> source) {
    { mint_demote<From, To>(std::move(source)) } -> std::same_as<CipherTier<To, T>>;
};

struct MoveOnlyHash {
    ContentHash hash;

    explicit constexpr MoveOnlyHash(ContentHash h) noexcept : hash{h} {}
    MoveOnlyHash(const MoveOnlyHash&) = delete;
    MoveOnlyHash& operator=(const MoveOnlyHash&) = delete;
    constexpr MoveOnlyHash(MoveOnlyHash&&) noexcept = default;
    constexpr MoveOnlyHash& operator=(MoveOnlyHash&&) noexcept = default;
};

static_assert(CanMintPromote<CipherTierTag_v::Cold, CipherTierTag_v::Warm, ContentHash>);
static_assert(CanMintPromote<CipherTierTag_v::Cold, CipherTierTag_v::Hot, MoveOnlyHash>);
static_assert(CanMintPromote<CipherTierTag_v::Warm, CipherTierTag_v::Hot, ContentHash>);
static_assert(!CanMintPromote<CipherTierTag_v::Hot, CipherTierTag_v::Cold, ContentHash>);

static_assert(CanMintDemote<CipherTierTag_v::Hot, CipherTierTag_v::Cold, MoveOnlyHash>);
static_assert(CanMintDemote<CipherTierTag_v::Hot, CipherTierTag_v::Warm, ContentHash>);
static_assert(CanMintDemote<CipherTierTag_v::Warm, CipherTierTag_v::Cold, ContentHash>);
static_assert(!CanMintDemote<CipherTierTag_v::Cold, CipherTierTag_v::Hot, ContentHash>);

static_assert(std::is_same_v<HotPromote<ContentHash>, s::Send<tier::Hot<ContentHash>, s::End>>);
static_assert(s::is_well_formed_v<HotPromote<ContentHash>>);
static_assert(s::DelegatesTo<HotPromoteDelegate<ContentHash>, HotPromote<ContentHash>>);
static_assert(s::AcceptsFrom<HotPromoteAccept<ContentHash>, HotPromote<ContentHash>>);
static_assert(std::is_same_v<s::dual_of_t<HotPromoteDelegate<ContentHash>>, HotPromoteAccept<ContentHash>>);

// A diagnostic prints a refusal by its enumerator name.
static_assert(::foundation::reflect::enum_name(RestoreError::EmptyContentHash) == "EmptyContentHash");
static_assert(::foundation::reflect::enum_name(RestoreError::BackendUnavailable) == "BackendUnavailable");

void test_promote_and_demote_preserve_value() {
    constexpr ContentHash cold_hash{0xA001000000000001ULL};
    auto cold = ::fixy::mint_band<tier::Cold<MoveOnlyHash>>(MoveOnlyHash{cold_hash});
    auto hot = mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Hot>(std::move(cold));
    static_assert(std::is_same_v<decltype(hot), tier::Hot<MoveOnlyHash>>);
    MoveOnlyHash moved_hot = std::move(hot).consume();
    assert(moved_hot.hash == cold_hash);

    constexpr ContentHash hot_hash{0xB002000000000002ULL};
    auto hot_source = ::fixy::mint_band<tier::Hot<MoveOnlyHash>>(MoveOnlyHash{hot_hash});
    auto cold_again = mint_demote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(std::move(hot_source));
    static_assert(std::is_same_v<decltype(cold_again), tier::Cold<MoveOnlyHash>>);
    MoveOnlyHash moved_cold = std::move(cold_again).consume();
    assert(moved_cold.hash == hot_hash);
}

void test_restore_returns_expected_warm_handle() {
    constexpr ContentHash hash{0xC003000000000003ULL};
    auto restored = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(hash), hash);
    assert(restored.has_value());
    static_assert(std::is_same_v<decltype(restored), std::expected<tier::Warm<ContentHash>, RestoreError>>);
    ContentHash restored_hash = std::move(restored.value()).consume();
    assert(restored_hash == hash);
}

void test_restore_error_surface() {
    constexpr ContentHash hash{0xD004000000000004ULL};
    auto empty_key = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(hash), ContentHash{});
    assert(!empty_key.has_value());
    assert(empty_key.error() == RestoreError::EmptyContentHash);

    auto empty_handle = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(ContentHash{}), hash);
    assert(!empty_handle.has_value());
    assert(empty_handle.error() == RestoreError::EmptyColdHandle);

    auto mismatch = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(ContentHash{0xE005ULL}), hash);
    assert(!mismatch.has_value());
    assert(mismatch.error() == RestoreError::ContentHashMismatch);
}

// The endpoint of the hot promotion: one hot-tier value and End, over a
// wire that records the marker it came with and the value it sent.
struct HotWire {
    ContentHash marker{};
    ContentHash sent{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using CarriedHotEndpoint =
    s::DelegatedSession<HotPromote<ContentHash>, HotWire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

// The carrier channel moves one delegated endpoint, in a slot that the
// test fills and empties on one thread.  A hand-off that runs is a Send of
// the DelegatedSession.  The Delegate head only states it.
struct CarrierSlot : ::foundation::Pinned<CarrierSlot> {
    std::optional<CarriedHotEndpoint> held;
    int delegated_endpoints = 0;
};
struct CarrierSender {
    CarrierSlot* slot = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct CarrierRecipient {
    CarrierSlot* slot = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Carrier = s::Send<CarriedHotEndpoint, s::End>;

void test_hot_promote_delegate_protocol() {
    constexpr ContentHash marker{0xABCD000000000001ULL};
    constexpr ContentHash payload{0xABCD000000000002ULL};

    CarrierSlot slot{};
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto [delegator, acceptor] = s::mint_test_channel<Carrier>(ctx, CarrierSender{&slot}, CarrierRecipient{&slot});

    auto delegated_endpoint = s::mint_session_handle<HotPromote<ContentHash>, HotWire>(HotWire{.marker = marker});
    CarriedHotEndpoint carried = s::mint_delegated_session(std::move(delegated_endpoint));

    auto delegator_end =
        std::move(delegator).send(std::move(carried), [](CarrierSender& end, CarriedHotEndpoint& value) noexcept {
            if (end.slot->held.has_value()) return false;
            end.slot->held.emplace(std::move(value));
            ++end.slot->delegated_endpoints;
            return true;
        });
    auto [received, acceptor_end] =
        std::move(acceptor).recv([](CarrierRecipient& end) noexcept -> std::optional<CarriedHotEndpoint> {
            if (!end.slot->held.has_value()) return std::nullopt;
            std::optional<CarriedHotEndpoint> taken{std::move(*end.slot->held)};
            end.slot->held.reset();
            return taken;
        });
    static_cast<void>(std::move(delegator_end).close());
    static_cast<void>(std::move(acceptor_end).close());
    assert(slot.delegated_endpoints == 1);

    auto hot_endpoint = std::move(received).accept();
    auto hot_end = std::move(hot_endpoint)
                       .send(::fixy::mint_band<tier::Hot<ContentHash>>(payload),
                             [](HotWire& wire, tier::Hot<ContentHash>& value) noexcept {
                                 wire.sent = value.peek();
                                 return true;
                             });
    const HotWire hot_wire = std::move(hot_end).close();
    assert(hot_wire.marker == marker);
    assert(hot_wire.sent == payload);
}

}  // namespace

int main() {
    test_promote_and_demote_preserve_value();
    test_restore_returns_expected_warm_handle();
    test_restore_error_surface();
    test_hot_promote_delegate_protocol();
    crucible::test::pass("cipher_tier_promotion: ok\n");
    return 0;
}
