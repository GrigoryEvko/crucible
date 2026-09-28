// fixy/Federation.h: the federation door admits a peer only through a
// handshake that verifies under the shared key, once for each nonce.
//
// The organizations are classes at namespace scope with external
// linkage, because the reflection hash that names an organization on
// the wire refuses a class with internal linkage.

#include <fixy/Federation.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

struct FederationTestOrg {};
struct FederationOtherOrg {};
struct FederationStatefulOrg {
    int id = 0;
};

namespace {

namespace eff = ::foundation::effects;
namespace fp = ::foundation::permissions;
namespace fed = ::fixy::federation;
namespace sh = ::fixy::siphash;
using ::fixy::mint_secret;
using ::fixy::Secret;

using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;
using Admission = fp::FederationAdmission<FederationTestOrg>;
using TestPolicy = fed::policy::admit_orgs<FederationTestOrg>;

[[nodiscard]] constexpr sh::Key test_key(std::uint8_t first) noexcept {
    sh::Key key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(static_cast<std::uint8_t>(first + i));
    }
    return key;
}

// ── Compile-time cells ──────────────────────────────────────────────

static_assert(fed::federation_org_id<FederationTestOrg>.is_set());
static_assert(fed::federation_org_id<FederationTestOrg> != fed::federation_org_id<FederationOtherOrg>);
static_assert(TestPolicy::admits<FederationTestOrg>);
static_assert(!TestPolicy::admits<FederationOtherOrg>);

// An organization is an empty class.  The door that signs a handshake,
// and the identity on the wire, refuse a type that is not a class and a
// class that holds state.
template <typename Org>
concept SignsHandshakeFor = requires(Secret<sh::Key>&& key) {
    fed::sign_handshake<Org>(std::move(key), fed::PeerKeyFingerprint{1}, fed::Nonce{1});
};
template <typename Org>
concept HasOrgId = requires { fed::federation_org_id<Org>; };
static_assert(SignsHandshakeFor<FederationTestOrg> && HasOrgId<FederationTestOrg>);
static_assert(!SignsHandshakeFor<int> && !HasOrgId<int>);
static_assert(!SignsHandshakeFor<FederationStatefulOrg> && !HasOrgId<FederationStatefulOrg>);

// The message is the domain word and the three fields, little-endian.
static_assert(fed::handshake_message(fed::OrgId{1}, fed::PeerKeyFingerprint{2}, fed::Nonce{3})[0] == std::byte{'c'});
static_assert(fed::handshake_message(fed::OrgId{1}, fed::PeerKeyFingerprint{2}, fed::Nonce{3})[7] == std::byte{'1'});
static_assert(fed::handshake_message(fed::OrgId{1}, fed::PeerKeyFingerprint{2}, fed::Nonce{3})[8] == std::byte{1});
static_assert(fed::handshake_message(fed::OrgId{1}, fed::PeerKeyFingerprint{2}, fed::Nonce{3})[16] == std::byte{2});
static_assert(fed::handshake_message(fed::OrgId{1}, fed::PeerKeyFingerprint{2}, fed::Nonce{3})[24] == std::byte{3});

// Each field is a type of its own.  No word converts into another word or
// comes from a raw integer, so a call that swaps two fields names no
// candidate.  <cstdint> brings std::uint64_t in with a using-declaration,
// which ^^ does not reflect, so an alias names the raw word.
using raw_word = std::uint64_t;

[[nodiscard]] consteval bool words_stay_apart() {
    const std::array words{std::meta::dealias(^^fed::OrgId), std::meta::dealias(^^fed::PeerKeyFingerprint),
                           std::meta::dealias(^^fed::Nonce), std::meta::dealias(^^fed::HandshakeMac)};
    for (const std::meta::info held : words) {
        if (std::meta::is_convertible_type(^^raw_word, held)) return false;
        for (const std::meta::info other : words) {
            if (std::meta::is_convertible_type(held, other) != (held == other)) return false;
        }
    }
    return true;
}
static_assert(words_stay_apart());

template <typename... Fields>
inline constexpr bool handshake_takes = std::is_invocable_v<decltype(&fed::handshake_message), Fields...>;
static_assert(handshake_takes<fed::OrgId, fed::PeerKeyFingerprint, fed::Nonce>);
static_assert(!handshake_takes<fed::PeerKeyFingerprint, fed::OrgId, fed::Nonce>
              && !handshake_takes<fed::OrgId, fed::Nonce, fed::PeerKeyFingerprint>
              && !handshake_takes<std::uint64_t, std::uint64_t, std::uint64_t>);

// The MAC word and the tag are the same eight bytes.
static_assert(fed::mac_of(fed::tag_of(fed::HandshakeMac{0x0123456789abcdefULL})).raw() == 0x0123456789abcdefULL);

// The admission is one object in one place: no copy and no move.
static_assert(!std::is_copy_constructible_v<Admission> && !std::is_move_constructible_v<Admission>);
static_assert(!std::is_default_constructible_v<Admission>);

// A foreground context cannot open the door; a context whose row holds
// IO can.
static_assert(fed::CtxFitsFederationAdmission<IoCtx, FederationTestOrg, TestPolicy>);
static_assert(!fed::CtxFitsFederationAdmission<eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>, FederationTestOrg, TestPolicy>);
static_assert(!fed::CtxFitsFederationAdmission<IoCtx, FederationOtherOrg, TestPolicy>);

// ── The replay window ───────────────────────────────────────────────

[[nodiscard]] constexpr bool window_follows_its_eviction_rule() noexcept {
    fed::ReplayWindow window{};
    if (window.verdict(fed::Nonce{0}) != fed::NonceVerdict::Missing) return false;
    if (window.verdict(fed::Nonce{5}) != fed::NonceVerdict::Fresh) return false;
    window.record(fed::Nonce{5});
    if (window.verdict(fed::Nonce{5}) != fed::NonceVerdict::Replayed) return false;
    // A nonce below the highest and inside the window is fresh once.
    if (window.verdict(fed::Nonce{3}) != fed::NonceVerdict::Fresh) return false;
    window.record(fed::Nonce{3});
    if (window.verdict(fed::Nonce{3}) != fed::NonceVerdict::Replayed) return false;
    // A move up by less than the width keeps the record of 3 and of 5.
    window.record(fed::Nonce{60});
    if (window.verdict(fed::Nonce{5}) != fed::NonceVerdict::Replayed) return false;
    if (window.verdict(fed::Nonce{3}) != fed::NonceVerdict::Replayed) return false;
    if (window.verdict(fed::Nonce{4}) != fed::NonceVerdict::Fresh) return false;
    // At an offset of 64 the nonce is out of the window and too old.
    window.record(fed::Nonce{67});
    if (window.verdict(fed::Nonce{3}) != fed::NonceVerdict::TooOld) return false;
    if (window.verdict(fed::Nonce{4}) != fed::NonceVerdict::Fresh) return false;
    if (window.verdict(fed::Nonce{5}) != fed::NonceVerdict::Replayed) return false;
    // A jump of the full width or more forgets everything below.
    window.record(fed::Nonce{1000});
    if (window.verdict(fed::Nonce{67}) != fed::NonceVerdict::TooOld) return false;
    if (window.verdict(fed::Nonce{1000}) != fed::NonceVerdict::Replayed) return false;
    if (window.verdict(fed::Nonce{999}) != fed::NonceVerdict::Fresh) return false;
    return window.verdict(fed::Nonce{1000 - 63}) == fed::NonceVerdict::Fresh
        && window.verdict(fed::Nonce{1000 - 64}) == fed::NonceVerdict::TooOld;
}
static_assert(window_follows_its_eviction_rule());

// ── Runtime cells ───────────────────────────────────────────────────

struct Fixture {
    IoCtx ctx{eff::testing::bg()};
};

[[nodiscard]] Admission open_test_admission(Fixture const& fixture) noexcept {
    return Admission::mint_federation_admission<TestPolicy>(fixture.ctx, mint_secret<sh::Key>(test_key(1)));
}

// One admittance.  The token of the local cipher goes in, and it comes
// back into the same variable with the same brand on each path.
template <typename LocalCipher>
[[nodiscard]] auto admit(Admission& admission, LocalCipher& local_cipher,
                         fed::FederationHandshake const& handshake) noexcept {
    auto [returned_cipher, admitted] = admission.mint_federation_admittance(std::move(local_cipher), handshake);
    static_assert(std::is_same_v<decltype(returned_cipher), LocalCipher>);
    local_cipher = std::move(returned_cipher);
    return std::move(admitted);
}

// A handshake that the peer tags under the shared key admits the peer,
// and the same handshake a second time is a replay.
int check_admits_once() {
    Fixture fixture;
    Admission admission = open_test_admission(fixture);
    auto local_cipher = fp::mint_permission_root<fed::LocalCipherTag>(fixture.ctx);
    auto [peer_key, handshake] = fed::sign_handshake<FederationTestOrg>(mint_secret<sh::Key>(test_key(1)),
                                                                        fed::PeerKeyFingerprint{42}, fed::Nonce{1});
    auto admitted = admit(admission, local_cipher, handshake);
    if (!admitted.has_value()) return 10;
    static_assert(std::is_same_v<typename std::remove_cvref_t<decltype(*admitted)>::tag_type,
                                 fp::tag::FederatedPeer<FederationTestOrg>>);
    auto replayed = admit(admission, local_cipher, handshake);
    if (replayed.has_value() || replayed.error() != fed::AdmittanceError::Replayed) return 11;

    auto [kept, second] = fed::sign_handshake<FederationTestOrg>(std::move(peer_key), fed::PeerKeyFingerprint{42},
                                                                 fed::Nonce{2});
    if (!admit(admission, local_cipher, second).has_value()) return 12;
    return 0;
}

// Each refusal names its reason, and a refused handshake moves nothing:
// the honest handshake with the same nonce is admitted afterwards.
int check_each_refusal() {
    Fixture fixture;
    Admission admission = open_test_admission(fixture);
    auto local_cipher = fp::mint_permission_root<fed::LocalCipherTag>(fixture.ctx);
    auto [peer_key, honest] = fed::sign_handshake<FederationTestOrg>(mint_secret<sh::Key>(test_key(1)),
                                                                     fed::PeerKeyFingerprint{42}, fed::Nonce{9});

    fed::FederationHandshake wrong_org = honest;
    wrong_org.org_id = fed::federation_org_id<FederationOtherOrg>;
    auto refused_org = admit(admission, local_cipher, wrong_org);
    if (refused_org.has_value() || refused_org.error() != fed::AdmittanceError::OrgMismatch) return 20;

    fed::FederationHandshake no_peer_key = honest;
    no_peer_key.peer_key_fingerprint = fed::PeerKeyFingerprint{};
    auto refused_peer_key = admit(admission, local_cipher, no_peer_key);
    if (refused_peer_key.has_value() || refused_peer_key.error() != fed::AdmittanceError::MissingPeerKey) return 21;

    fed::FederationHandshake no_nonce = honest;
    no_nonce.nonce = fed::Nonce{};
    auto refused_nonce = admit(admission, local_cipher, no_nonce);
    if (refused_nonce.has_value() || refused_nonce.error() != fed::AdmittanceError::MissingNonce) return 22;

    // A changed field breaks the tag, and so does a changed tag.
    fed::FederationHandshake other_peer_key = honest;
    other_peer_key.peer_key_fingerprint = fed::PeerKeyFingerprint{43};
    auto refused_field = admit(admission, local_cipher, other_peer_key);
    if (refused_field.has_value() || refused_field.error() != fed::AdmittanceError::BadMac) return 23;

    fed::FederationHandshake forged_mac = honest;
    forged_mac.mac = fed::HandshakeMac{honest.mac.raw() ^ 1U};
    auto refused_mac = admit(admission, local_cipher, forged_mac);
    if (refused_mac.has_value() || refused_mac.error() != fed::AdmittanceError::BadMac) return 24;

    // A forged handshake with a nonce far above the window does not move
    // the window, so it cannot push out the nonce of the honest one.
    fed::FederationHandshake far_forgery = honest;
    far_forgery.nonce = fed::Nonce{honest.nonce.raw() + 1000};
    auto refused_far = admit(admission, local_cipher, far_forgery);
    if (refused_far.has_value() || refused_far.error() != fed::AdmittanceError::BadMac) return 25;

    if (!admit(admission, local_cipher, honest).has_value()) return 26;
    return 0;
}

// A handshake tagged under another key does not verify.
int check_other_key() {
    Fixture fixture;
    Admission admission = open_test_admission(fixture);
    auto local_cipher = fp::mint_permission_root<fed::LocalCipherTag>(fixture.ctx);
    auto [stranger_key, handshake] = fed::sign_handshake<FederationTestOrg>(mint_secret<sh::Key>(test_key(2)),
                                                                            fed::PeerKeyFingerprint{42}, fed::Nonce{1});
    auto refused = admit(admission, local_cipher, handshake);
    if (refused.has_value() || refused.error() != fed::AdmittanceError::BadMac) return 30;
    return 0;
}

// A nonce more than 63 below the highest accepted one is refused as too
// old, even though the admission never saw it.
int check_too_old() {
    Fixture fixture;
    Admission admission = open_test_admission(fixture);
    auto local_cipher = fp::mint_permission_root<fed::LocalCipherTag>(fixture.ctx);
    auto [peer_key, late] = fed::sign_handshake<FederationTestOrg>(mint_secret<sh::Key>(test_key(1)),
                                                                   fed::PeerKeyFingerprint{42}, fed::Nonce{100});
    if (!admit(admission, local_cipher, late).has_value()) return 40;
    auto [kept, early] = fed::sign_handshake<FederationTestOrg>(std::move(peer_key), fed::PeerKeyFingerprint{42},
                                                                fed::Nonce{36});
    auto refused = admit(admission, local_cipher, early);
    if (refused.has_value() || refused.error() != fed::AdmittanceError::NonceTooOld) return 41;
    return 0;
}

int check_error_names() {
    if (fed::admittance_error_name(fed::AdmittanceError::BadMac) != "BadMac") return 50;
    if (fed::admittance_error_name(fed::AdmittanceError::Replayed) != "Replayed") return 51;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_admits_once(); rc != 0) return rc;
    if (int rc = check_each_refusal(); rc != 0) return rc;
    if (int rc = check_other_key(); rc != 0) return rc;
    if (int rc = check_too_old(); rc != 0) return rc;
    if (int rc = check_error_names(); rc != 0) return rc;
    return 0;
}
