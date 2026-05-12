#include <crucible/cntp/KtlsOffload.h>

namespace crucible::cntp {

std::string_view ktls_error_name(KtlsError error) noexcept {
    switch (error) {
        case KtlsError::EmptyKey:               return "EmptyKey";
        case KtlsError::InvalidKeySize:         return "InvalidKeySize";
        case KtlsError::EmptyIv:                return "EmptyIv";
        case KtlsError::IvTooLarge:             return "IvTooLarge";
        case KtlsError::SaltTooLarge:           return "SaltTooLarge";
        case KtlsError::RecSeqTooLarge:         return "RecSeqTooLarge";
        case KtlsError::UnsupportedTlsVersion:  return "UnsupportedTlsVersion";
        case KtlsError::UnsupportedCipherSuite: return "UnsupportedCipherSuite";
        case KtlsError::InvalidDirection:       return "InvalidDirection";
        case KtlsError::KernelInstallDeferred:  return "KernelInstallDeferred";
        case KtlsError::KernelTlsUnavailable:   return "KernelTlsUnavailable";
        default:                                return "<unknown KtlsError>";
    }
}

std::string_view
tls_offload_direction_name(TlsOffloadDirection direction) noexcept {
    switch (direction) {
        case TlsOffloadDirection::Tx:   return "tx";
        case TlsOffloadDirection::Rx:   return "rx";
        case TlsOffloadDirection::Both: return "both";
        default:                        return "unknown";
    }
}

std::expected<void, KtlsError>
enable_ktls_offload(KtlsOffloadSocket& socket,
                    DeclaredKtlsOffload const& request) noexcept {
    auto valid = validate_ktls_offload(request);
    if (!valid.has_value()) {
        return std::unexpected(valid.error());
    }

    auto const& raw = request.value();
    if (socket.socket().value() != raw.socket.value() ||
        socket.interface().view() != raw.interface.view()) {
        return std::unexpected(KtlsError::KernelTlsUnavailable);
    }
    if (!raw.allow_kernel_install) {
        return std::unexpected(KtlsError::KernelInstallDeferred);
    }

    return std::unexpected(KtlsError::KernelTlsUnavailable);
}

}  // namespace crucible::cntp
