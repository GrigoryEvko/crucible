#include <crucible/cipher/CipherTierPromotion.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/Delegate.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Pinned.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
static_assert(::fixy::enum_name_text(RestoreError::EmptyContentHash) == "EmptyContentHash");
static_assert(::fixy::enum_name_text(RestoreError::BackendUnavailable) == "BackendUnavailable");

void test_promote_and_demote_preserve_value() {
    constexpr ContentHash cold_hash{0xA001000000000001ULL};
    auto cold = ::fixy::mint_band<tier::Cold<MoveOnlyHash>>(MoveOnlyHash{cold_hash});
    auto hot = mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Hot>(std::move(cold));
    static_assert(std::is_same_v<decltype(hot), tier::Hot<MoveOnlyHash>>);
    MoveOnlyHash moved_hot = std::move(hot).consume();
    CRUCIBLE_TEST_ASSERT(kTest, moved_hot.hash == cold_hash);

    constexpr ContentHash hot_hash{0xB002000000000002ULL};
    auto hot_source = ::fixy::mint_band<tier::Hot<MoveOnlyHash>>(MoveOnlyHash{hot_hash});
    auto cold_again = mint_demote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(std::move(hot_source));
    static_assert(std::is_same_v<decltype(cold_again), tier::Cold<MoveOnlyHash>>);
    MoveOnlyHash moved_cold = std::move(cold_again).consume();
    CRUCIBLE_TEST_ASSERT(kTest, moved_cold.hash == hot_hash);
}

void test_restore_returns_expected_warm_handle() {
    constexpr ContentHash hash{0xC003000000000003ULL};
    auto restored = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(hash), hash);
    CRUCIBLE_TEST_ASSERT(kTest, restored.is_ok());
    static_assert(std::is_same_v<decltype(restored), ::fixy::Result<tier::Warm<ContentHash>, RestoreError>>);
    ContentHash restored_hash = std::move(restored).expect("the restore of a matching hash succeeds").consume();
    CRUCIBLE_TEST_ASSERT(kTest, restored_hash == hash);
}

void test_restore_error_surface() {
    constexpr ContentHash hash{0xD004000000000004ULL};
    auto empty_key = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(hash), ContentHash{});
    CRUCIBLE_TEST_ASSERT(kTest, empty_key.is_err());
    CRUCIBLE_TEST_ASSERT(kTest,
                         empty_key.err().expect("the restore refuses an empty key") == RestoreError::EmptyContentHash);

    auto empty_handle = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(ContentHash{}), hash);
    CRUCIBLE_TEST_ASSERT(kTest, empty_handle.is_err());
    CRUCIBLE_TEST_ASSERT(kTest, empty_handle.err().expect("the restore refuses an empty handle")
                                    == RestoreError::EmptyColdHandle);

    auto mismatch = mint_restore<ContentHash>(::fixy::mint_band<tier::Cold<ContentHash>>(ContentHash{0xE005ULL}), hash);
    CRUCIBLE_TEST_ASSERT(kTest, mismatch.is_err());
    CRUCIBLE_TEST_ASSERT(kTest,
                         mismatch.err().expect("the restore refuses a mismatch") == RestoreError::ContentHashMismatch);
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

// The carrier moves one delegated endpoint in a box, on one thread.  The
// sender closes with the endpoint in the box, and the recipient opens on it.
// A hand-off that runs is a Send of the DelegatedSession.  The Delegate head
// only states it.
struct CarrierBox {
    ::fixy::FixedList<CarriedHotEndpoint, 1> held{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Carrier = s::Send<CarriedHotEndpoint, s::End>;

void test_hot_promote_delegate_protocol() {
    constexpr ContentHash marker{0xABCD000000000001ULL};
    constexpr ContentHash payload{0xABCD000000000002ULL};

    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto delegator = s::mint_session<Carrier>(ctx, CarrierBox{});
    auto delegated_endpoint = s::mint_session<HotPromote<ContentHash>>(ctx, HotWire{.marker = marker});
    CarriedHotEndpoint carried = s::mint_delegated_session(std::move(delegated_endpoint));

    // The endpoint is linear, so the write takes it by move and gives it back
    // while the box is full.
    auto delegator_end = std::move(delegator).send(
        std::move(carried),
        [](CarrierBox& box, CarriedHotEndpoint&& value) noexcept -> ::fixy::Option<CarriedHotEndpoint> {
            if (box.held.size() != 0) return ::fixy::Option<CarriedHotEndpoint>::some(std::move(value));
            static_cast<void>(box.held.append(std::move(value)).expect("the box is empty"));
            return ::fixy::none;
        });
    auto acceptor = s::mint_session<s::dual_of_t<Carrier>>(ctx, std::move(delegator_end).close());
    auto [received, acceptor_end] =
        std::move(acceptor).recv([](CarrierBox& box) noexcept { return box.held.take_last(); });
    CarrierBox carrier_back = std::move(acceptor_end).close();
    std::move(carrier_back.held).expect_empty("the receive takes the endpoint out of the box");
    CRUCIBLE_TEST_ASSERT(kTest, received.holds_endpoint());

    auto hot_endpoint = std::move(received).accept();
    auto hot_end = std::move(hot_endpoint)
                       .send(::fixy::mint_band<tier::Hot<ContentHash>>(payload),
                             [](HotWire& wire, tier::Hot<ContentHash>& value) noexcept {
                                 wire.sent = value.peek();
                                 return true;
                             });
    const HotWire hot_wire = std::move(hot_end).close();
    CRUCIBLE_TEST_ASSERT(kTest, hot_wire.marker == marker);
    CRUCIBLE_TEST_ASSERT(kTest, hot_wire.sent == payload);
}

}  // namespace

int main() {
    test_promote_and_demote_preserve_value();
    test_restore_returns_expected_warm_handle();
    test_restore_error_surface();
    test_hot_promote_delegate_protocol();
    ::fixy::check::pass(kTest, "cipher_tier_promotion: ok\n");
    return 0;
}
