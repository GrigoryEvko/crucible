#pragma once

// One single-element lattice per Source.
//
// A tagged value's source is its type parameter, so within one
// instantiation the grade cannot vary at run time.  The element type is
// therefore empty and every operation is identity.  The emptiness is
// what lets the grade collapse to nothing in the wrapped value.
//
// Two different sources give two different lattices, so nothing here
// relates them.  Whether a value from one source may stand in for
// another is a separate judgement, decided outside this type.  That
// separation is load-bearing: composition takes two values of the same
// lattice, so distinct source types make cross-source composition
// structurally impossible rather than merely discouraged.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>

#include <meta>
#include <string_view>
#include <type_traits>

namespace foundation::algebra::lattices {

template <typename Source>
struct TrustLattice {
    struct element_type {
        using source_type = Source;
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
    };

    using source_type = Source;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return std::meta::display_string_of(^^Source); }
};

namespace detail {

// The source tags and the carrier of the TrustLattice checks.  The check
// file of this header and test/foundation/test_lattices_core.cpp name
// them, so they live here and not in the check file.
namespace source {
struct FromUser {};
struct FromDb {};
struct FromConfig {};
struct FromInternal {};
struct External {};
struct Sanitized {};
}  // namespace source

template <typename T>
using TaggedSanitized = Graded<ModalityKind::RelativeMonad, TrustLattice<source::Sanitized>, T>;

}  // namespace detail

}  // namespace foundation::algebra::lattices
