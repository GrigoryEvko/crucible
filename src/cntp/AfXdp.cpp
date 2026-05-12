#include <crucible/cntp/AfXdp.h>

namespace crucible::cntp {

std::string_view af_xdp_mode_name(AfXdpMode mode) noexcept {
    switch (mode) {
        case AfXdpMode::Copy: return "copy";
        case AfXdpMode::ZeroCopy: return "zero_copy";
        default: return "unknown";
    }
}

std::string_view af_xdp_error_name(AfXdpError error) noexcept {
    switch (error) {
        case AfXdpError::InvalidInterfaceName: return "InvalidInterfaceName";
        case AfXdpError::InvalidIfIndex: return "InvalidIfIndex";
        case AfXdpError::InvalidQueueId: return "InvalidQueueId";
        case AfXdpError::InvalidFrameSize: return "InvalidFrameSize";
        case AfXdpError::InvalidFrameCount: return "InvalidFrameCount";
        case AfXdpError::InvalidRingSize: return "InvalidRingSize";
        case AfXdpError::InvalidUmemShape: return "InvalidUmemShape";
        case AfXdpError::PacketTooLarge: return "PacketTooLarge";
        case AfXdpError::InvalidFrameAddress: return "InvalidFrameAddress";
        case AfXdpError::TxRingFull: return "TxRingFull";
        case AfXdpError::RxRingEmpty: return "RxRingEmpty";
        case AfXdpError::CompletionRingEmpty: return "CompletionRingEmpty";
        case AfXdpError::XdpRedirectMissing: return "XdpRedirectMissing";
        default: return "Unknown";
    }
}

}  // namespace crucible::cntp
