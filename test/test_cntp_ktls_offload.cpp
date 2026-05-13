#include <crucible/cntp/_wip/KtlsOffload.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp::_wip;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] std::array<std::byte, 32> bytes32(std::byte seed) {
    std::array<std::byte, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(
            std::to_integer<unsigned>(seed) ^ static_cast<unsigned>(i * 17u));
    }
    return out;
}

void test_names_and_material_admission() {
    assert(cntp::ktls_error_name(cntp::KtlsError::KernelInstallDeferred) ==
           std::string_view{"KernelInstallDeferred"});
    assert(cntp::tls_offload_direction_name(cntp::TlsOffloadDirection::Both) ==
           std::string_view{"both"});

    auto key = bytes32(std::byte{0x10});
    auto iv = bytes32(std::byte{0x20});
    auto salt = bytes32(std::byte{0x30});
    auto seq = bytes32(std::byte{0x40});
    auto material = cntp::admit_ktls_crypto_material(
        std::span{key}.first<32>(),
        std::span{iv}.first<12>(),
        std::span{salt}.first<4>(),
        std::span{seq}.first<8>());
    assert(material.has_value());
    assert(material->key_bytes == 32);
    assert(material->iv_bytes == 12);
    assert(material->salt_bytes == 4);
    assert(material->record_sequence_bytes == 8);

    std::array<std::byte, 0> empty{};
    auto no_key = cntp::admit_ktls_crypto_material(
        empty, std::span{iv}.first<12>(), {}, {});
    assert(!no_key.has_value());
    assert(no_key.error() == cntp::KtlsError::EmptyKey);

    auto no_iv = cntp::admit_ktls_crypto_material(
        std::span{key}.first<32>(), empty, {}, {});
    assert(!no_iv.has_value());
    assert(no_iv.error() == cntp::KtlsError::EmptyIv);

    std::printf("  test_names_and_material_admission: PASSED\n");
}

void test_crypto_mint_and_validation() {
    auto key = bytes32(std::byte{0x51});
    auto iv = bytes32(std::byte{0x52});
    auto material = cntp::admit_ktls_crypto_material(
        std::span{key}.first<32>(), std::span{iv}.first<12>(), {}, {});
    assert(material.has_value());

    auto crypto = cntp::mint_ktls_crypto_info<
        cntp::TlsVersion::V13,
        cntp::MtlsCipherSuite::TlsAes256GcmSha384>(std::move(*material));
    assert(crypto.has_value());
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(*crypto)>,
                  cntp::DeclaredTlsCryptoInfo>);
    static_assert(std::same_as<
                  cntp::DeclaredTlsCryptoInfo::tag_type,
                  crucible::cntp::_wip::wip_source::KtlsOffloaded>);
    assert(cntp::validate_ktls_crypto_info(*crypto).has_value());
    assert(crypto->value().shape.key_bytes == 32);
    assert(crypto->value().shape.iv_bytes == 12);

    auto short_key = cntp::admit_ktls_crypto_material(
        std::span{key}.first<16>(), std::span{iv}.first<12>(), {}, {});
    assert(short_key.has_value());
    auto wrong_size = cntp::mint_ktls_crypto_info<
        cntp::TlsVersion::V13,
        cntp::MtlsCipherSuite::TlsAes256GcmSha384>(std::move(*short_key));
    assert(!wrong_size.has_value());
    assert(wrong_size.error() == cntp::KtlsError::InvalidKeySize);

    cntp::TlsCryptoInfo forged_chacha{
        .cipher = cntp::MtlsCipherSuite::TlsChacha20Poly1305Sha256,
        .shape = cntp::KtlsCryptoShape{
            .key_bytes = 32,
            .iv_bytes = 12,
            .salt_bytes = 0,
            .record_sequence_bytes = 0,
        },
        .material = cntp::KtlsSecretMaterial{cntp::KtlsCryptoMaterial{}},
    };
    cntp::DeclaredTlsCryptoInfo tagged_forged_chacha{
        std::move(forged_chacha)};
    auto rejected_chacha = cntp::validate_ktls_crypto_info(
        tagged_forged_chacha);
    assert(!rejected_chacha.has_value());
    assert(rejected_chacha.error() ==
           cntp::KtlsError::UnsupportedCipherSuite);

    std::printf("  test_crypto_mint_and_validation: PASSED\n");
}

void test_socket_request_and_deferred_enable() {
    eff::Init init{};
    auto fd = cntp::admit_socket_fd(7);
    auto iface = cntp::NicInterfaceName::from("eth0");
    assert(fd.has_value());
    assert(iface.has_value());

    auto socket = cntp::mint_ktls_socket(init, *fd, *iface);
    assert(socket.socket().value() == 7);
    assert(socket.interface().view() == "eth0");
    assert(!socket.is_offload_active());

    auto key = bytes32(std::byte{0x61});
    auto iv = bytes32(std::byte{0x62});
    auto material = cntp::admit_ktls_crypto_material(
        std::span{key}.first<32>(), std::span{iv}.first<12>(), {}, {});
    assert(material.has_value());
    auto crypto = cntp::mint_ktls_crypto_info(std::move(*material));
    assert(crypto.has_value());

    auto request = cntp::mint_ktls_offload_for_socket(
        init,
        *fd,
        *iface,
        std::move(*crypto),
        cntp::TlsOffloadDirection::Both);
    assert(request.has_value());
    assert(cntp::validate_ktls_offload(*request).has_value());

    auto enabled = cntp::enable_ktls_offload(socket, *request);
    assert(!enabled.has_value());
    assert(enabled.error() == cntp::KtlsError::KernelInstallDeferred);

    std::printf("  test_socket_request_and_deferred_enable: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::KtlsSecretMaterial) ==
                  sizeof(cntp::KtlsCryptoMaterial));
    static_assert(sizeof(cntp::DeclaredTlsCryptoInfo) ==
                  sizeof(cntp::TlsCryptoInfo));
    static_assert(std::is_trivially_copyable_v<cntp::KtlsCryptoShape>);
    static_assert(!std::copy_constructible<cntp::KtlsCryptoMaterial>);
    static_assert(!std::copy_constructible<cntp::TlsCryptoInfo>);
    static_assert(!std::copy_constructible<cntp::KtlsOffloadRequest>);
    static_assert(!std::move_constructible<cntp::KtlsOffloadSocket>);
    static_assert(cntp::SupportedKtlsVersion<cntp::TlsVersion::V13>);
    static_assert(!cntp::SupportedKtlsVersion<cntp::TlsVersion::V12>);
    static_assert(cntp::KtlsAesGcmCipherSuite<
                  cntp::MtlsCipherSuite::TlsAes256GcmSha384>);
    static_assert(!cntp::KtlsAesGcmCipherSuite<
                  cntp::MtlsCipherSuite::TlsChacha20Poly1305Sha256>);

    std::printf("test_cntp_ktls_offload:\n");
    test_names_and_material_admission();
    test_crypto_mint_and_validation();
    test_socket_request_and_deferred_enable();
    std::printf("test_cntp_ktls_offload: all PASSED\n");
    return 0;
}
