#include <crucible/rt/Xdp.h>

namespace crucible::rt {

std::string_view xdp_action_name(XdpAction action) noexcept {
    switch (action) {
        case XdpAction::Aborted:  return "Aborted";
        case XdpAction::Drop:     return "Drop";
        case XdpAction::Pass:     return "Pass";
        case XdpAction::Tx:       return "Tx";
        case XdpAction::Redirect: return "Redirect";
        default:                  return "Unknown";
    }
}

std::string_view xdp_mode_name(XdpMode mode) noexcept {
    switch (mode) {
        case XdpMode::Generic: return "Generic";
        case XdpMode::Native:  return "Native";
        case XdpMode::Offload: return "Offload";
        default:               return "Unknown";
    }
}

std::string_view xdp_program_kind_name(XdpProgramKind kind) noexcept {
    switch (kind) {
        case XdpProgramKind::FlowFilter:      return "FlowFilter";
        case XdpProgramKind::AfXdpRedirect:   return "AfXdpRedirect";
        case XdpProgramKind::GossipMulticast: return "GossipMulticast";
        case XdpProgramKind::TcamAcl:         return "TcamAcl";
        default:                              return "Unknown";
    }
}

std::string_view bpf_map_kind_name(BpfMapKind kind) noexcept {
    switch (kind) {
        case BpfMapKind::Hash:        return "Hash";
        case BpfMapKind::LruHash:     return "LruHash";
        case BpfMapKind::Array:       return "Array";
        case BpfMapKind::PerCpuArray: return "PerCpuArray";
        case BpfMapKind::DevMap:      return "DevMap";
        case BpfMapKind::XskMap:      return "XskMap";
        default:                      return "Unknown";
    }
}

std::string_view xdp_error_name(XdpError error) noexcept {
    switch (error) {
        case XdpError::InvalidIfIndex:        return "InvalidIfIndex";
        case XdpError::InvalidMapEntries:     return "InvalidMapEntries";
        case XdpError::InvalidMapElementSize: return "InvalidMapElementSize";
        case XdpError::WrongCogKind:          return "WrongCogKind";
        case XdpError::MissingNativeXdp:      return "MissingNativeXdp";
        case XdpError::MissingOffloadXdp:     return "MissingOffloadXdp";
        case XdpError::MapFull:               return "MapFull";
        case XdpError::KeyNotFound:           return "KeyNotFound";
        case XdpError::KeyAlreadyExists:      return "KeyAlreadyExists";
        default:                              return "Unknown";
    }
}

}  // namespace crucible::rt
