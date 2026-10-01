#pragma once

// The six members every graded wrapper publishes, written once.
//
// A wrapper over foundation/algebra/Graded.h has to answer six
// questions the same way every time: what it wraps, what lattice grades
// it, which modality that grading is, what the substrate instantiation
// is, and the two names the diagnostic surface reads off that
// instantiation.  This base writes the six one time for every wrapper.
//
// GradedWrapper in foundation/algebra/GradedTrait.h compares each
// forwarder's string against the substrate's precisely because a
// hand-written forwarder can return something the wrapper does not
// have.  One base answers from graded_type in every case, so that class
// of drift has no place to start.
//
// The base is empty, so a wrapper that inherits it keeps its size: the
// grade lives in the wrapper's own graded_type member, not here.  It is
// not a CRTP base, because nothing here needs the derived type — every
// member is static or a typedef.
//
// A wrapper whose value_type is not the substrate's says so by
// declaring its own, which hides the base's, and by declaring the
// member `static constexpr bool value_type_decoupled = true;`, which
// is what GradedWrapper in GradedTrait.h reads to admit the mismatch.
// AppendOnly is that case: it wraps a container and grades the
// container, while its value_type is the element.

#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <string_view>
#include <type_traits>

namespace fixy {

template <::foundation::algebra::ModalityKind M, typename Lattice, typename T>
struct graded_facade {
    using value_type = T;
    using lattice_type = Lattice;
    static constexpr ::foundation::algebra::ModalityKind modality = M;
    using graded_type = ::foundation::algebra::Graded<M, Lattice, T>;

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

}  // namespace fixy
