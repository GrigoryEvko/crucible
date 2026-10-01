// The compile-time checks of foundation/algebra/ClaimOrientation.h.

#include <foundation/algebra/ClaimOrientation.h>

namespace foundation::algebra {

namespace detail::claim_orientation_self_test {

struct Silent {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};
struct Weaker : Silent {
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
};
struct Stronger : Silent {
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::stronger_is_higher;
};

// One element: every operation returns it.
struct Single {
    struct element_type {
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
    };
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
};

static_assert(claim_orientation_v<Silent> == ClaimOrientation::unstated);
static_assert(claim_orientation_v<Weaker> == ClaimOrientation::weaker_is_higher);
static_assert(claim_orientation_v<Stronger> == ClaimOrientation::stronger_is_higher);
static_assert(claim_orientation_v<Single> == ClaimOrientation::one_claim, "an empty element names one claim");

// A lattice that states one claim over an element with two values is
// unstated.  It cannot state its way past the gate.
struct ForgedSingle : Silent {
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::one_claim;
};
static_assert(claim_orientation_v<ForgedSingle> == ClaimOrientation::unstated);

static_assert(turned_over(ClaimOrientation::weaker_is_higher) == ClaimOrientation::stronger_is_higher);
static_assert(turned_over(ClaimOrientation::stronger_is_higher) == ClaimOrientation::weaker_is_higher);
static_assert(turned_over(ClaimOrientation::unstated) == ClaimOrientation::unstated);
static_assert(turned_over(ClaimOrientation::one_claim) == ClaimOrientation::one_claim);
static_assert(turned_over(turned_over(ClaimOrientation::stronger_is_higher)) == ClaimOrientation::stronger_is_higher);

static_assert(product_orientation<Weaker, Weaker>() == ClaimOrientation::weaker_is_higher);
static_assert(product_orientation<Weaker, Stronger>() == ClaimOrientation::stronger_is_higher);
static_assert(product_orientation<Stronger, Silent>() == ClaimOrientation::stronger_is_higher);
static_assert(product_orientation<Weaker, Silent>() == ClaimOrientation::unstated);
static_assert(product_orientation<Weaker, Single>() == ClaimOrientation::weaker_is_higher);
static_assert(product_orientation<Single, Single>() == ClaimOrientation::one_claim);
static_assert(product_orientation<>() == ClaimOrientation::one_claim);

// An unstated orientation is refused as a stored grade, as a stated
// stronger one is.  The two orientations that Graded accepts are the
// positive cases.
static_assert(GradableLattice<Weaker> && GradableLattice<Single>);
static_assert(!GradableLattice<Stronger> && !GradableLattice<Silent> && !GradableLattice<ForgedSingle>);
static_assert(Lattice<Stronger> && Lattice<Silent>, "the refusal is about orientation, not about the lattice laws");

struct AboutTheSlot : Silent {
    static constexpr ClaimSubject claim_subject = ClaimSubject::slot;
};
struct AboutTheBytes : Silent {
    static constexpr ClaimSubject claim_subject = ClaimSubject::bytes;
};

static_assert(claim_subject_v<Silent> == ClaimSubject::bytes, "a lattice that states nothing claims the bytes");
static_assert(claim_subject_v<AboutTheSlot> == ClaimSubject::slot);
static_assert(claim_subject_v<AboutTheBytes> == ClaimSubject::bytes);
static_assert(GradeIgnoresBytes<AboutTheSlot> && !GradeIgnoresBytes<AboutTheBytes> && !GradeIgnoresBytes<Silent>);

}  // namespace detail::claim_orientation_self_test

}  // namespace foundation::algebra
