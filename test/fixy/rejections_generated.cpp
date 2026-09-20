// The combinatorial half of the gate's coverage, generated rather than
// written out.
//
// The old corpus spent 566 files on this, one per combination, and the
// count was the problem: a file per case cannot be read, cannot be kept
// in step with the atom catalog, and says nothing about the cases nobody
// wrote a file for.  What actually needs asserting is universal over the
// catalog, so it is one walk here and the catalog is its input.
//
// Three claims, each over every atom the tree ships — the join of the
// seven family rosters, which each family separately proves covers its
// own namespace:
//
//   1. Every atom, alone, passes the STRUCTURAL tiers.  It is an atom
//      (tier 2) and it names its axis once (tier 4).  Whether tier 5
//      then refuses it is the rules' and the corpus's business and is
//      reported below as data, not asserted: four atoms alone ARE
//      refused, because reject-by-default means a strict pole can be
//      half of a contradiction.  The run prints which four, so this
//      comment cannot drift out of step with them; at the time of
//      writing they are atom::with<IO> and atom::with<Bg, IO> (a
//      classified value on an observable channel, Security's strict
//      pole being classified), atom::stale_to<N> (a classified value
//      behind a replay window) and ctrl::longjmp_unsafe (a linear
//      value a longjmp can skip).
//
//   2. Any two atoms on ONE axis are refused, and refused BY TIER 4.
//      Asserting the tier rather than the bare rejection is what makes
//      the claim non-vacuous: a pair that happened to trip a collision
//      rule would satisfy `!IsAccepted` while saying nothing about the
//      one-grade-per-axis rule this walk exists to check.
//
//   3. Every atom that lifts to an effect row is admitted by a context
//      exactly when that row is empty.  The foreground context claims
//      Row<>, so the biconditional below says: a lifting atom with a
//      non-empty row cannot run there, and one that lifts to nothing
//      can.  The SyscallSurface atoms of fixy/atoms/Os.h are the atoms
//      that lift; atom::with<Es...> does not, which is why nothing can
//      gate a context on the Effect axis's own grade.
//
// A gate bug that admitted a duplicate, or an atom that stopped being
// one, fails the build here rather than going unnoticed for want of the
// file nobody wrote.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Collision.h>
#include <fixy/Corpus.h>
#include <fixy/Ctx.h>
#include <fixy/Reject.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdio>
#include <meta>
#include <string_view>
#include <vector>
#include <type_traits>

namespace {

namespace fe = ::foundation::effects;
// `Axis` is spelled ::fixy::Axis at every `^^` below rather than through
// this using-declaration: GCC 16.2 refuses "‘^^’ cannot be applied to a
// using-declaration", so a reflection of an imported name has to name the
// entity, not the import.
using ::fixy::Axis;
using ::fixy::IsAccepted;
using ::fixy::detail::reject::first_failing_tier_;
using ::fixy::detail::reject::Tier;

// The join of every family roster, and the reflections of its members.
// Collision.h already builds the join for its own atomless-axis walk,
// and reading the same one keeps the two from disagreeing about what
// "every atom" means.
using every_atom = ::fixy::collision::all_atom_roster;
inline constexpr auto atom_members = ::fixy::atom::detail::roster_members_v<every_atom>;

// ---------------------------------------------------------------------
// Claim 1: every atom, alone, clears the structural tiers.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval std::size_t atoms_clearing_the_structural_tiers() noexcept {
    std::size_t cleared = 0;
    template for (constexpr auto member : atom_members) {
        using A = [:member:];
        constexpr Tier failed = first_failing_tier_<int, A>();
        if (failed != Tier::Malformed && failed != Tier::Duplicate && failed != Tier::Payload) ++cleared;
    }
    return cleared;
}

static_assert(atoms_clearing_the_structural_tiers() == atom_members.size(),
              "an atom in a family roster does not clear fixy::fn's structural tiers on its own: it is not an "
              "atom (tier 2), or the walk reads its axis twice (tier 4).  A roster member that cannot be "
              "written alone cannot be written at all.");

// How many atoms tier 5 refuses on their own.  This is reported rather
// than pinned to a literal: it moves whenever a rule or a corpus entry
// is written, and the count is not the property — the property is that
// each refusal has a named reason, which the hand fixtures of A11.4
// carry one by one.
[[nodiscard]] consteval std::size_t atoms_refused_alone() noexcept {
    std::size_t refused = 0;
    template for (constexpr auto member : atom_members) {
        using A = [:member:];
        if (first_failing_tier_<int, A>() == Tier::Composition) ++refused;
    }
    return refused;
}

// The names of the atoms tier 5 refuses on their own, so the reader does
// not have to take the header comment's word for which four they are.
// Reported at run time rather than pinned: the set moves whenever a rule
// or a corpus entry is written, and the A11.4 hand fixtures are what pin
// each refusal to its reason.
//
// display_string_of rather than identifier_of because a roster member can
// be a class-template specialization — atom::stale_to<N> is one — and
// identifier_of throws `reflection with has_identifier false` on those.
// The spelling is toolchain-specific, which is why nothing here hashes
// it; it is printed for a human and nothing else reads it.
//
// The names are joined one character at a time through a vector<char>,
// which is neither the obvious spelling nor an aesthetic choice.  Two
// GCC 16.2 walls stand in the way of the obvious ones:
//
//   std::string{sv} + ", "  — refused with "‘(((const char*)(&"..."))
//     == 0)’ is not a constant expression", because basic_string's null
//     check on a pointer into a static string is not foldable there.
//     fixy/Axis.h carries the same workaround for string_view::find.
//
//   define_static_array of std::string_view — refused because
//     string_view is not a structural type, so a list of views cannot
//     cross out of the constant evaluation at all.
//
// A vector<char> walked by index dodges both, and define_static_string
// gives the result static storage so main can print it.
[[nodiscard]] consteval std::string_view refused_alone_names() {
    std::vector<char> joined;
    bool first = true;
    template for (constexpr auto member : atom_members) {
        using A = [:member:];
        if constexpr (first_failing_tier_<int, A>() == Tier::Composition) {
            if (!first) {
                joined.push_back(',');
                joined.push_back(' ');
            }
            first = false;
            const std::string_view name = std::meta::display_string_of(std::meta::dealias(member));
            for (std::size_t i = 0; i < name.size(); ++i) joined.push_back(name[i]);
        }
    }
    return std::string_view{std::define_static_string(joined)};
}

// The list and the count have to agree about the same roster: an empty
// list exactly when nothing is refused.
static_assert(refused_alone_names().empty() == (atoms_refused_alone() == 0),
              "the named list of atoms refused alone and the count of them disagree about whether anything is "
              "refused, so the two walks read different rosters.");

// Every atom alone is EITHER accepted or refused at tier 5, never at a
// structural tier.  The two counts partition the roster, which is the
// same claim as the assertion above read from the other side.
static_assert(atoms_refused_alone() <= atom_members.size());

[[nodiscard]] consteval std::size_t atoms_accepted_alone() noexcept {
    std::size_t accepted = 0;
    template for (constexpr auto member : atom_members) {
        using A = [:member:];
        if constexpr (IsAccepted<int, A>) ++accepted;
    }
    return accepted;
}

static_assert(atoms_accepted_alone() + atoms_refused_alone() == atom_members.size(),
              "every atom alone must be either accepted or refused at tier 5.  A member counted in neither "
              "was refused by a structural tier, which the assertion above already forbids, so the two walks "
              "disagree about the roster.");

// ---------------------------------------------------------------------
// Claim 2: two atoms on one axis are refused, and refused by tier 4.
//
// The pairs are generated: for each axis, the walk takes the first two
// rostered atoms that name it.  Picking from the roster rather than from
// a hand list is what makes an atom added to a family covered the day it
// is declared.

// The reflection of the Nth rostered atom on one axis, or void when the
// axis has fewer than N+1.
template <Axis A, std::size_t N>
[[nodiscard]] consteval std::meta::info nth_atom_on_axis() noexcept {
    std::size_t seen = 0;
    std::meta::info found = ^^void;
    template for (constexpr auto member : atom_members) {
        using Candidate = [:member:];
        if constexpr (Candidate::axis == A) {
            if (seen == N && found == ^^void) found = member;
            ++seen;
        }
    }
    return found;
}

template <Axis A>
[[nodiscard]] consteval bool axis_has_two_atoms() noexcept {
    return nth_atom_on_axis<A, 1>() != ^^void;
}

// For one axis: its first two atoms in a pack are refused by tier 4, and
// each alone clears tier 4.  The second half is what stops the first
// from passing because the atoms were malformed.
template <Axis A>
[[nodiscard]] consteval bool axis_refuses_its_own_pair() noexcept {
    if constexpr (!axis_has_two_atoms<A>()) {
        return true;  // an axis with one atom has no pair to refuse
    } else {
        using First = [:nth_atom_on_axis<A, 0>():];
        using Second = [:nth_atom_on_axis<A, 1>():];
        return first_failing_tier_<int, First, Second>() == Tier::Duplicate
            && first_failing_tier_<int, Second, First>() == Tier::Duplicate
            && first_failing_tier_<int, First>() != Tier::Duplicate
            && first_failing_tier_<int, Second>() != Tier::Duplicate
            && !IsAccepted<int, First, Second> && !IsAccepted<int, Second, First>;
    }
}

[[nodiscard]] consteval bool every_axis_refuses_its_own_pair() noexcept {
    bool all_refused = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        all_refused = all_refused && axis_refuses_its_own_pair<axis>();
    }
    return all_refused;
}

static_assert(every_axis_refuses_its_own_pair(),
              "two atoms naming one axis must be refused by tier 4, in either order, while each alone clears "
              "it.  A pair that reaches tier 5 means the one-grade-per-axis rule is not what refused it.");

// How many axes the pair check actually exercised.  An axis with one
// atom, or none, contributes a vacuous true above, so this is the number
// that carried weight.
[[nodiscard]] consteval std::size_t axes_with_a_pair() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis_has_two_atoms<axis>()) ++counted;
    }
    return counted;
}

// The walk would be vacuous if no axis had two atoms.  Eight axes have
// no atom at all (collision::pending_axes) and Type has none by design,
// so this floor says the generated pairs cover most of the rest.
static_assert(axes_with_a_pair() >= 15,
              "fewer than fifteen axes have two rostered atoms, so the generated-pair walk covers much less "
              "than it did.  Either the rosters shrank or the join lost a family.  The floor is deliberately "
              "below the current count (19) so that reworking one family does not red this TU, and deliberately "
              "not an equality so that ADDING an atom does not either.");

// ---------------------------------------------------------------------
// Claim 3: a lifting atom runs in a context exactly when its row is
// empty.

[[nodiscard]] consteval std::size_t lifting_atoms() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto member : atom_members) {
        using A = [:member:];
        if constexpr (fe::LiftsToRow<A>) ++counted;
    }
    return counted;
}

[[nodiscard]] consteval bool every_lifting_atom_needs_its_row() noexcept {
    bool all_gated = true;
    template for (constexpr auto member : atom_members) {
        using A = [:member:];
        if constexpr (fe::LiftsToRow<A>) {
            using Lifted = fe::lift_row_t<A>;
            constexpr bool empty_row = std::is_same_v<Lifted, fe::Row<>>;
            constexpr bool foreground_admits = fe::CtxAdmits<::fixy::HotFgCtx, Lifted>;
            all_gated = all_gated && (foreground_admits == empty_row);
        }
    }
    return all_gated;
}

static_assert(every_lifting_atom_needs_its_row(),
              "an atom that lifts to a non-empty effect row was admitted by the foreground context, whose "
              "row is empty, or one that lifts to nothing was refused by it.  The lift is what os mints fold "
              "into the row a context has to admit, so either direction breaks that gate.");

// The lift walk is only as strong as the number of atoms that lift.
static_assert(lifting_atoms() >= 30, "fewer than thirty rostered atoms carry a lift, so the context-fit walk "
                                     "covers much less than it did.  The SyscallSurface families of "
                                     "fixy/atoms/Os.h are what lift, and the current count is 39.");

#pragma GCC diagnostic pop

}  // namespace

int main() {
    // The claims above are constant-evaluated.  Printing the shape of
    // the input set is what a reader needs to judge whether the walk
    // covered anything, and it runs, so a build that somehow folded the
    // walks away still reports a count.
    std::printf("rejections_generated: %zu atoms walked, %zu accepted alone, %zu refused at tier 5,\n",
                atom_members.size(), atoms_accepted_alone(), atoms_refused_alone());
    std::printf("                      %zu axes contributed a same-axis pair, %zu atoms carry an effect lift\n",
                axes_with_a_pair(), lifting_atoms());
    static constexpr std::string_view refused = refused_alone_names();
    std::printf("                      refused alone: %.*s\n", static_cast<int>(refused.size()), refused.data());

    if (atom_members.empty()) return 1;
    if (atoms_accepted_alone() + atoms_refused_alone() != atom_members.size()) return 2;
    if (axes_with_a_pair() < 15) return 3;
    if (lifting_atoms() < 30) return 4;
    return 0;
}
