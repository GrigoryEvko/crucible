#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/safety/IsTagged.h>

#include "test_assert.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <type_traits>

namespace fed = crucible::cipher::federation;
namespace eff = crucible::effects;
namespace perm = crucible::permissions;
namespace saf = crucible::safety;

namespace {

struct OrgSelf {};
struct OrgPeer {};
struct OrgBlocked {};

inline void f_payload(int) noexcept {}

using RowIO = eff::Row<eff::Effect::IO>;
using AllowSelfAndPeer = perm::policy::admit_orgs<OrgSelf, OrgPeer>;

static_assert(perm::federation_org_id<OrgSelf> != 0);
static_assert(perm::federation_org_id<OrgSelf>
              != perm::federation_org_id<OrgPeer>);
static_assert(AllowSelfAndPeer::template admits<OrgSelf>);
static_assert(AllowSelfAndPeer::template admits<OrgPeer>);
static_assert(!AllowSelfAndPeer::template admits<OrgBlocked>);

static_assert(std::is_same_v<
    perm::FederatedPeerPermission<OrgPeer>::tag_type,
    perm::tag::FederatedPeer<OrgPeer>>);

const perm::LocalCipherPermission& local_cipher_permission() {
    static const auto permission =
        saf::mint_permission_root<perm::tag::LocalCipherTag>();
    return permission;
}

int test_admittance_success() {
    const auto handshake =
        perm::make_self_signed_handshake<OrgPeer>(0xA11CE, 7);
    auto admitted =
        perm::mint_federation_admittance<OrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), handshake);
    assert(admitted.has_value());
    return 0;
}

int test_admittance_rejections() {
    const auto good =
        perm::make_self_signed_handshake<OrgPeer>(0xB0B, 9);

    auto not_allowed =
        perm::mint_federation_admittance<OrgBlocked, AllowSelfAndPeer>(
            local_cipher_permission(),
            perm::make_self_signed_handshake<OrgBlocked>(0xCAFE, 1));
    assert(!not_allowed.has_value());
    assert(not_allowed.error() == perm::AdmittanceError::OrgNotAllowed);

    auto wrong_org =
        perm::mint_federation_admittance<OrgSelf, AllowSelfAndPeer>(
            local_cipher_permission(), good);
    assert(!wrong_org.has_value());
    assert(wrong_org.error() == perm::AdmittanceError::OrgMismatch);

    auto missing_key = good;
    missing_key.peer_key_fingerprint = 0;
    auto missing_key_result =
        perm::mint_federation_admittance<OrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), missing_key);
    assert(!missing_key_result.has_value());
    assert(missing_key_result.error()
           == perm::AdmittanceError::MissingPeerKey);

    auto missing_sig = good;
    missing_sig.self_signature_fingerprint = 0;
    auto missing_sig_result =
        perm::mint_federation_admittance<OrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), missing_sig);
    assert(!missing_sig_result.has_value());
    assert(missing_sig_result.error()
           == perm::AdmittanceError::MissingSignature);

    auto bad_sig = good;
    bad_sig.self_signature_fingerprint ^= 0x55AA;
    auto bad_sig_result =
        perm::mint_federation_admittance<OrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(), bad_sig);
    assert(!bad_sig_result.has_value());
    assert(bad_sig_result.error() == perm::AdmittanceError::BadSignature);

    return 0;
}

int test_permissioned_deserialize_tags_payload() {
    std::array<std::uint8_t, 64> buf{};
    const std::array<std::uint8_t, 4> body = {1, 2, 3, 4};

    auto written = fed::serialize_computation_cache_federation_entry<
        &f_payload, RowIO, int>(local_cipher_permission(), buf, body);
    assert(written.has_value());

    auto admitted =
        perm::mint_federation_admittance<OrgPeer, AllowSelfAndPeer>(
            local_cipher_permission(),
            perm::make_self_signed_handshake<OrgPeer>(0xF00D, 11));
    assert(admitted.has_value());

    auto tagged_view = fed::deserialize_federation_entry(
        *admitted,
        std::span<const std::uint8_t>(buf.data(), *written),
        static_cast<std::uint16_t>(eff::OsUniverse::cardinality));
    assert(tagged_view.has_value());

    using TaggedView = std::remove_cvref_t<decltype(*tagged_view)>;
    static_assert(saf::extract::is_tagged_v<TaggedView>);
    static_assert(std::is_same_v<
        saf::extract::tagged_tag_t<TaggedView>,
        saf::source::FederatedPeer<OrgPeer>>);

    const auto& view = tagged_view->value();
    assert(view.payload.size() == body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        assert(view.payload[i] == body[i]);
    }

    const auto expected_key =
        fed::federation_key<&f_payload, RowIO, int>();
    assert(view.header.content_hash == expected_key.content_hash);
    assert(view.header.row_hash == expected_key.row_hash);

    return 0;
}

}  // namespace

int main() {
    if (int rc = test_admittance_success(); rc != 0) return rc;
    if (int rc = test_admittance_rejections(); rc != 0) return 100 + rc;
    if (int rc = test_permissioned_deserialize_tags_payload(); rc != 0) {
        return 200 + rc;
    }

    std::puts("federation_permission: typed admittance + tagged decode OK");
    return 0;
}
