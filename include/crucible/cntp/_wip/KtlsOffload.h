#pragma once

// GAPS-146 WIP. CNT-P kernel TLS offload sketch.
//
// This header owns typed admission for TLS 1.3 record keys that may be
// handed to Linux kTLS / NIC TLS offload.  It deliberately does not
// synthesize a fake kernel install: without the real socket/TLS backend,
// target-capability proof, and vendor policy, runtime enablement reports
// a deferred or unavailable result after validating the request shape.

#include <crucible/cntp/MtlsTransport.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp::_wip {

using ::crucible::cntp::MtlsCipherSuite;
using ::crucible::cntp::NicInterfaceName;
using ::crucible::cntp::SocketFd;
using ::crucible::cntp::TlsVersion;
using ::crucible::cntp::admit_socket_fd;

namespace wip_source {
struct KtlsOffloaded {};
}  // namespace wip_source

enum class KtlsError : std::uint8_t {
    EmptyKey,
    InvalidKeySize,
    EmptyIv,
    IvTooLarge,
    SaltTooLarge,
    RecSeqTooLarge,
    UnsupportedTlsVersion,
    UnsupportedCipherSuite,
    InvalidDirection,
    KernelInstallDeferred,
    KernelTlsUnavailable,
};

enum class TlsOffloadDirection : std::uint8_t {
    Tx = 1u,
    Rx = 2u,
    Both = 3u,
};

[[nodiscard]] std::string_view ktls_error_name(KtlsError error) noexcept;
[[nodiscard]] std::string_view
tls_offload_direction_name(TlsOffloadDirection direction) noexcept;

template <TlsVersion Version>
concept SupportedKtlsVersion = Version == TlsVersion::V13;

template <MtlsCipherSuite Suite>
concept KtlsAesGcmCipherSuite =
    Suite == MtlsCipherSuite::TlsAes128GcmSha256 ||
    Suite == MtlsCipherSuite::TlsAes256GcmSha384;

template <MtlsCipherSuite Suite, std::size_t KeyBytes>
concept KtlsKeySizeForCipher =
    (Suite == MtlsCipherSuite::TlsAes128GcmSha256 && KeyBytes == 16u) ||
    (Suite == MtlsCipherSuite::TlsAes256GcmSha384 && KeyBytes == 32u);

struct KtlsCryptoMaterial {
    static constexpr std::size_t max_key_bytes = 32;
    static constexpr std::size_t max_iv_bytes = 16;
    static constexpr std::size_t max_salt_bytes = 4;
    static constexpr std::size_t max_record_sequence_bytes = 8;

    std::array<std::byte, max_key_bytes> key{};
    std::array<std::byte, max_iv_bytes> iv{};
    std::array<std::byte, max_salt_bytes> salt{};
    std::array<std::byte, max_record_sequence_bytes> record_sequence{};
    std::uint8_t key_bytes = 0;
    std::uint8_t iv_bytes = 0;
    std::uint8_t salt_bytes = 0;
    std::uint8_t record_sequence_bytes = 0;

    constexpr KtlsCryptoMaterial() noexcept = default;
    KtlsCryptoMaterial(KtlsCryptoMaterial const&) = delete;
    KtlsCryptoMaterial& operator=(KtlsCryptoMaterial const&) = delete;

    constexpr KtlsCryptoMaterial(KtlsCryptoMaterial&& other) noexcept
        : key{other.key},
          iv{other.iv},
          salt{other.salt},
          record_sequence{other.record_sequence},
          key_bytes{other.key_bytes},
          iv_bytes{other.iv_bytes},
          salt_bytes{other.salt_bytes},
          record_sequence_bytes{other.record_sequence_bytes} {
        other.zeroize();
    }

    constexpr KtlsCryptoMaterial& operator=(KtlsCryptoMaterial&& other) noexcept {
        if (this != &other) {
            zeroize();
            key = other.key;
            iv = other.iv;
            salt = other.salt;
            record_sequence = other.record_sequence;
            key_bytes = other.key_bytes;
            iv_bytes = other.iv_bytes;
            salt_bytes = other.salt_bytes;
            record_sequence_bytes = other.record_sequence_bytes;
            other.zeroize();
        }
        return *this;
    }

    constexpr ~KtlsCryptoMaterial() noexcept { zeroize(); }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return key_bytes;
    }

    constexpr void zeroize() noexcept {
        for (auto& b : key) { b = std::byte{0}; }
        for (auto& b : iv) { b = std::byte{0}; }
        for (auto& b : salt) { b = std::byte{0}; }
        for (auto& b : record_sequence) { b = std::byte{0}; }
        key_bytes = 0;
        iv_bytes = 0;
        salt_bytes = 0;
        record_sequence_bytes = 0;
    }
};

using KtlsSecretMaterial = safety::Secret<KtlsCryptoMaterial>;

struct KtlsCryptoShape {
    std::uint8_t key_bytes = 0;
    std::uint8_t iv_bytes = 0;
    std::uint8_t salt_bytes = 0;
    std::uint8_t record_sequence_bytes = 0;
};

struct TlsCryptoInfo {
    MtlsCipherSuite cipher = MtlsCipherSuite::TlsAes256GcmSha384;
    KtlsCryptoShape shape{};
    KtlsSecretMaterial material;
};

using DeclaredTlsCryptoInfo =
    safety::Tagged<TlsCryptoInfo, wip_source::KtlsOffloaded>;

struct KtlsOffloadRequest {
    SocketFd socket;
    NicInterfaceName interface{};
    TlsOffloadDirection direction = TlsOffloadDirection::Tx;
    DeclaredTlsCryptoInfo crypto;
    bool allow_kernel_install = false;
};

using DeclaredKtlsOffload =
    safety::Tagged<KtlsOffloadRequest, wip_source::KtlsOffloaded>;

class KtlsOffloadSocket : public safety::Pinned<KtlsOffloadSocket> {
public:
    [[nodiscard]] SocketFd socket() const noexcept { return socket_; }
    [[nodiscard]] NicInterfaceName interface() const noexcept { return interface_; }
    [[nodiscard]] bool tx_active() const noexcept { return tx_active_; }
    [[nodiscard]] bool rx_active() const noexcept { return rx_active_; }
    [[nodiscard]] bool is_offload_active() const noexcept {
        return tx_active_ || rx_active_;
    }

private:
    SocketFd socket_;
    NicInterfaceName interface_{};
    bool tx_active_ = false;
    bool rx_active_ = false;

    constexpr KtlsOffloadSocket(SocketFd socket,
                                NicInterfaceName interface) noexcept
        : socket_{socket}, interface_{interface} {}

    friend constexpr KtlsOffloadSocket
    mint_ktls_socket(effects::Init, SocketFd, NicInterfaceName) noexcept;
};

[[nodiscard]] constexpr bool
ktls_direction_valid(TlsOffloadDirection direction) noexcept {
    return direction == TlsOffloadDirection::Tx ||
           direction == TlsOffloadDirection::Rx ||
           direction == TlsOffloadDirection::Both;
}

template <MtlsCipherSuite Suite>
[[nodiscard]] constexpr std::size_t ktls_key_bytes_for_cipher() noexcept {
    if constexpr (Suite == MtlsCipherSuite::TlsAes128GcmSha256) {
        return 16u;
    } else {
        static_assert(Suite == MtlsCipherSuite::TlsAes256GcmSha384);
        return 32u;
    }
}

[[nodiscard]] constexpr std::expected<KtlsCryptoMaterial, KtlsError>
admit_ktls_crypto_material(std::span<const std::byte> key,
                           std::span<const std::byte> iv,
                           std::span<const std::byte> salt,
                           std::span<const std::byte> record_sequence) noexcept {
    if (key.empty()) {
        return std::unexpected(KtlsError::EmptyKey);
    }
    if (key.size() > KtlsCryptoMaterial::max_key_bytes) {
        return std::unexpected(KtlsError::InvalidKeySize);
    }
    if (iv.empty()) {
        return std::unexpected(KtlsError::EmptyIv);
    }
    if (iv.size() > KtlsCryptoMaterial::max_iv_bytes) {
        return std::unexpected(KtlsError::IvTooLarge);
    }
    if (salt.size() > KtlsCryptoMaterial::max_salt_bytes) {
        return std::unexpected(KtlsError::SaltTooLarge);
    }
    if (record_sequence.size() > KtlsCryptoMaterial::max_record_sequence_bytes) {
        return std::unexpected(KtlsError::RecSeqTooLarge);
    }

    KtlsCryptoMaterial out{};
    for (std::size_t i = 0; i < key.size(); ++i) { out.key[i] = key[i]; }
    for (std::size_t i = 0; i < iv.size(); ++i) { out.iv[i] = iv[i]; }
    for (std::size_t i = 0; i < salt.size(); ++i) { out.salt[i] = salt[i]; }
    for (std::size_t i = 0; i < record_sequence.size(); ++i) {
        out.record_sequence[i] = record_sequence[i];
    }
    out.key_bytes = static_cast<std::uint8_t>(key.size());
    out.iv_bytes = static_cast<std::uint8_t>(iv.size());
    out.salt_bytes = static_cast<std::uint8_t>(salt.size());
    out.record_sequence_bytes =
        static_cast<std::uint8_t>(record_sequence.size());
    return out;
}

template <TlsVersion Version = TlsVersion::V13,
          MtlsCipherSuite Suite = MtlsCipherSuite::TlsAes256GcmSha384>
    requires SupportedKtlsVersion<Version> && KtlsAesGcmCipherSuite<Suite>
[[nodiscard]] constexpr std::expected<DeclaredTlsCryptoInfo, KtlsError>
mint_ktls_crypto_info(KtlsCryptoMaterial material) noexcept {
    if (material.key_bytes != ktls_key_bytes_for_cipher<Suite>()) {
        return std::unexpected(KtlsError::InvalidKeySize);
    }
    const KtlsCryptoShape shape{
        .key_bytes = material.key_bytes,
        .iv_bytes = material.iv_bytes,
        .salt_bytes = material.salt_bytes,
        .record_sequence_bytes = material.record_sequence_bytes,
    };
    return DeclaredTlsCryptoInfo{TlsCryptoInfo{
        .cipher = Suite,
        .shape = shape,
        .material = KtlsSecretMaterial{std::move(material)},
    }};
}

[[nodiscard]] constexpr KtlsOffloadSocket
mint_ktls_socket(effects::Init, SocketFd socket, NicInterfaceName iface) noexcept {
    return KtlsOffloadSocket{socket, iface};
}

[[nodiscard]] constexpr std::expected<DeclaredKtlsOffload, KtlsError>
mint_ktls_offload_for_socket(effects::Init,
                             SocketFd socket,
                             NicInterfaceName iface,
                             DeclaredTlsCryptoInfo crypto,
                             TlsOffloadDirection direction,
                             bool allow_kernel_install = false) noexcept {
    if (!ktls_direction_valid(direction)) {
        return std::unexpected(KtlsError::InvalidDirection);
    }
    return DeclaredKtlsOffload{KtlsOffloadRequest{
        .socket = socket,
        .interface = iface,
        .direction = direction,
        .crypto = std::move(crypto),
        .allow_kernel_install = allow_kernel_install,
    }};
}

[[nodiscard]] constexpr std::expected<void, KtlsError>
validate_ktls_crypto_info(DeclaredTlsCryptoInfo const& crypto) noexcept {
    auto const& raw = crypto.value();
    if (raw.cipher != MtlsCipherSuite::TlsAes128GcmSha256 &&
        raw.cipher != MtlsCipherSuite::TlsAes256GcmSha384) {
        return std::unexpected(KtlsError::UnsupportedCipherSuite);
    }
    if ((raw.cipher == MtlsCipherSuite::TlsAes128GcmSha256 &&
         raw.shape.key_bytes != 16u) ||
        (raw.cipher == MtlsCipherSuite::TlsAes256GcmSha384 &&
         raw.shape.key_bytes != 32u)) {
        return std::unexpected(KtlsError::InvalidKeySize);
    }
    if (raw.shape.iv_bytes == 0u) {
        return std::unexpected(KtlsError::EmptyIv);
    }
    if (raw.shape.iv_bytes > KtlsCryptoMaterial::max_iv_bytes) {
        return std::unexpected(KtlsError::IvTooLarge);
    }
    if (raw.shape.salt_bytes > KtlsCryptoMaterial::max_salt_bytes) {
        return std::unexpected(KtlsError::SaltTooLarge);
    }
    if (raw.shape.record_sequence_bytes >
        KtlsCryptoMaterial::max_record_sequence_bytes) {
        return std::unexpected(KtlsError::RecSeqTooLarge);
    }
    return {};
}

[[nodiscard]] constexpr std::expected<void, KtlsError>
validate_ktls_offload(DeclaredKtlsOffload const& request) noexcept {
    auto const& raw = request.value();
    if (!ktls_direction_valid(raw.direction)) {
        return std::unexpected(KtlsError::InvalidDirection);
    }
    return validate_ktls_crypto_info(raw.crypto);
}

[[nodiscard]] std::expected<void, KtlsError>
enable_ktls_offload(KtlsOffloadSocket& socket,
                    DeclaredKtlsOffload const& request) noexcept;

static_assert(sizeof(KtlsSecretMaterial) == sizeof(KtlsCryptoMaterial));
static_assert(std::is_trivially_copyable_v<KtlsCryptoShape>);
static_assert(sizeof(DeclaredTlsCryptoInfo) == sizeof(TlsCryptoInfo));
static_assert(sizeof(DeclaredKtlsOffload) == sizeof(KtlsOffloadRequest));
static_assert(!std::copy_constructible<KtlsCryptoMaterial>);
static_assert(!std::copy_constructible<TlsCryptoInfo>);
static_assert(!std::copy_constructible<KtlsOffloadRequest>);
static_assert(!std::move_constructible<KtlsOffloadSocket>);
static_assert(SupportedKtlsVersion<TlsVersion::V13>);
static_assert(!SupportedKtlsVersion<TlsVersion::V12>);
static_assert(KtlsAesGcmCipherSuite<MtlsCipherSuite::TlsAes256GcmSha384>);
static_assert(!KtlsAesGcmCipherSuite<MtlsCipherSuite::TlsChacha20Poly1305Sha256>);
static_assert(KtlsKeySizeForCipher<MtlsCipherSuite::TlsAes128GcmSha256, 16>);
static_assert(!KtlsKeySizeForCipher<MtlsCipherSuite::TlsAes128GcmSha256, 32>);

}  // namespace crucible::cntp::_wip
