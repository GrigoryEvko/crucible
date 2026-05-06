#pragma once

// ── FederationPermission — typed cross-organization admission ───────
//
// This header is the permission boundary for Cipher federation.  The
// low-level wire codec can still parse bytes as an untrusted frame, but
// consumers that want a trusted federation payload must first mint
// Permission<tag::FederatedPeer<Org>> through the admittance factory.
//
// STATUS: V1 exploratory implementation. GAPS-107, the formal federation
// peer-trust-model decision, is still pending. The current trust model is
// self-signed peer identity fingerprints. The actual cryptographic verifier is
// intentionally outside this header; mint_federation_admittance is the
// substitution point that must be revisited when GAPS-107 chooses the verifier
// and key-distribution discipline. This layer pins the type-level authority
// flow and the deterministic fingerprint contract the verifier must satisfy.

#include <crucible/permissions/Permission.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/diag/StableName.h>

#include <cstdint>
#include <concepts>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::safety::source {

template <typename Org>
struct FederatedPeer {
    using org_type = Org;
};

}  // namespace crucible::safety::source

namespace crucible::permissions {

namespace tag {

struct LocalCipherTag {};

template <typename Org>
struct FederatedPeer {
    using org_type = Org;
};

}  // namespace tag

template <typename Org>
using FederatedPeerPermission =
    ::crucible::safety::Permission<tag::FederatedPeer<Org>>;

using LocalCipherPermission =
    ::crucible::safety::Permission<tag::LocalCipherTag>;

template <typename Org>
inline constexpr std::uint64_t federation_org_id =
    ::crucible::safety::diag::stable_type_id<Org>;

namespace policy {

template <typename... Orgs>
struct admit_orgs {
    template <typename Org>
    static constexpr bool admits =
        (std::same_as<Org, Orgs> || ...);
};

}  // namespace policy

struct FederationHandshake {
    std::uint64_t org_id = 0;
    std::uint64_t peer_key_fingerprint = 0;
    std::uint64_t nonce = 0;
    std::uint64_t self_signature_fingerprint = 0;
};

static_assert(std::is_trivially_copyable_v<FederationHandshake>);
static_assert(sizeof(FederationHandshake) == 32);

enum class AdmittanceError : std::uint8_t {
    OrgNotAllowed = 1,
    OrgMismatch = 2,
    MissingPeerKey = 3,
    MissingSignature = 4,
    BadSignature = 5,
};

[[nodiscard]] inline constexpr std::string_view
admittance_error_name(AdmittanceError error) noexcept {
    switch (error) {
        case AdmittanceError::OrgNotAllowed:    return "OrgNotAllowed";
        case AdmittanceError::OrgMismatch:      return "OrgMismatch";
        case AdmittanceError::MissingPeerKey:   return "MissingPeerKey";
        case AdmittanceError::MissingSignature: return "MissingSignature";
        case AdmittanceError::BadSignature:     return "BadSignature";
        default:                                return "<unknown AdmittanceError>";
    }
}

namespace detail {

[[nodiscard]] constexpr std::uint64_t federation_mix64(std::uint64_t k) noexcept {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

[[nodiscard]] constexpr std::uint64_t combine_runtime_ids(
    std::uint64_t a,
    std::uint64_t b) noexcept
{
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return federation_mix64(a);
}

}  // namespace detail

[[nodiscard]] constexpr std::uint64_t federation_signature_fingerprint(
    std::uint64_t org_id,
    std::uint64_t peer_key_fingerprint,
    std::uint64_t nonce) noexcept
{
    constexpr std::uint64_t kDomain = 0xCFED'AD11'0000'0001ULL;
    return detail::combine_runtime_ids(
        detail::combine_runtime_ids(kDomain, org_id),
        detail::combine_runtime_ids(peer_key_fingerprint, nonce));
}

template <typename Org>
[[nodiscard]] constexpr std::uint64_t default_peer_key_fingerprint() noexcept {
    return detail::combine_runtime_ids(
        federation_org_id<Org>,
        0xCFED'9EED'0000'0001ULL);
}

template <typename Org>
[[nodiscard]] constexpr FederationHandshake
make_self_signed_handshake(
    std::uint64_t peer_key_fingerprint =
        default_peer_key_fingerprint<Org>(),
    std::uint64_t nonce = 0) noexcept
{
    const std::uint64_t org_id = federation_org_id<Org>;
    return FederationHandshake{
        .org_id = org_id,
        .peer_key_fingerprint = peer_key_fingerprint,
        .nonce = nonce,
        .self_signature_fingerprint =
            federation_signature_fingerprint(
                org_id, peer_key_fingerprint, nonce),
    };
}

template <typename Org,
          typename Policy = policy::admit_orgs<Org>>
[[nodiscard]] constexpr std::expected<
    FederatedPeerPermission<Org>,
    AdmittanceError>
mint_federation_admittance(
    const LocalCipherPermission& local_permission,
    FederationHandshake handshake) noexcept
{
    (void)local_permission;

    if constexpr (!Policy::template admits<Org>) {
        return std::unexpected(AdmittanceError::OrgNotAllowed);
    }

    if (handshake.org_id != federation_org_id<Org>) {
        return std::unexpected(AdmittanceError::OrgMismatch);
    }
    if (handshake.peer_key_fingerprint == 0) {
        return std::unexpected(AdmittanceError::MissingPeerKey);
    }
    if (handshake.self_signature_fingerprint == 0) {
        return std::unexpected(AdmittanceError::MissingSignature);
    }
    if (handshake.self_signature_fingerprint
        != federation_signature_fingerprint(
            handshake.org_id,
            handshake.peer_key_fingerprint,
            handshake.nonce)) {
        return std::unexpected(AdmittanceError::BadSignature);
    }

    return ::crucible::safety::mint_permission_root<
        tag::FederatedPeer<Org>>();
}

namespace detail::federation_permission_self_test {

struct SelfOrg {};
struct OtherOrg {};

static_assert(federation_org_id<SelfOrg> != 0);
static_assert(federation_org_id<SelfOrg> != federation_org_id<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg>::template admits<SelfOrg>);
static_assert(!policy::admit_orgs<SelfOrg>::template admits<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg, OtherOrg>::template admits<OtherOrg>);

static_assert(std::is_same_v<
    FederatedPeerPermission<SelfOrg>::tag_type,
    tag::FederatedPeer<SelfOrg>>);

constexpr FederationHandshake kSelfHandshake =
    make_self_signed_handshake<SelfOrg>(123, 456);
static_assert(kSelfHandshake.org_id == federation_org_id<SelfOrg>);
static_assert(kSelfHandshake.peer_key_fingerprint == 123);
static_assert(kSelfHandshake.self_signature_fingerprint
              == federation_signature_fingerprint(
                  federation_org_id<SelfOrg>, 123, 456));

}  // namespace detail::federation_permission_self_test

}  // namespace crucible::permissions
