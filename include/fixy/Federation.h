#pragma once

// The federation door.  A handshake that verifies under the key of the
// local organization is the one way to a token for a peer of another
// organization.
//
// The local organization holds a SipHash-2-4 key in a fixy::Secret.  A
// peer that holds the same key tags its handshake with it.  The
// admission below verifies that tag, refuses a nonce that it has seen
// or that is too old, and only then makes federation_admission_key, the
// key of the one constructor of Permission<tag::FederatedPeer<Org>>.
//
// foundation/permissions/Permission.h declares FederationAdmission and
// the key, and this header defines the admission.  The definition is
// the whole of the door, so scripts/check-federation-admission.py
// refuses a definition, a specialization or a member definition of
// FederationAdmission in any other file.  A member name of the
// admission is therefore refused as the last name of a qualified
// definition anywhere else in the tree, and each member name must be
// distinctive.
//
// Old spelling: include/crucible/permissions/FederationPermission.h.
// Its signature was a mix of public values with no key, it kept no
// record of the nonces it had seen, and the admission read a borrowed
// local permission that it then discarded.  Here each admittance takes
// the token of the local cipher and gives it back beside its result.
// The self-signed handshake, the mixer and the intra-organization split
// are not carried.

#include <fixy/SipHash.h>
#include <fixy/Secret.h>
#include <foundation/Brand.h>
#include <foundation/Pinned.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy::federation {

// The local cipher.  Each admittance takes a token for it and gives the
// token back, so the holder of one token does one admittance at a time.
// Cipher state lives on disk and on remote storage, so the row is IO.
struct LocalCipherTag {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

// An organization is named by an empty class.  Its identity on the wire
// is the stable identity of that class, which the reflection hash
// refuses for a closure, an unnamed class and a class with internal
// linkage.
template <typename Org>
concept FederationOrgTag = std::is_class_v<Org> && std::is_empty_v<Org>;

namespace detail::handshake_word {

struct Org {};
struct PeerKeyFingerprint {};
struct Nonce {};
struct Mac {};

}  // namespace detail::handshake_word

// One 64-bit field of the handshake.  Each field is a type of its own,
// so a positional call cannot swap two of them.  Zero means that the
// sender did not set the field.
template <typename Kind>
class HandshakeWord {
public:
    constexpr HandshakeWord() noexcept = default;
    constexpr explicit HandshakeWord(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_set() const noexcept { return value_ != 0; }

    constexpr auto operator<=>(const HandshakeWord&) const noexcept = default;

private:
    std::uint64_t value_ = 0;
};

using OrgId = HandshakeWord<detail::handshake_word::Org>;
using PeerKeyFingerprint = HandshakeWord<detail::handshake_word::PeerKeyFingerprint>;
using Nonce = HandshakeWord<detail::handshake_word::Nonce>;
using HandshakeMac = HandshakeWord<detail::handshake_word::Mac>;

static_assert(sizeof(OrgId) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<OrgId>);

template <typename Org>
    requires FederationOrgTag<Org>
inline constexpr OrgId federation_org_id = OrgId{::foundation::reflect::stable_type_id<Org>};

namespace policy {

// The organizations that a deployment admits.  The admission mint
// refuses an organization that the policy does not name.
template <typename... Orgs>
struct admit_orgs {
    template <typename Org>
    static constexpr bool admits = (std::same_as<Org, Orgs> || ...);
};

}  // namespace policy

struct FederationHandshake {
    OrgId org_id{};
    PeerKeyFingerprint peer_key_fingerprint{};
    Nonce nonce{};
    HandshakeMac mac{};
};

static_assert(std::is_trivially_copyable_v<FederationHandshake>);
static_assert(sizeof(FederationHandshake) == 32);

enum class AdmittanceError : std::uint8_t {
    OrgMismatch = 1,
    MissingPeerKey = 2,
    MissingNonce = 3,
    Replayed = 4,
    NonceTooOld = 5,
    BadMac = 6,
};

[[nodiscard]] constexpr std::string_view admittance_error_name(AdmittanceError error) noexcept {
    switch (error) {
        case AdmittanceError::OrgMismatch:
            return "OrgMismatch";
        case AdmittanceError::MissingPeerKey:
            return "MissingPeerKey";
        case AdmittanceError::MissingNonce:
            return "MissingNonce";
        case AdmittanceError::Replayed:
            return "Replayed";
        case AdmittanceError::NonceTooOld:
            return "NonceTooOld";
        case AdmittanceError::BadMac:
            return "BadMac";
        default:
            return "<unknown AdmittanceError>";
    }
}

// ── The message that the tag authenticates ───────────────────────────
//
// A domain word, then the organization, the peer key and the nonce,
// each as eight little-endian bytes.  The domain word keeps a tag made
// for a handshake from verifying any other message under the same key.
// Its bytes read "crnfedv1".

inline constexpr std::uint64_t handshake_domain = 0x3176'6465'666e'7263ULL;
inline constexpr std::size_t handshake_message_bytes = 32;

using HandshakeMessage = std::array<std::byte, handshake_message_bytes>;

namespace detail {

inline constexpr std::size_t word_bytes = 8;

constexpr void store_le64(HandshakeMessage& message, std::size_t offset, std::uint64_t word) noexcept {
    for (std::size_t i = 0; i < word_bytes; ++i) {
        message[offset + i] = static_cast<std::byte>(static_cast<std::uint8_t>(word >> (8 * i)));
    }
}

}  // namespace detail

[[nodiscard]] constexpr HandshakeMessage handshake_message(OrgId org_id, PeerKeyFingerprint peer_key_fingerprint,
                                                           Nonce nonce) noexcept {
    HandshakeMessage message{};
    detail::store_le64(message, 0, handshake_domain);
    detail::store_le64(message, 8, org_id.raw());
    detail::store_le64(message, 16, peer_key_fingerprint.raw());
    detail::store_le64(message, 24, nonce.raw());
    return message;
}

// A tag and a MAC word are the same eight bytes, read little-endian.
[[nodiscard]] constexpr HandshakeMac mac_of(siphash::Tag const& tag) noexcept {
    std::uint64_t word = 0;
    for (std::size_t i = 0; i < siphash::tag_bytes; ++i) {
        word |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(tag[i])) << (8 * i);
    }
    return HandshakeMac{word};
}

[[nodiscard]] constexpr siphash::Tag tag_of(HandshakeMac mac) noexcept {
    siphash::Tag tag{};
    for (std::size_t i = 0; i < siphash::tag_bytes; ++i) {
        tag[i] = static_cast<std::byte>(static_cast<std::uint8_t>(mac.raw() >> (8 * i)));
    }
    return tag;
}

// The handshake of a peer of Org, tagged under the key that the peer
// shares with the local organization.  The key comes back beside it.
template <typename Org>
    requires FederationOrgTag<Org>
[[nodiscard]] constexpr std::pair<Secret<siphash::Key>, FederationHandshake>
sign_handshake(Secret<siphash::Key>&& key, PeerKeyFingerprint peer_key_fingerprint, Nonce nonce) noexcept {
    OrgId const org_id = federation_org_id<Org>;
    HandshakeMessage const message = handshake_message(org_id, peer_key_fingerprint, nonce);
    auto [kept, tag] = siphash::sign(std::move(key), message);
    return {std::move(kept), FederationHandshake{.org_id = org_id,
                                                 .peer_key_fingerprint = peer_key_fingerprint,
                                                 .nonce = nonce,
                                                 .mac = mac_of(tag)}};
}

// ── The seen-nonce store ─────────────────────────────────────────────
//
// The anti-replay window of IPsec (RFC 4303, section 3.4.3), over a
// 64-bit mask.  The window holds the highest nonce that it accepted and
// the 63 nonces below it.  Bit k of the mask is set when the nonce
// highest - k was accepted.
//
// Eviction rule: a nonce above the highest moves the window up to it,
// and each nonce that falls out of the bottom is forgotten.  The window
// refuses a nonce that it forgot, and every nonce more than 63 below
// the highest, as too old, because it can no longer tell whether it
// accepted that nonce.  So each peer takes its nonces from one
// increasing sequence, and a message that arrives more than 63 nonces
// late is refused.
//
// Zero is not a nonce: it means that the sender did not set the field.
//
// The store is 16 bytes, and each operation is O(1).

enum class NonceVerdict : std::uint8_t {
    Fresh,
    Missing,
    Replayed,
    TooOld,
};

class ReplayWindow {
public:
    static constexpr std::uint64_t width = 64;

    [[nodiscard]] constexpr NonceVerdict verdict(Nonce nonce) const noexcept {
        if (!nonce.is_set()) return NonceVerdict::Missing;
        if (nonce.raw() > highest_) return NonceVerdict::Fresh;
        std::uint64_t const offset = highest_ - nonce.raw();
        if (offset >= width) return NonceVerdict::TooOld;
        return ((seen_ >> offset) & std::uint64_t{1}) != 0 ? NonceVerdict::Replayed : NonceVerdict::Fresh;
    }

    // Records one accepted nonce.  Call it only after the message that
    // carried the nonce verified, so that a forged message cannot move
    // the window and push out the nonces of honest ones.
    constexpr void record(Nonce nonce) noexcept {
        CRUCIBLE_PRE(verdict(nonce) == NonceVerdict::Fresh);
        if (nonce.raw() > highest_) {
            std::uint64_t const shift = nonce.raw() - highest_;
            seen_ = shift >= width ? std::uint64_t{1} : (seen_ << shift) | std::uint64_t{1};
            highest_ = nonce.raw();
            return;
        }
        seen_ |= std::uint64_t{1} << (highest_ - nonce.raw());
    }

private:
    std::uint64_t highest_ = 0;
    std::uint64_t seen_ = 0;
};

// The fit of the admission mint.  The context must admit the row of the
// local cipher and the row of the peer token, and the policy must name
// the organization.
template <typename Ctx, typename Org, typename Policy>
concept CtxFitsFederationAdmission =
    ::foundation::effects::IsExecCtx<Ctx> && FederationOrgTag<Org>
    && ::foundation::permissions::CtxAdmitsPermission<LocalCipherTag, Ctx>
    && ::foundation::permissions::CtxAdmitsPermission<::foundation::permissions::tag::FederatedPeer<Org>, Ctx>
    && Policy::template admits<Org>;

// The peer token takes a fresh brand from each call site.  A caller
// cannot name the brand: an explicit template argument lands in this
// pack, and the pack must be empty.
template <typename... Explicit>
concept AdmittanceNamesNoBrand = sizeof...(Explicit) == 0;

}  // namespace fixy::federation

// The admission of peers of one organization.  It is Pinned, and one
// thread owns it: each admittance reads and writes the replay window.
//
// Every member that could reach the key is defined here, in the class
// body.  The guard refuses a definition of one elsewhere, and the
// language refuses a second definition in a file that includes this one.
// The copy and move members are implicit, because Pinned deletes them,
// and the language does not let a file specialize an implicit member.
template <typename Org>
class foundation::permissions::FederationAdmission final
    : ::foundation::Pinned<::foundation::permissions::FederationAdmission<Org>> {
    static_assert(::fixy::federation::FederationOrgTag<Org>,
                  "FederationAdmission<Org>: Org must be an empty class that names an organization.");

public:
    // The admission of Org, under a key that the local organization
    // shares with its peers of Org.  The admission holds no token of the
    // local cipher.  Each admittance takes one and gives it back.
    template <typename Policy, typename Ctx>
        requires ::fixy::federation::CtxFitsFederationAdmission<Ctx, Org, Policy>
    [[nodiscard]] static constexpr FederationAdmission mint_federation_admission(
        Ctx const&, ::fixy::Secret<::fixy::siphash::Key>&& key) noexcept {
        return FederationAdmission{std::move(key)};
    }

    // A token for the peer whose handshake this is, or the reason for
    // the refusal.  The token of the local cipher comes back beside the
    // result on each path, with the brand that it came in with.
    template <typename... Explicit, typename LocalBrand, typename Brand = CRUCIBLE_FRESH_BRAND>
        requires ::fixy::federation::AdmittanceNamesNoBrand<Explicit...>
    [[nodiscard]] constexpr std::pair<
        Permission<::fixy::federation::LocalCipherTag, LocalBrand>,
        std::expected<Permission<tag::FederatedPeer<Org>, Brand>, ::fixy::federation::AdmittanceError>>
    mint_federation_admittance(Permission<::fixy::federation::LocalCipherTag, LocalBrand>&& local_cipher,
                               ::fixy::federation::FederationHandshake const& handshake) noexcept {
        std::expected<void, ::fixy::federation::AdmittanceError> const verdict =
            verify_federation_handshake_(handshake);
        if (!verdict.has_value()) {
            return {std::move(local_cipher), std::unexpected(verdict.error())};
        }
        return {std::move(local_cipher), Permission<tag::FederatedPeer<Org>, Brand>{federation_admission_key{}}};
    }

private:
    constexpr explicit FederationAdmission(::fixy::Secret<::fixy::siphash::Key>&& key) noexcept
        : admission_key_{std::move(key)} {}

    // The checks of the public fields and of the window come first, and
    // they record nothing.  The window records the nonce only after the
    // tag verifies.  The check depends on Org alone, so each brand of a
    // peer token shares one copy of it.
    [[nodiscard]] constexpr std::expected<void, ::fixy::federation::AdmittanceError> verify_federation_handshake_(
        ::fixy::federation::FederationHandshake const& handshake) noexcept {
        using ::fixy::federation::AdmittanceError;
        using ::fixy::federation::NonceVerdict;
        if (handshake.org_id != ::fixy::federation::federation_org_id<Org>) {
            return std::unexpected(AdmittanceError::OrgMismatch);
        }
        if (!handshake.peer_key_fingerprint.is_set()) {
            return std::unexpected(AdmittanceError::MissingPeerKey);
        }
        switch (admission_window_.verdict(handshake.nonce)) {
            case NonceVerdict::Missing:
                return std::unexpected(AdmittanceError::MissingNonce);
            case NonceVerdict::Replayed:
                return std::unexpected(AdmittanceError::Replayed);
            case NonceVerdict::TooOld:
                return std::unexpected(AdmittanceError::NonceTooOld);
            case NonceVerdict::Fresh:
                break;
            default:
                std::unreachable();
        }
        ::fixy::federation::HandshakeMessage const message = ::fixy::federation::handshake_message(
            handshake.org_id, handshake.peer_key_fingerprint, handshake.nonce);
        auto [kept, verified] = ::fixy::siphash::verify(std::move(admission_key_), message,
                                                        ::fixy::federation::tag_of(handshake.mac));
        admission_key_ = std::move(kept);
        if (!verified) {
            return std::unexpected(AdmittanceError::BadMac);
        }
        admission_window_.record(handshake.nonce);
        return {};
    }

    ::fixy::Secret<::fixy::siphash::Key> admission_key_;
    ::fixy::federation::ReplayWindow admission_window_{};
};
