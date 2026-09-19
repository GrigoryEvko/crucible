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

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

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

inline constexpr std::size_t vendor_backend_count = ::foundation::reflect::enum_count<VendorBackend>;

// The identifier of b, or "<unknown VendorBackend>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view vendor_backend_name(VendorBackend b) noexcept {
    return ::foundation::reflect::enum_name(b);
}

struct VendorLattice {
    using element_type = VendorBackend;

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
        if (a == VendorBackend::Portable || b == VendorBackend::Portable) {
            return VendorBackend::Portable;
        }
        return VendorBackend::Portable;
    }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        if (a == b) return a;
        if (a == VendorBackend::Portable) return b;
        if (b == VendorBackend::Portable) return a;
        if (a == VendorBackend::None || b == VendorBackend::None) {
            return VendorBackend::None;
        }
        return VendorBackend::None;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "VendorLattice"; }

    template <VendorBackend B>
    struct AtElement : PinnedElement<B> {
        using vendor_backend_value_type = VendorBackend;
    };

    template <VendorBackend B>
    struct At : PinnedAt<VendorLattice, B, AtElement<B>> {
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

namespace detail::vendor_lattice_self_test {

static_assert(vendor_backend_count == 8, "The VendorBackend catalog changed size.  Confirm the intent, then "
                                         "update leq, join and meet, which special-case None and Portable, "
                                         "and the admission gates of every backend dispatcher.");

static_assert(Lattice<VendorLattice>);
static_assert(BoundedLattice<VendorLattice>);
static_assert(!UnboundedLattice<VendorLattice>);
static_assert(!Semiring<VendorLattice>);

static_assert(VendorLattice::bottom() == VendorBackend::None);
static_assert(VendorLattice::top() == VendorBackend::Portable);

// The partial-order axioms at every triple, and leq, join and meet in
// agreement at every pair, walked by reflection over the enumerators.
static_assert(verify_enum_lattice_exhaustive<VendorLattice>(),
              "The partial-order axioms must hold at every triple of the eight "
              "backends.  A failure means leq, join or meet is wrong for some pair, "
              "or the special-case routing for None, Portable, or two distinct named "
              "vendors is wrong.");

// These are the assertions a chain would break.  Ordering the named vendors
// against one another, in either direction, would silently admit a kernel built
// for one where the other is required, which is the mistake this order exists
// to reject.  Every pair of distinct named vendors is walked.
[[nodiscard]] consteval bool named_vendors_stay_incomparable() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^VendorBackend));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto ea : enumerators) {
        template for (constexpr auto eb : enumerators) {
            constexpr VendorBackend a = [:ea:];
            constexpr VendorBackend b = [:eb:];
            constexpr bool a_named = a != VendorBackend::None && a != VendorBackend::Portable;
            constexpr bool b_named = b != VendorBackend::None && b != VendorBackend::Portable;
            if constexpr (a_named && b_named && a != b) {
                if (VendorLattice::leq(a, b)) return false;
                if (VendorLattice::join(a, b) != VendorBackend::Portable) return false;
                if (VendorLattice::meet(a, b) != VendorBackend::None) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(named_vendors_stay_incomparable(), "Two distinct named vendors must stay incomparable, with Portable as "
                                                 "their join and None as their meet.");

static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::None));
static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::NV) == VendorBackend::NV);
static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::NV) == VendorBackend::NV);

// A bounded order with a single top and bottom and with unordered middle
// elements cannot be distributive, so the failure below is structural rather
// than a defect.  It is pinned so that anyone flattening this into a chain has
// to confront the assertion first.
[[nodiscard]] consteval bool non_distributive_witness() noexcept {
    using L = VendorLattice;
    auto lhs = L::meet(L::join(VendorBackend::NV, VendorBackend::AMD), VendorBackend::TPU);
    auto rhs = L::join(L::meet(VendorBackend::NV, VendorBackend::TPU), L::meet(VendorBackend::AMD, VendorBackend::TPU));
    return lhs == VendorBackend::TPU && rhs == VendorBackend::None && lhs != rhs;
}
static_assert(non_distributive_witness(), "VendorLattice must stay non-distributive.  A failure means either the "
                                          "order was flattened into a chain, which destroys the incomparability "
                                          "of distinct vendors, or an intermediate element was added that closed "
                                          "the distributivity gap.  Audit before resolving.");

// The shape of every At<backend>, walked by reflection.
static_assert(verify_pinned_at<VendorLattice>(), "VendorLattice::At<B>: a pinned grade lost its emptiness, its "
                                                 "conversion back to B, or its reflected name.");

static_assert(VendorLattice::name() == "VendorLattice");
static_assert(vendor_backend::NoneVendor::name() == "VendorLattice::At<None>");
static_assert(vendor_backend::PortableVendor::name() == "VendorLattice::At<Portable>");
static_assert(VendorLattice::At<static_cast<VendorBackend>(7)>::name() == "VendorLattice::At<?>");

static_assert(vendor_backend_name(VendorBackend::TRN) == "TRN");
static_assert(vendor_backend_name(static_cast<VendorBackend>(7)) == "<unknown VendorBackend>");

static_assert(vendor_backend::NoneVendor::backend == VendorBackend::None);
static_assert(vendor_backend::PortableVendor::backend == VendorBackend::Portable);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using PortableGraded = Graded<ModalityKind::Absolute, vendor_backend::PortableVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PortableGraded, double);

template <typename T_>
using NvGraded = Graded<ModalityKind::Absolute, vendor_backend::NvVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NvGraded, EightByteValue);

template <typename T_>
using AmdGraded = Graded<ModalityKind::Absolute, vendor_backend::AmdVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AmdGraded, EightByteValue);

template <typename T_>
using NoneGraded = Graded<ModalityKind::Absolute, vendor_backend::NoneVendor, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NoneGraded, EightByteValue);

inline void runtime_smoke_test() {
    VendorBackend a = VendorBackend::NV;
    VendorBackend b = VendorBackend::AMD;
    [[maybe_unused]] bool l1 = VendorLattice::leq(a, b);
    [[maybe_unused]] VendorBackend j1 = VendorLattice::join(a, b);
    [[maybe_unused]] VendorBackend m1 = VendorLattice::meet(a, b);
    [[maybe_unused]] VendorBackend bot = VendorLattice::bottom();
    [[maybe_unused]] VendorBackend topv = VendorLattice::top();

    VendorBackend portable = VendorBackend::Portable;
    [[maybe_unused]] VendorBackend j2 = VendorLattice::join(portable, a);
    [[maybe_unused]] VendorBackend m2 = VendorLattice::meet(portable, b);

    OneByteValue v{42};
    PortableGraded<OneByteValue> initial{v, vendor_backend::PortableVendor::bottom()};
    auto widened = initial.weaken(vendor_backend::PortableVendor::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(vendor_backend::PortableVendor::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    vendor_backend::PortableVendor::element_type e{};
    [[maybe_unused]] VendorBackend rec = e;
}

}  // namespace detail::vendor_lattice_self_test

}  // namespace foundation::algebra::lattices
