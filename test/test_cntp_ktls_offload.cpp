#include <crucible/cntp/_wip/KtlsOffload.h>
#include <fixy/Ctx.h>

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

// These tests reach enable_ktls_offload, which is
// [[deprecated("CRUCIBLE_STUB:...")]] until the live install path ships.
// This is the authorised suppression of that warning.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static_assert(crucible::cntp::_wip::kernel_install_implemented == false,
              "kernel_install_implemented flipped to true. Rewrite the live-tier test for this surface and "
              "remove the deprecation from enable_ktls_offload in lockstep.");

namespace cntp = crucible::cntp::_wip;
namespace fe = ::foundation::effects;

namespace {

[[nodiscard]] std::array<std::byte, 32> bytes32(std::byte seed) {
    std::array<std::byte, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(std::to_integer<unsigned>(seed) ^ static_cast<unsigned>(i * 17u));
    }
    return out;
}

void test_names_and_material_admission() {
    assert(cntp::ktls_error_name(cntp::KtlsError::KernelInstallDeferred) == std::string_view{"KernelInstallDeferred"});
    assert(cntp::tls_offload_direction_name(cntp::TlsOffloadDirection::Both) == std::string_view{"both"});

    auto key = bytes32(std::byte{0x10});
    auto iv = bytes32(std::byte{0x20});
    auto salt = bytes32(std::byte{0x30});
    auto seq = bytes32(std::byte{0x40});
    auto material = cntp::admit_ktls_crypto_material(std::span{key}.first<32>(), std::span{iv}.first<12>(),
                                                     std::span{salt}.first<4>(), std::span{seq}.first<8>());
    assert(material.has_value());
    assert(material->key_bytes == 32);
    assert(material->iv_bytes == 12);
    assert(material->salt_bytes == 4);
    assert(material->record_sequence_bytes == 8);

    std::array<std::byte, 0> empty{};
    auto no_key = cntp::admit_ktls_crypto_material(empty, std::span{iv}.first<12>(), {}, {});
    assert(!no_key.has_value());
    assert(no_key.error() == cntp::KtlsError::EmptyKey);

    auto no_iv = cntp::admit_ktls_crypto_material(std::span{key}.first<32>(), empty, {}, {});
    assert(!no_iv.has_value());
    assert(no_iv.error() == cntp::KtlsError::EmptyIv);

    std::printf("  test_names_and_material_admission: PASSED\n");
}

void test_crypto_mint_and_validation() {
    auto key = bytes32(std::byte{0x51});
    auto iv = bytes32(std::byte{0x52});
    auto material = cntp::admit_ktls_crypto_material(std::span{key}.first<32>(), std::span{iv}.first<12>(), {}, {});
    assert(material.has_value());

    auto crypto = cntp::mint_ktls_crypto_info<cntp::TlsVersion::V13, cntp::MtlsCipherSuite::TlsAes256GcmSha384>(
        std::move(*material));
    assert(crypto.has_value());
    static_assert(std::same_as<std::remove_cvref_t<decltype(*crypto)>, cntp::DeclaredTlsCryptoInfo>);
    static_assert(std::same_as<cntp::DeclaredTlsCryptoInfo::tag_type, crucible::cntp::_wip::wip_source::KtlsOffloaded>);
    assert(cntp::validate_ktls_crypto_info(*crypto).has_value());
    assert(crypto->value().shape.key_bytes == 32);
    assert(crypto->value().shape.iv_bytes == 12);

    auto short_key = cntp::admit_ktls_crypto_material(std::span{key}.first<16>(), std::span{iv}.first<12>(), {}, {});
    assert(short_key.has_value());
    auto wrong_size = cntp::mint_ktls_crypto_info<cntp::TlsVersion::V13, cntp::MtlsCipherSuite::TlsAes256GcmSha384>(
        std::move(*short_key));
    assert(!wrong_size.has_value());
    assert(wrong_size.error() == cntp::KtlsError::InvalidKeySize);

    // The test builds a declared value through mint_tagged, not through
    // mint_ktls_crypto_info.  The check reads the cipher again and refuses it.
    cntp::TlsCryptoInfo forged_chacha{
        .cipher = cntp::MtlsCipherSuite::TlsChacha20Poly1305Sha256,
        .shape =
            cntp::KtlsCryptoShape{
                .key_bytes = 32,
                .iv_bytes = 12,
                .salt_bytes = 0,
                .record_sequence_bytes = 0,
            },
        .material = ::fixy::mint_secret<cntp::KtlsCryptoMaterial>(),
    };
    auto tagged_forged_chacha = ::fixy::mint_tagged<cntp::wip_source::KtlsOffloaded>(std::move(forged_chacha));
    auto rejected_chacha = cntp::validate_ktls_crypto_info(tagged_forged_chacha);
    assert(!rejected_chacha.has_value());
    assert(rejected_chacha.error() == cntp::KtlsError::UnsupportedCipherSuite);

    std::printf("  test_crypto_mint_and_validation: PASSED\n");
}

void test_socket_request_and_deferred_enable() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
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
    auto material = cntp::admit_ktls_crypto_material(std::span{key}.first<32>(), std::span{iv}.first<12>(), {}, {});
    assert(material.has_value());
    auto crypto = cntp::mint_ktls_crypto_info(std::move(*material));
    assert(crypto.has_value());

    auto request =
        cntp::mint_ktls_offload_for_socket(init, *fd, *iface, std::move(*crypto), cntp::TlsOffloadDirection::Both);
    assert(request.has_value());
    assert(cntp::validate_ktls_offload(*request).has_value());

    auto enabled = cntp::enable_ktls_offload(socket, *request);
    assert(!enabled.has_value());
    assert(enabled.error() == cntp::KtlsError::KernelInstallDeferred);

    auto other_fd = cntp::admit_socket_fd(8);
    assert(other_fd.has_value());
    auto other_material =
        cntp::admit_ktls_crypto_material(std::span{key}.first<32>(), std::span{iv}.first<12>(), {}, {});
    assert(other_material.has_value());
    auto other_crypto = cntp::mint_ktls_crypto_info(std::move(*other_material));
    assert(other_crypto.has_value());
    auto other_request = cntp::mint_ktls_offload_for_socket(init, *other_fd, *iface, std::move(*other_crypto),
                                                            cntp::TlsOffloadDirection::Tx, true);
    assert(other_request.has_value());
    auto wrong_socket = cntp::enable_ktls_offload(socket, *other_request);
    assert(!wrong_socket.has_value());
    assert(wrong_socket.error() == cntp::KtlsError::KernelTlsUnavailable);

    auto bad_direction =
        cntp::admit_ktls_crypto_material(std::span{key}.first<32>(), std::span{iv}.first<12>(), {}, {});
    assert(bad_direction.has_value());
    auto bad_direction_crypto = cntp::mint_ktls_crypto_info(std::move(*bad_direction));
    assert(bad_direction_crypto.has_value());
    auto refused = cntp::mint_ktls_offload_for_socket(init, *fd, *iface, std::move(*bad_direction_crypto),
                                                      static_cast<cntp::TlsOffloadDirection>(0));
    assert(!refused.has_value());
    assert(refused.error() == cntp::KtlsError::InvalidDirection);

    std::printf("  test_socket_request_and_deferred_enable: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::KtlsSecretMaterial) == sizeof(cntp::KtlsCryptoMaterial));
    static_assert(sizeof(cntp::DeclaredTlsCryptoInfo) == sizeof(cntp::TlsCryptoInfo));
    static_assert(std::is_trivially_copyable_v<cntp::KtlsCryptoShape>);
    static_assert(!std::copy_constructible<cntp::KtlsCryptoMaterial>);
    static_assert(!std::copy_constructible<cntp::TlsCryptoInfo>);
    static_assert(!std::copy_constructible<cntp::KtlsOffloadRequest>);
    static_assert(!std::move_constructible<cntp::KtlsOffloadSocket>);
    static_assert(cntp::SupportedKtlsVersion<cntp::TlsVersion::V13>);
    static_assert(!cntp::SupportedKtlsVersion<cntp::TlsVersion::V12>);
    static_assert(cntp::KtlsAesGcmCipherSuite<cntp::MtlsCipherSuite::TlsAes256GcmSha384>);
    static_assert(!cntp::KtlsAesGcmCipherSuite<cntp::MtlsCipherSuite::TlsChacha20Poly1305Sha256>);
    static_assert(cntp::CtxFitsKtlsMint<::fixy::ColdInitCtx>);
    static_assert(!cntp::CtxFitsKtlsMint<::fixy::BgDrainCtx>);
    static_assert(!cntp::CtxFitsKtlsMint<::fixy::HotFgCtx>);
    static_assert(!std::is_default_constructible_v<cntp::DeclaredTlsCryptoInfo>);
    static_assert(!std::is_default_constructible_v<cntp::DeclaredKtlsOffload>);

    std::printf("test_cntp_ktls_offload:\n");
    test_names_and_material_admission();
    test_crypto_mint_and_validation();
    test_socket_request_and_deferred_enable();
    std::printf("test_cntp_ktls_offload: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
