#pragma once

// A partial order, not a chain.  A kernel built for one vendor does not run on
// another, so no two named vendors are ordered against each other.  The order
// runs the other way from the storage: a leq that holds reads as a consumer
// asking for the lower claim being served by a provider meeting the higher one,
// so Portable at the top satisfies every consumer and None at the bottom
// satisfies none.
//
// None is synthetic.  It exists so that the meet of two different vendors has
// somewhere to land.  Without it the structure is a meet-semilattice only, and
// no longer a bounded lattice.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

// The underlying values are storage, not order.  Two named vendors stay
// unordered however their ordinals compare.
enum class VendorBackend : std::uint8_t {
    None = 0,  // no kernel at all
    CPU = 1,  // x86_64 or aarch64 host
    NV = 2,  // NVIDIA GPU
    AMD = 3,  // AMD GPU
    TPU = 4,  // Google TPU
    TRN = 5,  // AWS Trainium
    CER = 6,  // Cerebras
    Portable = 255,  // one kernel that runs on every backend
};

struct VendorLattice {
    using element_type = VendorBackend;

    // A backend that runs in more places is the stronger claim.
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return VendorBackend::None; }
    [[nodiscard]] static constexpr element_type top() noexcept { return VendorBackend::Portable; }

    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept {
        if (a == b) return true;
        if (a == VendorBackend::None) return true;
        if (b == VendorBackend::Portable) return true;
        return false;
    }

    // Two different named vendors have nothing between them, so their least
    // upper bound can only be the top and their greatest lower bound only the
    // bottom.
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == VendorBackend::None) return b;
        if (b == VendorBackend::None) return a;
        return VendorBackend::Portable;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == VendorBackend::Portable) return b;
        if (b == VendorBackend::Portable) return a;
        return VendorBackend::None;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "VendorLattice"; }

    template <VendorBackend B>
    struct At : PinnedAt<VendorLattice, B> {
        static constexpr VendorBackend backend = B;
    };
};

namespace vendor_backend {
using NoneVendor = VendorLattice::At<VendorBackend::None>;
using CpuVendor = VendorLattice::At<VendorBackend::CPU>;
using NvVendor = VendorLattice::At<VendorBackend::NV>;
using AmdVendor = VendorLattice::At<VendorBackend::AMD>;
using TpuVendor = VendorLattice::At<VendorBackend::TPU>;
using TrnVendor = VendorLattice::At<VendorBackend::TRN>;
using CerVendor = VendorLattice::At<VendorBackend::CER>;
using PortableVendor = VendorLattice::At<VendorBackend::Portable>;
}  // namespace vendor_backend

}  // namespace foundation::algebra::lattices
