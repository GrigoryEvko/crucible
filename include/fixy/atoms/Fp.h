#pragma once

// The floating-point-mode atoms.  Every atom here engages Axis::FpMode.
//
// FpMode is not Precision, and fixy/Axis.h says so at the enumerator:
// Precision names the element type, and one element type produces
// bit-different results under different rounding, flush-to-zero and
// contraction modes, so the mode needs its own axis.
//
// ---------------------------------------------------------------------
// One atom, a product, and why
//
// An FP mode is several independent settings at once — whether the
// compiler may reassociate, whether it may contract across statements,
// what happens to subnormals on the way in and on the way out.  An axis
// carries ONE grade per binding (fn's tier 4), so if each setting were
// its own atom a binding could name only one of them, and a body that
// contracts AND reassociates could not be typed.  The atom is therefore
// a product: fp::mode<Settings...> takes any subset of the settings,
// each from its own enum, each at most once.  A setting the mode does
// not name is at that enum's strict value, which is the first
// enumerator in every case — reassociation forbidden, contraction off,
// subnormals preserved, denormals honored.
//
// The four setting enums are the ones a collision rule reads.  The old
// FpModeLattice carried seven more — rounding, trap mask, NaN and Inf
// policy, complex layout, libm policy, constant rounding — and no rule
// reads any of them.  An atom no rule can reject on is decoration, so
// they are not restated here; the product accepts any enum, and the day
// a rule reads one of the seven, adding it is one enum and one line in
// the self-test.
//
// The enums are declared here verbatim because foundation ports no
// FpMode lattice and the old one at
// include/crucible/algebra/lattices/FpModeLattice.h was neither carried
// across nor recorded in port-drops.txt.  Same names, same values.
//
// ---------------------------------------------------------------------
// No lift
//
// The third of fixy/atoms/Sync.h's three readings.  A mode is how the
// arithmetic the body already performs is compiled, not an operation on
// a surface a context must admit.  What a mode does to a replay claim is
// F101's and F102's business, and both are collision rules.
//
// ---------------------------------------------------------------------
// One limitation, stated
//
// The family's only atom is parametric, so the namespace walk that keeps
// the other families' rosters honest covers nothing here, as in
// fixy/atoms/Observe.h.  The roster lists representative instantiations
// and the guard with teeth is the axis check plus the at-most-once pin.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/effects/Lift.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::fp {

// Old spellings: crucible::algebra::lattices::{FpReassociate, FpContract,
// FpFtz, FpDenormalInput}.  The first enumerator of each is the strict
// value a mode takes when it does not name the setting.
enum class FpReassociate : std::uint8_t {
    Forbidden = 0,
    BoundedTreeDepth = 1,  // log-N tree only, so the topology stays pinned
    UnrestrictedRewrite = 2,
};

enum class FpContract : std::uint8_t {
    Off = 0,
    OnInExpr = 1,  // contract within a single expression
    Fast = 2,  // contract across statements
};

enum class FpFtz : std::uint8_t {
    PreserveSubnormals = 0,
    FlushToZero = 1,
};

enum class FpDenormalInput : std::uint8_t {
    HonorDenormals = 0,
    DenormalsAreZero = 1,
};

namespace detail {

template <class E>
concept IsFpSetting = std::is_same_v<E, FpReassociate> || std::is_same_v<E, FpContract> || std::is_same_v<E, FpFtz>
                   || std::is_same_v<E, FpDenormalInput>;

// How many of the settings in a pack come from one enum.
template <class E, auto... Settings>
[[nodiscard]] consteval std::size_t settings_from_() noexcept {
    return ((std::is_same_v<decltype(Settings), E> ? 1u : 0u) + ... + 0u);
}

// Whether two settings are the same setting.  Two enumerators of
// different enums are never the same setting, and comparing them with
// == would be ill-formed rather than false, so the type check comes
// first and gates the comparison behind `if constexpr`.
template <auto A, auto B>
[[nodiscard]] consteval bool same_setting_() noexcept {
    if constexpr (std::is_same_v<decltype(A), decltype(B)>) {
        return A == B;
    } else {
        return false;
    }
}

}  // namespace detail

// The mode: any subset of the settings, each from its own enum, each at
// most once.  A repeated enum is refused here rather than resolved by
// order, because "the last one wins" is the kind of rule nobody reads.
template <auto... Settings>
    requires ((detail::IsFpSetting<decltype(Settings)> && ...))
          && (detail::settings_from_<FpReassociate, Settings...>() <= 1)
          && (detail::settings_from_<FpContract, Settings...>() <= 1)
          && (detail::settings_from_<FpFtz, Settings...>() <= 1)
          && (detail::settings_from_<FpDenormalInput, Settings...>() <= 1)
struct mode final : atom_of<Axis::FpMode> {
    // Whether the mode names a particular setting value.  A rule asks
    // this and nothing else.
    template <auto Wanted>
    static constexpr bool names = (detail::same_setting_<Settings, Wanted>() || ... || false);
};

}  // namespace fixy::atom::fp

namespace fixy::atom::detail {

using fp_atom_roster = std::tuple<fp::mode<>, fp::mode<fp::FpReassociate::UnrestrictedRewrite>,
                                  fp::mode<fp::FpReassociate::BoundedTreeDepth>, fp::mode<fp::FpContract::Fast>,
                                  fp::mode<fp::FpFtz::PreserveSubnormals>, fp::mode<fp::FpDenormalInput::HonorDenormals>,
                                  fp::mode<fp::FpContract::Fast, fp::FpReassociate::UnrestrictedRewrite>>;

}  // namespace fixy::atom::detail

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
