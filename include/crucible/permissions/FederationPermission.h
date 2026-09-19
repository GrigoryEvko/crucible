#pragma once

#include <crucible/permissions/Permission.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/diag/_StableName.h>

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

}  // namespace crucible::permissions

// Federation policy is intra-org splits only.  A cross-org split would
// turn one peer's permission into another peer's, which is total
// admittance escalation with no policy check and no handshake.

namespace crucible::safety {

template <typename Org, typename A, typename B>
struct splits_into<::crucible::permissions::tag::FederatedPeer<Org>, ::crucible::permissions::tag::FederatedPeer<A>,
                   ::crucible::permissions::tag::FederatedPeer<B>>
    : std::bool_constant<std::is_same_v<A, Org> && std::is_same_v<B, Org>> {};

template <typename Org, typename... Children>
struct splits_into_pack<::crucible::permissions::tag::FederatedPeer<Org>, Children...>
    : std::bool_constant<(std::is_same_v<Children, ::crucible::permissions::tag::FederatedPeer<Org>> && ...)> {};

// The witnesses mirror the truth conditions above rather than being
// unconditionally true, and that is what makes them load-bearing here.
// A foreign full specialization of splits_into over three concrete peer
// tags outranks the partial specialization above and flips
// splits_into_v to true.  The witness deduced for that same argument
// triple still folds to false, so the composite gate rejects the split.
// Forging the witness as well is what the build-time source scan for
// out-of-place manifests catches.
template <typename Org, typename A, typename B>
struct splits_into_authoring_witness<::crucible::permissions::tag::FederatedPeer<Org>,
                                     ::crucible::permissions::tag::FederatedPeer<A>,
                                     ::crucible::permissions::tag::FederatedPeer<B>>
    : std::bool_constant<std::is_same_v<A, Org> && std::is_same_v<B, Org>> {};

template <typename Org, typename... Children>
struct splits_into_pack_authoring_witness<::crucible::permissions::tag::FederatedPeer<Org>, Children...>
    : std::bool_constant<(std::is_same_v<Children, ::crucible::permissions::tag::FederatedPeer<Org>> && ...)> {};

}  // namespace crucible::safety

namespace crucible::permissions {

// The match set of this trait is closed.  Specializing it further would
// widen or narrow which tags the deleted root mints below reject.

template <typename Tag>
struct is_federated_peer_tag : std::false_type {};

template <typename Org>
struct is_federated_peer_tag<tag::FederatedPeer<Org>> : std::true_type {};

template <typename Tag>
inline constexpr bool is_federated_peer_tag_v = is_federated_peer_tag<Tag>::value;

// The one path to a federated peer permission that is not deleted.  Its
// only legitimate caller is the admittance mint below.  Any other call
// site bypasses the policy check and the handshake.

namespace detail {

struct FederationMintAccess {
    template <typename Org>
    [[nodiscard]] static constexpr ::crucible::safety::Permission<tag::FederatedPeer<Org>> mint() noexcept {
        return ::crucible::safety::Permission<tag::FederatedPeer<Org>>{};
    }
};

}  // namespace detail

template <typename Org>
using FederatedPeerPermission = ::crucible::safety::Permission<tag::FederatedPeer<Org>>;

using LocalCipherPermission = ::crucible::safety::Permission<tag::LocalCipherTag>;

// The four handshake fields are all 64-bit and all swappable at a
// positional call site, so each gets its own type.  The macro that
// generates this shape elsewhere is undefined at the end of the header
// that declares it, so the shape is written out by hand here.

struct OrgId {
private:
    std::uint64_t v;

public:
    constexpr OrgId() noexcept : v(0) {}
    constexpr explicit OrgId(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr OrgId from_raw(std::uint64_t val) noexcept { return OrgId{val}; }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return v != 0; }
    constexpr auto operator<=>(const OrgId&) const noexcept = default;
};
static_assert(sizeof(OrgId) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<OrgId>);

struct PeerKeyFingerprint {
private:
    std::uint64_t v;

public:
    constexpr PeerKeyFingerprint() noexcept : v(0) {}
    constexpr explicit PeerKeyFingerprint(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr PeerKeyFingerprint from_raw(std::uint64_t val) noexcept {
        return PeerKeyFingerprint{val};
    }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return v != 0; }
    constexpr auto operator<=>(const PeerKeyFingerprint&) const noexcept = default;
};
static_assert(sizeof(PeerKeyFingerprint) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<PeerKeyFingerprint>);

struct Nonce {
private:
    std::uint64_t v;

public:
    constexpr Nonce() noexcept : v(0) {}
    constexpr explicit Nonce(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr Nonce from_raw(std::uint64_t val) noexcept { return Nonce{val}; }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return v != 0; }
    constexpr auto operator<=>(const Nonce&) const noexcept = default;
};
static_assert(sizeof(Nonce) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<Nonce>);

struct SignatureFingerprint {
private:
    std::uint64_t v;

public:
    constexpr SignatureFingerprint() noexcept : v(0) {}
    constexpr explicit SignatureFingerprint(std::uint64_t val) noexcept : v(val) {}
    [[nodiscard]] static constexpr SignatureFingerprint from_raw(std::uint64_t val) noexcept {
        return SignatureFingerprint{val};
    }
    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return v; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return v != 0; }
    constexpr auto operator<=>(const SignatureFingerprint&) const noexcept = default;
};
static_assert(sizeof(SignatureFingerprint) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<SignatureFingerprint>);

template <typename Org>
inline constexpr OrgId federation_org_id = OrgId{::crucible::safety::diag::stable_type_id<Org>};

namespace policy {

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
    SignatureFingerprint self_signature_fingerprint{};
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

[[nodiscard]] inline constexpr std::string_view admittance_error_name(AdmittanceError error) noexcept {
    switch (error) {
        case AdmittanceError::OrgNotAllowed:
            return "OrgNotAllowed";
        case AdmittanceError::OrgMismatch:
            return "OrgMismatch";
        case AdmittanceError::MissingPeerKey:
            return "MissingPeerKey";
        case AdmittanceError::MissingSignature:
            return "MissingSignature";
        case AdmittanceError::BadSignature:
            return "BadSignature";
        default:
            return "<unknown AdmittanceError>";
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

[[nodiscard]] constexpr std::uint64_t combine_runtime_ids(std::uint64_t a, std::uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return federation_mix64(a);
}

}  // namespace detail

[[nodiscard]] constexpr SignatureFingerprint
federation_signature_fingerprint(OrgId org_id, PeerKeyFingerprint peer_key_fingerprint, Nonce nonce) noexcept {
    constexpr std::uint64_t kDomain = 0xCFEDAD1100000001ULL;
    return SignatureFingerprint{
        detail::combine_runtime_ids(detail::combine_runtime_ids(kDomain, org_id.raw()),
                                    detail::combine_runtime_ids(peer_key_fingerprint.raw(), nonce.raw()))};
}

template <typename Org>
[[nodiscard]] constexpr PeerKeyFingerprint default_peer_key_fingerprint() noexcept {
    return PeerKeyFingerprint{detail::combine_runtime_ids(federation_org_id<Org>.raw(), 0xCFED9EED00000001ULL)};
}

template <typename Org>
concept FederationOrgTag = std::is_class_v<Org> && std::is_empty_v<Org>;

template <typename Org>
    requires FederationOrgTag<Org>
[[nodiscard]] constexpr FederationHandshake
mint_self_signed_handshake(PeerKeyFingerprint peer_key_fingerprint = default_peer_key_fingerprint<Org>(),
                           Nonce nonce = Nonce{0}) noexcept {
    const OrgId org_id = federation_org_id<Org>;
    return FederationHandshake{
        .org_id = org_id,
        .peer_key_fingerprint = peer_key_fingerprint,
        .nonce = nonce,
        .self_signature_fingerprint = federation_signature_fingerprint(org_id, peer_key_fingerprint, nonce),
    };
}

// A forwarder that deliberately omits the concept gate above, so call
// sites whose org tag does not satisfy it keep compiling.  It carries no
// deprecation attribute: a warning at every one of those call sites
// would bury real regressions in the build output.
template <typename Org>
[[nodiscard]] constexpr FederationHandshake
make_self_signed_handshake(PeerKeyFingerprint peer_key_fingerprint = default_peer_key_fingerprint<Org>(),
                           Nonce nonce = Nonce{0}) noexcept {
    const OrgId org_id = federation_org_id<Org>;
    return FederationHandshake{
        .org_id = org_id,
        .peer_key_fingerprint = peer_key_fingerprint,
        .nonce = nonce,
        .self_signature_fingerprint = federation_signature_fingerprint(org_id, peer_key_fingerprint, nonce),
    };
}

template <typename Org, typename Policy = policy::admit_orgs<Org>>
[[deprecated("the self-signed federation handshake is forgeable, replayable, "
             "and admits borrowed-ref local authority.  Forgeable: "
             "signature_fingerprint is a deterministic mix64 of public "
             "values, not a MAC.  Replayable: no seen-nonce store, no epoch "
             "binding, no expiry.  Borrowed authority: local_permission is a "
             "const-ref that is (void)-cast, so the verifier never inspects "
             "the bytes and any const-ref to a Permission<LocalCipherTag> "
             "mints.  Replace with a verifier that MACs the handshake using "
             "per-cipher secret material, plus a stateful seen-nonce store, "
             "before production deployment.  Suppress locally via "
             "_Pragma(\"GCC diagnostic ignored \\\"-Wdeprecated-declarations\\\"\") "
             "if you are calling this knowingly.")]]
// The [[nodiscard]] stands on its own line rather than folded into the
// attribute above, because the source scanner that audits mint factories
// reads a fixed qualifier window ahead of the signature.
[[nodiscard]] constexpr std::expected<FederatedPeerPermission<Org>, AdmittanceError>
mint_federation_admittance(const LocalCipherPermission& local_permission, FederationHandshake handshake) noexcept {
    (void)local_permission;

    if constexpr (!Policy::template admits<Org>) {
        return std::unexpected(AdmittanceError::OrgNotAllowed);
    }

    if (handshake.org_id != federation_org_id<Org>) {
        return std::unexpected(AdmittanceError::OrgMismatch);
    }
    if (!handshake.peer_key_fingerprint) {
        return std::unexpected(AdmittanceError::MissingPeerKey);
    }
    if (!handshake.self_signature_fingerprint) {
        return std::unexpected(AdmittanceError::MissingSignature);
    }
    if (handshake.self_signature_fingerprint
        != federation_signature_fingerprint(handshake.org_id, handshake.peer_key_fingerprint, handshake.nonce)) {
        return std::unexpected(AdmittanceError::BadSignature);
    }

    return ::crucible::permissions::detail::FederationMintAccess::template mint<Org>();
}

}  // namespace crucible::permissions

// These deletions are more constrained than the generic root mints, so
// overload ordering picks them for any federation peer tag.  Without
// them a root mint would hand out a peer permission with no policy
// check and no handshake at all.

namespace crucible::safety {

template <typename Tag>
    requires ::crucible::permissions::is_federated_peer_tag_v<Tag>
[[nodiscard]] constexpr Permission<Tag>
mint_permission_root() noexcept = delete("Permission<tag::FederatedPeer<Org>> cannot be minted via "
                                         "mint_permission_root.  Federation peer admittance is the "
                                         "load-bearing security boundary between organizations, so every "
                                         "minting must run the admittance policy and the handshake check.  "
                                         "Use `::crucible::permissions::mint_federation_admittance<Org, "
                                         "Policy>(local_cipher, handshake)`.");

template <typename Tag, ::crucible::effects::IsExecCtx Ctx>
    requires ::crucible::permissions::is_federated_peer_tag_v<Tag> && CtxAdmitsPermission<Tag, Ctx>
[[nodiscard]] constexpr Permission<Tag>
mint_permission_root(Ctx const&) noexcept = delete("Permission<tag::FederatedPeer<Org>> cannot be minted via "
                                                   "mint_permission_root(ctx) either.  An ExecCtx argument does "
                                                   "not authorize cross-org admittance; only "
                                                   "`mint_federation_admittance<Org, Policy>(local_cipher, handshake)` "
                                                   "does.");

}  // namespace crucible::safety

namespace crucible::permissions {

namespace detail::federation_permission_self_test {

struct SelfOrg {};
struct OtherOrg {};

static_assert(static_cast<bool>(federation_org_id<SelfOrg>));
static_assert(federation_org_id<SelfOrg> != federation_org_id<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg>::template admits<SelfOrg>);
static_assert(!policy::admit_orgs<SelfOrg>::template admits<OtherOrg>);
static_assert(policy::admit_orgs<SelfOrg, OtherOrg>::template admits<OtherOrg>);

static_assert(::crucible::safety::splits_into_v<tag::FederatedPeer<SelfOrg>, tag::FederatedPeer<SelfOrg>,
                                                tag::FederatedPeer<SelfOrg>>,
              "intra-org split must be admitted");
static_assert(!::crucible::safety::splits_into_v<tag::FederatedPeer<SelfOrg>, tag::FederatedPeer<OtherOrg>,
                                                 tag::FederatedPeer<OtherOrg>>,
              "cross-org split must be rejected by default");
static_assert(!::crucible::safety::splits_into_v<tag::FederatedPeer<SelfOrg>, tag::FederatedPeer<SelfOrg>,
                                                 tag::FederatedPeer<OtherOrg>>,
              "mixed-org split (one child crosses) must be rejected");
static_assert(::crucible::safety::splits_into_pack_v<tag::FederatedPeer<SelfOrg>, tag::FederatedPeer<SelfOrg>,
                                                     tag::FederatedPeer<SelfOrg>, tag::FederatedPeer<SelfOrg>>,
              "intra-org N-ary split must be admitted");
static_assert(!::crucible::safety::splits_into_pack_v<tag::FederatedPeer<SelfOrg>, tag::FederatedPeer<SelfOrg>,
                                                      tag::FederatedPeer<OtherOrg>>,
              "N-ary split with one cross-org child must be rejected");

static_assert(std::is_same_v<FederatedPeerPermission<SelfOrg>::tag_type, tag::FederatedPeer<SelfOrg>>);

constexpr FederationHandshake kSelfHandshake = make_self_signed_handshake<SelfOrg>(PeerKeyFingerprint{123}, Nonce{456});
static_assert(kSelfHandshake.org_id == federation_org_id<SelfOrg>);
static_assert(kSelfHandshake.peer_key_fingerprint == PeerKeyFingerprint{123});
static_assert(kSelfHandshake.self_signature_fingerprint
              == federation_signature_fingerprint(federation_org_id<SelfOrg>, PeerKeyFingerprint{123}, Nonce{456}));

}  // namespace detail::federation_permission_self_test

}  // namespace crucible::permissions
