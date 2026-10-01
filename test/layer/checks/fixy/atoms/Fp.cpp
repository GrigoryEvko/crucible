// The compile-time checks of fixy/atoms/Fp.h.

#include <fixy/atoms/Fp.h>

namespace fixy::atom::detail::fp_atom_self_test {

using ::fixy::atom::fp::FpContract;
using ::fixy::atom::fp::FpDenormalInput;
using ::fixy::atom::fp::FpFtz;
using ::fixy::atom::fp::FpReassociate;
using ::fixy::atom::fp::mode;

static_assert(every_roster_member_is_atom_<fp_atom_roster>(),
              "fixy/atoms/Fp.h: a member of fp_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<fp_atom_roster, Axis::FpMode>(),
              "fixy/atoms/Fp.h: every FP-mode atom engages Axis::FpMode.");

// `names` answers for what the mode says and not for what it leaves at
// the strict value.
static_assert(mode<FpContract::Fast>::names<FpContract::Fast>);
static_assert(!mode<FpContract::Fast>::names<FpContract::Off>, "an unnamed setting is not reported as named");
static_assert(!mode<FpContract::Fast>::names<FpReassociate::UnrestrictedRewrite>);
static_assert(mode<FpContract::Fast, FpReassociate::UnrestrictedRewrite>::names<FpReassociate::UnrestrictedRewrite>);
static_assert(!mode<>::names<FpContract::Fast>);

// The settings are part of the identity, so tier 4 refuses two modes.
static_assert(!std::is_same_v<mode<FpContract::Fast>, mode<FpContract::OnInExpr>>);
static_assert(!std::is_same_v<mode<FpContract::Fast>, mode<>>);

// A repeated enum is refused by the constraint, which is what keeps the
// product a set rather than a list.
template <auto... S>
concept ModeAdmits = requires { typename mode<S...>; };
static_assert(ModeAdmits<FpContract::Fast, FpReassociate::UnrestrictedRewrite>);
static_assert(!ModeAdmits<FpContract::Fast, FpContract::Off>, "two settings from one enum");
static_assert(!ModeAdmits<FpReassociate::Forbidden, FpReassociate::UnrestrictedRewrite>);
static_assert(!ModeAdmits<7>, "a setting must come from one of the four enums");

// No lift.
static_assert(!::foundation::effects::LiftsToRow<mode<>>);
static_assert(!::foundation::effects::LiftsToRow<mode<FpContract::Fast>>);

}  // namespace fixy::atom::detail::fp_atom_self_test
