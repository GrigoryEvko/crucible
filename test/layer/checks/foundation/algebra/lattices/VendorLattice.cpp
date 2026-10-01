// The compile-time checks of foundation/algebra/lattices/VendorLattice.h.

#include <foundation/algebra/lattices/VendorLattice.h>

namespace foundation::algebra::lattices {

namespace detail::vendor_lattice_self_test {

static_assert(::foundation::reflect::enum_count<VendorBackend> == 8,
              "The VendorBackend catalog changed size.  Confirm the intent, then update leq, join and meet, which "
              "special-case None and Portable, and the admission gates of every backend dispatcher.");

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

static_assert(vendor_backend::NoneVendor::backend == VendorBackend::None);
static_assert(vendor_backend::PortableVendor::backend == VendorBackend::Portable);

}  // namespace detail::vendor_lattice_self_test

}  // namespace foundation::algebra::lattices
