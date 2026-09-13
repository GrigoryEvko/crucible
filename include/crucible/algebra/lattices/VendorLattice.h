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

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

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

inline constexpr std::size_t vendor_backend_count = std::meta::enumerators_of(^^VendorBackend).size();

[[nodiscard]] consteval std::string_view vendor_backend_name(VendorBackend b) noexcept {
    switch (b) {
        case VendorBackend::None:
            return "None";
        case VendorBackend::CPU:
            return "CPU";
        case VendorBackend::NV:
            return "NV";
        case VendorBackend::AMD:
            return "AMD";
        case VendorBackend::TPU:
            return "TPU";
        case VendorBackend::TRN:
            return "TRN";
        case VendorBackend::CER:
            return "CER";
        case VendorBackend::Portable:
            return "Portable";
        default:
            return std::string_view{"<unknown VendorBackend>"};
    }
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
    struct At {
        struct element_type {
            using vendor_backend_value_type = VendorBackend;
            [[nodiscard]] constexpr operator vendor_backend_value_type() const noexcept { return B; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr VendorBackend backend = B;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (B) {
                case VendorBackend::None:
                    return "VendorLattice::At<None>";
                case VendorBackend::CPU:
                    return "VendorLattice::At<CPU>";
                case VendorBackend::NV:
                    return "VendorLattice::At<NV>";
                case VendorBackend::AMD:
                    return "VendorLattice::At<AMD>";
                case VendorBackend::TPU:
                    return "VendorLattice::At<TPU>";
                case VendorBackend::TRN:
                    return "VendorLattice::At<TRN>";
                case VendorBackend::CER:
                    return "VendorLattice::At<CER>";
                case VendorBackend::Portable:
                    return "VendorLattice::At<Portable>";
                default:
                    return "VendorLattice::At<?>";
            }
        }
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

[[nodiscard]] consteval bool every_vendor_backend_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^VendorBackend));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (vendor_backend_name([:en:]) == std::string_view{"<unknown VendorBackend>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_vendor_backend_has_name(), "vendor_backend_name() switch missing arm for at least one backend.");

static_assert(Lattice<VendorLattice>);
static_assert(BoundedLattice<VendorLattice>);
static_assert(Lattice<vendor_backend::NoneVendor>);
static_assert(Lattice<vendor_backend::NvVendor>);
static_assert(Lattice<vendor_backend::AmdVendor>);
static_assert(Lattice<vendor_backend::PortableVendor>);
static_assert(BoundedLattice<vendor_backend::PortableVendor>);

static_assert(!UnboundedLattice<VendorLattice>);
static_assert(!Semiring<VendorLattice>);

static_assert(std::is_empty_v<vendor_backend::NoneVendor::element_type>);
static_assert(std::is_empty_v<vendor_backend::NvVendor::element_type>);
static_assert(std::is_empty_v<vendor_backend::AmdVendor::element_type>);
static_assert(std::is_empty_v<vendor_backend::PortableVendor::element_type>);

static_assert(VendorLattice::bottom() == VendorBackend::None);
static_assert(VendorLattice::top() == VendorBackend::Portable);

static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::None));
static_assert(VendorLattice::leq(VendorBackend::CPU, VendorBackend::CPU));
static_assert(VendorLattice::leq(VendorBackend::NV, VendorBackend::NV));
static_assert(VendorLattice::leq(VendorBackend::AMD, VendorBackend::AMD));
static_assert(VendorLattice::leq(VendorBackend::TPU, VendorBackend::TPU));
static_assert(VendorLattice::leq(VendorBackend::TRN, VendorBackend::TRN));
static_assert(VendorLattice::leq(VendorBackend::CER, VendorBackend::CER));
static_assert(VendorLattice::leq(VendorBackend::Portable, VendorBackend::Portable));

static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::CPU));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::NV));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::AMD));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::TPU));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::TRN));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::CER));
static_assert(VendorLattice::leq(VendorBackend::None, VendorBackend::Portable));

static_assert(VendorLattice::leq(VendorBackend::CPU, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::NV, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::AMD, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::TPU, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::TRN, VendorBackend::Portable));
static_assert(VendorLattice::leq(VendorBackend::CER, VendorBackend::Portable));

// These are the assertions a chain would break.  Ordering the named vendors
// against one another, in either direction, would silently admit a kernel built
// for one where the other is required, which is the mistake this order exists
// to reject.
static_assert(!VendorLattice::leq(VendorBackend::NV, VendorBackend::AMD));
static_assert(!VendorLattice::leq(VendorBackend::AMD, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::NV, VendorBackend::TPU));
static_assert(!VendorLattice::leq(VendorBackend::TPU, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::CPU, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::NV, VendorBackend::CPU));
static_assert(!VendorLattice::leq(VendorBackend::TRN, VendorBackend::CER));
static_assert(!VendorLattice::leq(VendorBackend::CER, VendorBackend::TRN));
static_assert(!VendorLattice::leq(VendorBackend::AMD, VendorBackend::TPU));
static_assert(!VendorLattice::leq(VendorBackend::TPU, VendorBackend::AMD));

static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::CPU));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::NV));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::AMD));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::None));

static_assert(!VendorLattice::leq(VendorBackend::CPU, VendorBackend::None));
static_assert(!VendorLattice::leq(VendorBackend::NV, VendorBackend::None));
static_assert(!VendorLattice::leq(VendorBackend::AMD, VendorBackend::None));
static_assert(!VendorLattice::leq(VendorBackend::Portable, VendorBackend::None));

static_assert(VendorLattice::join(VendorBackend::NV, VendorBackend::AMD) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::NV, VendorBackend::AMD) == VendorBackend::None);
static_assert(VendorLattice::join(VendorBackend::TPU, VendorBackend::TRN) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::TPU, VendorBackend::TRN) == VendorBackend::None);
static_assert(VendorLattice::join(VendorBackend::CPU, VendorBackend::CER) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::CPU, VendorBackend::CER) == VendorBackend::None);

static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::CPU) == VendorBackend::CPU);
static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::NV) == VendorBackend::NV);
static_assert(VendorLattice::join(VendorBackend::None, VendorBackend::Portable) == VendorBackend::Portable);

static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::CPU) == VendorBackend::CPU);
static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::NV) == VendorBackend::NV);
static_assert(VendorLattice::meet(VendorBackend::Portable, VendorBackend::None) == VendorBackend::None);

static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::CPU) == VendorBackend::None);
static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::NV) == VendorBackend::None);
static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::Portable) == VendorBackend::None);

static_assert(VendorLattice::join(VendorBackend::Portable, VendorBackend::CPU) == VendorBackend::Portable);
static_assert(VendorLattice::join(VendorBackend::Portable, VendorBackend::None) == VendorBackend::Portable);

static_assert(VendorLattice::join(VendorBackend::NV, VendorBackend::NV) == VendorBackend::NV);
static_assert(VendorLattice::meet(VendorBackend::NV, VendorBackend::NV) == VendorBackend::NV);
static_assert(VendorLattice::join(VendorBackend::Portable, VendorBackend::Portable) == VendorBackend::Portable);
static_assert(VendorLattice::meet(VendorBackend::None, VendorBackend::None) == VendorBackend::None);

// The shared chain verifier does not apply to a partial order, so the axioms
// are walked by hand over every triple of the eight elements.
inline constexpr VendorBackend kAll[] = {
    VendorBackend::None, VendorBackend::CPU, VendorBackend::NV,  VendorBackend::AMD,
    VendorBackend::TPU,  VendorBackend::TRN, VendorBackend::CER, VendorBackend::Portable,
};

[[nodiscard]] consteval bool verify_partial_order_exhaustive() noexcept {
    using L = VendorLattice;
    for (auto a : kAll) {
        if (!L::leq(a, a)) return false;
        for (auto b : kAll) {
            if (L::leq(a, b) && L::leq(b, a) && a != b) return false;
            if (L::join(a, b) != L::join(b, a)) return false;
            if (L::meet(a, b) != L::meet(b, a)) return false;
            if (L::join(a, a) != a) return false;
            if (L::meet(a, a) != a) return false;
            if (L::join(a, L::meet(a, b)) != a) return false;
            if (L::meet(a, L::join(a, b)) != a) return false;
            if (!L::leq(L::bottom(), a)) return false;
            if (!L::leq(a, L::top())) return false;
            for (auto c : kAll) {
                if (L::leq(a, b) && L::leq(b, c) && !L::leq(a, c)) return false;
                if (L::join(L::join(a, b), c) != L::join(a, L::join(b, c))) return false;
                if (L::meet(L::meet(a, b), c) != L::meet(a, L::meet(b, c))) return false;
                bool by_meet = (L::meet(a, b) == a);
                bool by_join = (L::join(a, b) == b);
                bool by_leq = L::leq(a, b);
                if (by_leq != by_meet) return false;
                if (by_leq != by_join) return false;
            }
        }
    }
    return true;
}
static_assert(verify_partial_order_exhaustive(),
              "The partial-order axioms must hold at every triple of the eight "
              "backends.  A failure means leq, join or meet is wrong for some pair, "
              "or the special-case routing for None, Portable, or two distinct named "
              "vendors is wrong.");

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

static_assert(VendorLattice::name() == "VendorLattice");
static_assert(vendor_backend::NoneVendor::name() == "VendorLattice::At<None>");
static_assert(vendor_backend::CpuVendor::name() == "VendorLattice::At<CPU>");
static_assert(vendor_backend::NvVendor::name() == "VendorLattice::At<NV>");
static_assert(vendor_backend::AmdVendor::name() == "VendorLattice::At<AMD>");
static_assert(vendor_backend::TpuVendor::name() == "VendorLattice::At<TPU>");
static_assert(vendor_backend::TrnVendor::name() == "VendorLattice::At<TRN>");
static_assert(vendor_backend::CerVendor::name() == "VendorLattice::At<CER>");
static_assert(vendor_backend::PortableVendor::name() == "VendorLattice::At<Portable>");

[[nodiscard]] consteval bool every_at_vendor_backend_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^VendorBackend));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (VendorLattice::At<([:en:])>::name() == std::string_view{"VendorLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_vendor_backend_has_name(), "VendorLattice::At<B>::name() switch missing an arm.");

static_assert(vendor_backend::NoneVendor::backend == VendorBackend::None);
static_assert(vendor_backend::CpuVendor::backend == VendorBackend::CPU);
static_assert(vendor_backend::NvVendor::backend == VendorBackend::NV);
static_assert(vendor_backend::AmdVendor::backend == VendorBackend::AMD);
static_assert(vendor_backend::TpuVendor::backend == VendorBackend::TPU);
static_assert(vendor_backend::TrnVendor::backend == VendorBackend::TRN);
static_assert(vendor_backend::CerVendor::backend == VendorBackend::CER);
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

}  // namespace crucible::algebra::lattices
